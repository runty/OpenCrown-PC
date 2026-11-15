#!/usr/bin/env python3
"""Checked file-copy regressions, original LOCK exclusion and SDK compile probes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--backend-root', type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    p.error('Output must be on a mounted external disk')
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[2]
files = [source / 'ios/cpu/runtime.cpp', source / 'ios/game/file-services.cpp',
         source / 'ios/game/file-memory-fixture.cpp']
headers = [source / 'ios/cpu/runtime.h', source / 'ios/game/file-services.h']
includes = ['-I' + str(a.backend_root.resolve() / 'remill/include')]
flags = ['-std=c++17', '-O1', '-g', '-fno-omit-frame-pointer', '-pthread', '-Wall', '-Wextra', '-Werror']
report = dict(sources={str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files + headers}, runs={}, sdk_compiles={})

def save():
    (out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')

def build(command, tag):
    with (out / (tag + '-build.log')).open('w') as log:
        subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)

for mode, sanitizers in [('native', []), ('asan-ubsan', ['-fsanitize=address,undefined']), ('tsan', ['-fsanitize=thread'])]:
    binary = out / ('file-memory-' + mode)
    build(['xcrun', 'clang++', *flags, *sanitizers, *includes, *files, '-o', binary], mode)
    result = subprocess.run([binary, out], text=True, capture_output=True, timeout=120,
        env=dict(os.environ, ASAN_OPTIONS='abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1', TSAN_OPTIONS='halt_on_error=1'))
    (out / (mode + '-stdout.log')).write_text(result.stdout)
    (out / (mode + '-stderr.log')).write_text(result.stderr)
    report['runs'][mode] = dict(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)
    save()
    print(mode, result.returncode, result.stdout.strip(), flush=True)
    if result.returncode:
        raise SystemExit('File memory fixture failed: ' + mode)

# This deliberately bypasses publication locking only in the standalone test
# callback. It must fail the functional exclusion assertion, without depending
# on a sanitizer to discover a particular scheduling race.
negative = subprocess.run([out / 'file-memory-native', out, '--unsafe-publication'], text=True, capture_output=True, timeout=120)
report['negative_control'] = dict(exit_code=negative.returncode, stdout=negative.stdout, stderr=negative.stderr)
save()
if negative.returncode != 1 or 'native file output crossed original LOCK' not in negative.stderr:
    raise SystemExit('Unsafe publication negative control was not detected')
print('unsafe publication correctly rejected', flush=True)
for platform, sdk, target in [('ios', 'iphoneos', 'arm64-apple-ios18'), ('simulator', 'iphonesimulator', 'arm64-apple-ios18-simulator')]:
    directory = out / platform
    directory.mkdir(exist_ok=True)
    sdk_path = subprocess.check_output(['xcrun', '--sdk', sdk, '--show-sdk-path'], text=True).strip()
    for file in files:
        build(['xcrun', '--sdk', sdk, 'clang++', '-target', target, '-isysroot', sdk_path,
               *flags, *includes, '-c', file, '-o', directory / (file.stem + '.o')], platform + '-' + file.stem)
    report['sdk_compiles'][platform] = dict(target=target, sources=[str(f) for f in files])
    save()
    print(platform, 'compiled', flush=True)
