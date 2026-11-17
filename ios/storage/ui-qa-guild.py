#!/usr/bin/env python3
"""Run the post-adventure Guild path in an isolated, installed Simulator QA app."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--simulator', required=True)
p.add_argument('--bundle', required=True)
p.add_argument('--build-id', required=True)
p.add_argument('--seed', type=Path, required=True)
p.add_argument('--save-sha256', required=True)
p.add_argument('--require-observer', action='store_true')
a = p.parse_args()
out, seed = a.output.resolve(), a.seed.resolve()
for path in (out, seed):
    if len(path.parts) < 3 or path.parts[1] != 'Volumes' or not Path('/Volumes', path.parts[2]).is_mount():
        p.error('output and disposable seed must be on a mounted external disk')
if out == seed or out in seed.parents or seed in out.parents:
    p.error('output and seed must not overlap')
if not a.bundle.startswith('com.wadscorp.opencrown.qa.') or len(a.build_id) != 40:
    p.error('a separate QA bundle and exact 40-character build ID are required')
if len(a.save_sha256) != 64 or any(c not in '0123456789abcdef' for c in a.save_sha256):
    p.error('save SHA-256 must be lowercase hexadecimal')
out.mkdir(parents=True, exist_ok=False)

def simctl(*args, **kwargs):
    return subprocess.run(['xcrun', 'simctl', *map(str, args)], check=True,
                          capture_output=True, text=True, **kwargs)

def docs(bundle):
    return Path(simctl('get_app_container', a.simulator, bundle, 'data').stdout.strip()) / 'Documents'

def inventory(root):
    return {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in sorted(root.rglob('*')) if f.is_file()}

qa = docs(a.bundle)
normal = docs('com.wadscorp.opencrown.mobile')
if qa == normal or not (qa / 'Game').is_dir():
    raise SystemExit('QA app must have separate, already validated game data')
protected = ('ProbeSaves', 'SaveCheckpoints', '.ProbeSaves.previous')
def protected_inventory():
    return {str(f.relative_to(qa)): hashlib.sha256(f.read_bytes()).hexdigest()
            for part in protected for f in (qa / part).rglob('*') if f.is_file()}

seed_before = inventory(seed)
if seed_before.get('SAVEDATA00/SAVE0.DAT') != a.save_sha256:
    raise SystemExit('unexpected disposable seed hash')
before = protected_inventory()
saveid = str(uuid.uuid4()).upper()
save = qa / 'DiagnosticSessions' / saveid / 'ProbeSaves'
shutil.copytree(seed, save)
if inventory(save) != seed_before:
    raise SystemExit('seed copy did not match')
source = Path(__file__).resolve().parent
shutil.copyfile(source / 'ui-qa-ruins-input.txt', qa / 'diagnostic-input.txt')
route = ('0 0\n600 LS_RIGHT\n1300 0\n1340 LS_RIGHT\n1455 0\n'
         '1495 LS_UP\n1575 0\n' +
         ''.join(f'{t} CROSS\n{t+8} 0\n' for t in range(1700, 3300, 100)) +
         '3500 0\n')
values = dict(DCP_DIAGNOSTIC_SECONDS='240', DCP_DIAGNOSTIC_RESUME_UUID=saveid,
              DCP_CHECKPOINT_SCENE='TOWN', DCP_CHECKPOINT_SETTLE_FRAMES='3600',
              DCP_MAX_TRACE_ENTRIES='9400000000', DCP_MAX_VBLANKS='90000',
              DCP_CHECKPOINT_INPUT=route, DCP_MUTE_AUDIO='1',
              DCP_TEXTURE_VERSIONS='1', DCP_TEXTURE_VERSION_VERIFY='1')
env = os.environ.copy()
env.update({'SIMCTL_CHILD_' + k: v for k, v in values.items()})
(out / 'intent.json').write_text(json.dumps({'bundle': a.bundle, 'session': saveid,
    'seed_sha256': a.save_sha256, 'environment': values, 'observer_required': a.require_observer}, indent=2) + '\n')
try:
    launch = simctl('launch', '--terminate-running-process', a.simulator, a.bundle,
                    '--diagnostic-startup', env=env)
    (out / 'launch.txt').write_text(launch.stdout)
    deadline = time.monotonic() + 280
    last = None
    while time.monotonic() < deadline:
        time.sleep(3)
        try:
            report = json.loads((qa / 'diagnostics-live.json').read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            continue
        if report.get('save_session_id') != saveid or not report.get('diagnostic_startup'):
            continue
        state = (report.get('phase'), report.get('guest', {}).get('current_scene'))
        if state != last:
            print(state, report.get('captured_game_frames'), flush=True)
            last = state
        if state[0] == 'stopped':
            break
    else:
        raise RuntimeError('timed out waiting for the same QA session; inspect before restarting')
    (out / 'final.json').write_text(json.dumps(report, indent=2) + '\n')
    g = report['guest']
    v = g['texture_versions']
    if (report['build_id'] != a.build_id or not g['booted'] or not g['game_code_executed'] or
        not g['checkpoint_complete'] or g['current_scene'] != 'TOWN' or
        any((g['error'], report['frontend_error'], report['save_error'], report.get('platform_error', ''))) or
        not report['diagnostic_resumed'] or report['save_session_id'] != saveid):
        raise RuntimeError('runtime or checkpoint validation failed; inspect retained report')
    if 'reference stub: sceNpTrophyUnlockTrophy' not in g['events']:
        raise RuntimeError('Guild trophy call not observed')
    if a.require_observer and not g.get('qa_guild_switch_hits', 0):
        raise RuntimeError('Guild switch case not observed')
    if not v['enabled'] or not v['verifying'] or not v['verified_checks'] or v['proof_failures'] or v['version_hits'] != v['verified_checks']:
        raise RuntimeError('texture verification failed')
    if inventory(save) != seed_before:
        raise RuntimeError('disposable working save changed')
    frame = qa / 'DiagnosticSessions' / report['session_id'] / 'last-game-frame.png'
    shutil.copy2(frame, out / 'final.png')
    result = {'frames': report['captured_game_frames'], 'texture_checks': v['verified_checks'],
              'guild_switch_hits': g.get('qa_guild_switch_hits'), 'trophy_call_observed': True,
              'scope': 'Separate muted Simulator QA; not physical-device or performance acceptance'}
    (out / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
finally:
    after = protected_inventory()
    preserved = {'protected_qa_files_unchanged': before == after,
                 'seed_unchanged': seed_before == inventory(seed)}
    (out / 'preservation.json').write_text(json.dumps(preserved, indent=2) + '\n')
    if not all(preserved.values()):
        raise RuntimeError('protected QA files or seed changed')
