#!/usr/bin/env python3
"""Prepare a temporary diagnostic session. Activation/restoration are explicit manual actions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SNAP = ROOT / 'server/build/compositor-diag-20261115-201859'
BUNDLE = SNAP / 'session-test'
DROPIN = Path('/run/systemd/system/lordos.service.d/90-lordos-compositor-diagnostic.conf')
USER = 'getsugah'
INSTALLED = Path('/usr/lib/lordos')
CONF = ('# LordOS compositor diagnostic: temporary, managed by tools/diag/session-test.py\n'
        '[Service]\nExecStart=\nExecStart=' + str(BUNDLE / 'bin/lord-sessiond') + '\n')


def run(*args):
    return subprocess.run(args, check=True, timeout=30)


def servers(uid):
    found = []
    for p in Path('/proc').glob('[0-9]*'):
        try:
            if p.joinpath('comm').read_text().strip() not in ('lord-server', 'lord-server-ins'):
                continue
            status = p.joinpath('status').read_text()
            actual = int(next(l for l in status.splitlines() if l.startswith('Uid:')).split()[1])
            if actual == uid:
                found.append(int(p.name))
        except (OSError, StopIteration, ValueError):
            continue
    return found


def verify():
    manifest = json.loads((SNAP / 'manifest.json').read_text())
    expected = manifest['sha256']['server/build/lord-server']
    for binary in (SNAP / 'server/build/lord-server', BUNDLE / 'bin/lord-server-instrumented'):
        if hashlib.sha256(binary.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Binaire différent du manifeste : {binary}')
    for p in ('bin/lord-sessiond', 'bin/lord-server', 'assets/fonts', 'themes/default/theme.bundle.js'):
        if not (BUNDLE / p).exists():
            raise RuntimeError(f'Ressource manquante : {p}')
    sessiond = INSTALLED / 'bin/lord-sessiond'
    if hashlib.sha256(sessiond.read_bytes()).digest() != hashlib.sha256((BUNDLE / 'bin/lord-sessiond').read_bytes()).digest():
        raise RuntimeError('lord-sessiond installé a changé : repréparer le test avant activation.')


def prepare():
    if os.geteuid() == 0:
        raise RuntimeError('--prepare doit être exécuté sans sudo')
    if BUNDLE.exists():
        verify()
        print(f'Déjà préparé : {BUNDLE}')
        return
    BUNDLE.mkdir()
    (BUNDLE / 'bin').mkdir()
    (BUNDLE / 'runs').mkdir(mode=0o700)
    for item in INSTALLED.iterdir():
        if item.name != 'bin':
            (BUNDLE / item.name).symlink_to(item, target_is_directory=item.is_dir())
    for item in (INSTALLED / 'bin').iterdir():
        if item.name not in ('lord-server', 'lord-sessiond'):
            (BUNDLE / 'bin' / item.name).symlink_to(item)
    shutil.copy2(SNAP / 'server/build/lord-server', BUNDLE / 'bin/lord-server-instrumented')
    shutil.copy2(INSTALLED / 'bin/lord-sessiond', BUNDLE / 'bin/lord-sessiond')
    # The unchanged session manager starts this wrapper with its normal filtered environment.
    wrapper = '''#!/bin/sh
target=0
previous=
for argument in "$@"; do
    if [ "$argument" = --greeter ]; then exec /usr/lib/lordos/bin/lord-server "$@"; fi
    if [ "$previous" = --user ] && [ "$argument" = getsugah ]; then target=1; fi
    previous=$argument
done
if [ "$target" = 1 ]; then
    export LORD_DIAG_FILE="BUNDLE/runs/internal-$$.csv"
    export LORD_DIAG_SECONDS=120
    exec "BUNDLE/bin/lord-server-instrumented" "$@"
fi
exec /usr/lib/lordos/bin/lord-server "$@"
'''.replace('BUNDLE', str(BUNDLE))
    (BUNDLE / 'bin/lord-server').write_text(wrapper)
    (BUNDLE / 'bin/lord-server').chmod(0o755)
    verify()
    print(f'Préparé, sans activation : {BUNDLE}')


def close_user_session(uid):
    targets = servers(uid)
    for pid in targets:
        os.kill(pid, signal.SIGTERM)
    deadline = time.monotonic() + 15
    while servers(uid) and time.monotonic() < deadline:
        time.sleep(.1)
    if servers(uid):
        raise RuntimeError('La session ne se termine pas. Aucun SIGKILL envoyé.')


def switch(restore):
    if os.geteuid() != 0 or os.environ.get('SUDO_USER') != USER:
        raise RuntimeError(f'Lancer avec sudo depuis le compte {USER}.')
    account = pwd.getpwnam(USER)
    if not restore:
        verify()
    if restore:
        if not DROPIN.exists() or DROPIN.read_text() != CONF:
            raise RuntimeError('Activation de ce test absente ou différente ; aucune modification.')
    elif DROPIN.exists():
        raise RuntimeError('Un test est déjà activé. Utiliser --restore.')
    # Keep this explicit user-invoked operation alive when its terminal closes.
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    action = 'restore' if restore else 'activate'
    stamp = time.strftime('%Y%m%d-%H%M%S')
    log_path = BUNDLE / 'runs' / f'{action}-{stamp}.log'
    log_fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.fchown(log_fd, account.pw_uid, account.pw_gid)
    print(f'La session va se fermer. Journal : {log_path}', flush=True)
    os.dup2(log_fd, 1)
    os.dup2(log_fd, 2)
    os.close(log_fd)
    sys.stdout.reconfigure(line_buffering=True)
    print('Action manuelle demandée :', action, time.strftime('%Y-%m-%d %H:%M:%S'))
    old_log = Path('/var/log/lordos/users') / USER / 'compositor.log'
    if old_log.is_file() and old_log.stat().st_size < 10 * 1024 * 1024:
        backup = BUNDLE / 'runs' / f'compositor-before-{action}-{stamp}.log'
        shutil.copyfile(old_log, backup)
        backup.chmod(0o600)
        os.chown(backup, account.pw_uid, account.pw_gid)
    changed = False
    try:
        run('systemctl', 'stop', 'lordos.service')
        close_user_session(account.pw_uid)
        if restore:
            DROPIN.unlink()
        else:
            DROPIN.parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(DROPIN, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
            with os.fdopen(fd, 'w') as f:
                f.write(CONF)
        changed = True
        run('systemctl', 'daemon-reload')
        run('systemctl', 'start', 'lordos.service')
        run('systemctl', 'is-active', 'lordos.service')
        print('Service démarré. Se reconnecter sur l’écran de connexion habituel.')
    except Exception:
        # On activation failure, return to the original ExecStart. Never remove unrelated overrides.
        if not restore and changed and DROPIN.exists() and DROPIN.read_text() == CONF:
            DROPIN.unlink()
        try:
            run('systemctl', 'daemon-reload')
            run('systemctl', 'start', 'lordos.service')
        except Exception as error:
            print('Retour automatique échoué :', error)
            print('Depuis une console : sudo systemctl start lordos.service')
        raise


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument('--prepare', action='store_true', help='prépare les fichiers, sans changer de service')
    group.add_argument('--check', action='store_true', help='vérifie les fichiers, sans changer de service')
    group.add_argument('--start-test', action='store_true', help='MANUEL : ferme la session et active le diagnostic temporaire')
    group.add_argument('--restore', action='store_true', help='MANUEL : ferme la session de test et revient au lancement installé')
    args = ap.parse_args()
    try:
        if args.prepare:
            prepare()
        elif args.check:
            verify()
            print('Fichiers vérifiés. Aucun service modifié.')
        else:
            switch(args.restore)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f'Erreur : {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
