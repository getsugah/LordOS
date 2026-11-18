#!/usr/bin/env python3
"""Fresh GPU build and real Wayland wire test, in an isolated /tmp snapshot.

No package installation, dependency downloads, desktop startup or GPU/input access.
--sanitize instruments the production wl.c and harness; the remaining linked
objects are the optimized production build. Leak detection is disabled only for
the test process. ASan global checking stays enabled in this complete link.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, help='reuse an unchanged snapshot previously prepared here')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--prepare-only', action='store_true', help='compile without creating the private test socket')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--build-id', default='20261117.gpuvalidation')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    target = args.build_root.resolve() if args.build_root else Path(tempfile.mkdtemp(prefix='lordos-gpu-validation-'))
    manifest_file = target / 'source-sha256.json'
    if not manifest_file.exists():
        for name in ('server/src', 'server/mk', 'third_party'):
            shutil.copytree(repo / name, target / name)
        shutil.copytree(repo / 'server/build/gen', target / 'server/build/gen', ignore=shutil.ignore_patterns('*.o', '*.new'))
        shutil.copy2(repo / 'server/Makefile', target / 'server/Makefile')
        shutil.copy2(repo / 'package.json', target / 'package.json')
        manifest = {str(p.relative_to(target)): digest(p) for name in ('server/src', 'server/mk', 'third_party')
                    for p in (target / name).rglob('*') if p.is_file()}
        manifest_file.write_text(json.dumps(manifest, indent=2) + '\n')
    manifest = json.loads(manifest_file.read_text())
    for name, expected in manifest.items():
        if digest(repo / name) != expected or digest(target / name) != expected:
            raise SystemExit(f'snapshot/source mismatch: {name}; omit --build-root for a fresh build')
    for name in ('package.json', 'server/Makefile'):
        if digest(repo / name) != digest(target / name):
            raise SystemExit(f'snapshot/source mismatch: {name}; omit --build-root for a fresh build')
    (target / 'server/tests').mkdir(exist_ok=True)
    shutil.copy2(repo / 'server/tests/wl_wire_test.c', target / 'server/tests/wl_wire_test.c')
    sdk = repo / 'server/build/mouse-package-sdk/root'
    env = os.environ.copy()
    if sdk.exists():
        env['PKG_CONFIG_PATH'] = str(sdk / 'usr/lib/x86_64-linux-gnu/pkgconfig')
        env['PKG_CONFIG_SYSROOT_DIR'] = str(sdk)
    command = ['make', '-C', str(target / 'server'), 'CC=cc', 'CFLAGS=-O2 -g', 'GPU=1', 'SESSIOND=0',
               f'LORD_BUILD={args.build_id}', f'-j{max(1, args.jobs)}', 'build/lord-server']
    print(f'Artifacts: {target}', flush=True)
    with (target / 'build.log').open('a') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f'production build failed: {target / "build.log"}')
    lines = (target / 'build.log').read_text().splitlines()
    link = next(shlex.split(line) for line in reversed(lines) if line.startswith('cc ') and '-o build/lord-server ' in line)
    link[link.index('build/lord-server')] = 'build/wl-wire-test'
    link.remove('build/obj/wl/wl.o')
    flags = ['-D_GNU_SOURCE', '-DLORD_GPU', '-Isrc', '-Ibuild/gen', '-Ibuild/gen/client',
             '-isystem', '../third_party/quickjs', '-isystem', '../third_party/stb',
             '-O1' if args.sanitize else '-O2', '-g', '-Wall', '-Wextra', '-std=c11',
             'tests/wl_wire_test.c', '-Wl,--wrap=main']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
    link[1:1] = flags
    (target / 'wire-link-command.json').write_text(json.dumps(link, indent=2) + '\n')
    with (target / 'wire-build.log').open('w') as log:
        result = subprocess.run(link, cwd=target / 'server', stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f'wire test link failed: {target / "wire-build.log"}')
    binary = target / 'server/build/lord-server'
    ldd = subprocess.run(['ldd', str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout
    (target / 'ldd.txt').write_text(ldd)
    if 'not found' in ldd:
        raise SystemExit('missing production runtime library; see ldd.txt')
    report = {'production_binary_sha256': digest(binary),
              'harness_source_sha256': digest(target / 'server/tests/wl_wire_test.c'),
              'harness_binary_sha256': digest(target / 'server/build/wl-wire-test'),
              'gpu_build': True, 'sanitizer_scope': 'wl.c and harness' if args.sanitize else 'none',
              'asan_globals': bool(args.sanitize), 'leak_detection': False if args.sanitize else None,
              'production_warnings': [line for line in lines if 'warning:' in line],
              'wire_build_warnings': [line for line in (target / 'wire-build.log').read_text().splitlines() if 'warning:' in line],
              'runtime': 'not_run'}
    if not args.prepare_only:
        test_env = env.copy()
        if args.sanitize:
            test_env['ASAN_OPTIONS'] = 'detect_leaks=0'
        result = subprocess.run([str(target / 'server/build/wl-wire-test')], env=test_env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        (target / 'wire-test.log').write_text(result.stdout)
        print(result.stdout, end='')
        report['runtime'] = 'passed' if result.returncode == 0 else 'failed'
        report['runtime_exit_code'] = result.returncode
    (target / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Production SHA256: {report["production_binary_sha256"]}')
    print(f'Test binary: {target / "server/build/wl-wire-test"}')
    if report['runtime'] == 'failed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
