#!/usr/bin/env python3
"""Read-only, bounded live capture. Standard library only; no compositor commands."""
import argparse
import glob
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import time


def read(path):
    try:
        return Path(path).read_text(errors='replace').strip()
    except OSError as e:
        return {'unavailable': e.strerror}


def emit(f, kind, **values):
    f.write(json.dumps(dict(kind=kind, mono=time.monotonic(), wall=time.time(), **values)) + '\n')
    f.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid', type=int)
    ap.add_argument('--seconds', type=int, default=180)
    ap.add_argument('--output', type=Path, default=Path('/tmp'))
    ap.add_argument('--compare', action='store_true',
                    help='120 s guidées : pauses, collecte complète, proc seul, GPU seul')
    args = ap.parse_args()
    if args.compare:
        args.seconds = 120
    if not 10 <= args.seconds <= 600:
        ap.error('--seconds doit être compris entre 10 et 600')
    candidates = [int(p.parent.name) for p in Path('/proc').glob('[0-9]*/comm')
                  if read(p) == 'lord-server']
    pid = args.pid
    if pid is None:
        if len(candidates) != 1:
            ap.error('Lancez dans une session LordOS réelle ; si plusieurs serveurs : --pid PID. Aucun enregistrement lancé.')
        pid = candidates[0]
    if read(f'/proc/{pid}/comm') != 'lord-server':
        ap.error('Le PID doit désigner lord-server')
    identity = read(f'/proc/{pid}/stat')
    if not isinstance(identity, str):
        ap.error('stat du processus inaccessible')
    start_ticks = identity.rsplit(')', 1)[1].split()[19]
    dest = args.output / time.strftime('lordos-perf-%Y%m%d-%H%M%S')
    dest.mkdir(parents=True, exist_ok=False)
    gpu_paths = []
    for device in glob.glob('/sys/class/drm/card[0-9]*/device'):
        if not Path(device).parent.name[4:].isdigit():
            continue
        for name in ('gpu_busy_percent', 'mem_busy_percent', 'mem_info_vram_used',
                     'mem_info_vram_total', 'mem_info_gtt_used', 'pp_dpm_sclk', 'pp_dpm_mclk'):
            gpu_paths.append(str(Path(device) / name))
    connectors = {p: read(p) for pattern in ('/sys/class/drm/card*-*/status',
                  '/sys/class/drm/card*-*/enabled', '/sys/class/drm/card*-*/modes')
                  for p in glob.glob(pattern)}
    metadata = dict(pid=pid, uname=list(os.uname()), clk_tck=os.sysconf('SC_CLK_TCK'),
                    perf=shutil.which('perf'), perf_paranoid=read('/proc/sys/kernel/perf_event_paranoid'),
                    connectors=connectors, stderr=os.path.realpath(f'/proc/{pid}/fd/2'),
                    executable=os.path.realpath(f'/proc/{pid}/exe'), compare=args.compare)
    (dest / 'metadata.json').write_text(json.dumps(metadata, indent=2))
    print(f'Capture : {dest}\nMaximum {args.seconds} s, échantillons toutes les 0,5 s.\n'
          'Saisir : begin repos | end | begin déplacement | begin resize | begin firefox | begin vidéo\n'
          'Autres : note votre observation ; begin mono-écran ; quit. Entrée valide le marqueur.\n'
          'Quittez le terminal pour manipuler le bureau puis revenez saisir end. Ctrl-C termine proprement.', flush=True)
    since = time.time()
    started = time.monotonic()
    deadline = started + args.seconds
    next_sample = time.monotonic()
    active = None
    stdin_open = True
    plan = ('pause', 'complet', 'pause', 'proc', 'pause', 'gpu', 'pause', 'complet')
    last_phase = -1
    if args.compare:
        print('Comparaison automatique : 8 phases de 15 s. Rien à saisir.\n'
              'Répétez le même déplacement de fenêtre pendant les 120 s.\n'
              'Gardez le terminal visible sur le secondaire ; notez les phases fluides.\n'
              'pause = aucune lecture périodique proc/GPU ; proc = sans lecture GPU ;\n'
              'gpu = seulement les compteurs GPU ; complet = collecte originale.', flush=True)
    with (dest / 'samples.jsonl').open('w') as f:
        emit(f, 'start', pid=pid)
        try:
            while time.monotonic() < deadline:
                now = time.monotonic()
                mode = 'complet'
                if args.compare:
                    phase = min(int((now - started) // 15), len(plan) - 1)
                    mode = plan[phase]
                    if phase != last_phase:
                        if last_phase >= 0:
                            emit(f, 'end', scenario=active)
                        last_phase = phase
                        active = f'{phase + 1}-{mode}'
                        emit(f, 'begin', scenario=active)
                        print(f'Phase {phase + 1}/8 : {mode.upper()} — 15 secondes', flush=True)
                if now >= next_sample:
                    if mode in ('pause', 'gpu'):
                        # Keep the same timer in controls, but avoid all periodic /proc reads.
                        if mode == 'gpu':
                            emit(f, 'sample', scenario=active, gpu={p: read(p) for p in gpu_paths})
                        next_sample = time.monotonic() + 0.5
                        continue
                    stat = read(f'/proc/{pid}/stat')
                    if not isinstance(stat, str) or stat.rsplit(')', 1)[1].split()[19] != start_ticks:
                        emit(f, 'process-ended')
                        break
                    threads = {}
                    for task in Path(f'/proc/{pid}/task').glob('*'):
                        threads[task.name] = {name: read(task / name) for name in
                                             ('stat', 'schedstat', 'wchan', 'status')}
                    fdinfo = {}
                    seen = set()
                    for fd in Path(f'/proc/{pid}/fdinfo').glob('*'):
                        info = read(fd)
                        if isinstance(info, str) and 'drm-driver:' in info:
                            client = tuple(line for line in info.splitlines() if
                                           line.startswith(('drm-client-id:', 'drm-pdev:')))
                            if client and client in seen:
                                continue
                            seen.add(client)
                            fdinfo[fd.name] = info
                    emit(f, 'sample', scenario=active, cpu=read('/proc/stat'),
                         pressure={x: read('/proc/pressure/' + x) for x in ('cpu', 'memory', 'io')},
                         process=stat, threads=threads, drm=fdinfo,
                         gpu={p: read(p) for p in gpu_paths} if mode == 'complet' else {})
                    next_sample = time.monotonic() + 0.5
                ready, _, _ = select.select([sys.stdin] if stdin_open else [], [], [],
                                            max(0, min(next_sample, deadline) - time.monotonic()))
                if ready:
                    line = sys.stdin.readline()
                    if not line:
                        stdin_open = False
                        continue
                    line = line.strip()[:500]
                    if line.startswith('begin '):
                        if active:
                            emit(f, 'end', scenario=active, implicit=True)
                        active = line[6:]
                        emit(f, 'begin', scenario=active)
                    elif line == 'end':
                        emit(f, 'end', scenario=active)
                        active = None
                    elif line == 'quit':
                        break
                    else:
                        emit(f, 'note', text=line)
                    print('Marqueur enregistré.', flush=True)
        except KeyboardInterrupt:
            pass
        finally:
            if active:
                emit(f, 'end', scenario=active, implicit=True)
            emit(f, 'stop')
    until = time.time()
    # Never read fd/2: it may be a pipe consumed by the session manager.
    if shutil.which('journalctl'):
        with (dest / 'journal.txt').open('w') as log:
            try:
                subprocess.run(['journalctl', '--no-pager', '-o', 'short-unix',
                                '--since', '@' + str(since), '--until', '@' + str(until),
                                '_PID=' + str(pid), '-n', '1500'], stdout=log,
                               stderr=subprocess.STDOUT, timeout=8, check=False)
            except subprocess.TimeoutExpired:
                log.write('\nJournal interrompu après 8 secondes.\n')
    print(f'Terminé : {dest}. Transmettez ce chemin pour analyse.')


if __name__ == '__main__':
    main()
