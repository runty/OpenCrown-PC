import hashlib, json, os, shutil, subprocess, time, uuid
from pathlib import Path

root = Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate')
out = root / 'simulator-batch-ruins-retry'
out.mkdir(exist_ok=False)
sim = 'F30116F9-F84C-44AA-A1A4-5A942B779628'
bundle = 'com.wadscorp.opencrown.mobile'
revision = 'qa-scale-batch-a3e07c2'
save_hash = '728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac'

def command(*args, **kwargs):
    return subprocess.run(['xcrun', 'simctl', *map(str, args)], check=True,
                          text=True, capture_output=True, **kwargs)

def docs():
    return Path(command('get_app_container', sim, bundle, 'data').stdout.strip()) / 'Documents'

def manifest(folder):
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in folder.rglob('*') if p.is_file() and p.relative_to(folder).parts[0] != 'Game'}

old_docs = docs()
assert json.loads((old_docs / 'diagnostics-live.json').read_text())['phase'] in ('launcher', 'stopped')
before = manifest(old_docs)
(out / 'before.json').write_text(json.dumps(before, indent=2))
for relative in before:
    target = out / 'before' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(old_docs / relative, target)
command('install', sim, root / 'app-simulator-batch-qa/DerivedData/Build/Products/Release-iphonesimulator/OpenCrown.app')
documents = docs()
assert manifest(documents) == before, 'Install changed existing state'
save_id = str(uuid.uuid4()).upper()
save = documents / 'DiagnosticSessions' / save_id / 'ProbeSaves'
shutil.copytree(root.parent / 'cursor-investigation/adventure-cleared-save', save)
assert hashlib.sha256((save / 'SAVEDATA00/SAVE0.DAT').read_bytes()).hexdigest() == save_hash
input_file = documents / 'diagnostic-input.txt'
original = input_file.read_bytes()
input_file.write_bytes(Path(__file__).with_name('batch-ruins-extended-input.txt').read_bytes())
env = os.environ.copy()
values = dict(DCP_DIAGNOSTIC_SECONDS='240', DCP_DIAGNOSTIC_RESUME_UUID=save_id,
              DCP_CHECKPOINT_SCENE='GOBLIN', DCP_CHECKPOINT_SETTLE_FRAMES='720',
              DCP_MAX_TRACE_ENTRIES='9400000000', DCP_MAX_VBLANKS='90000',
              DCP_CHECKPOINT_INPUT=Path('/Volumes/office/Build/OpenCrown/OpenCrown-sse-nan/ios/host/routes/ruins-first-encounter.txt').read_text(), DCP_MUTE_AUDIO='1',
              DCP_TEXTURE_VERSIONS='1', DCP_TEXTURE_VERSION_VERIFY='1')
env.update({'SIMCTL_CHILD_' + k: v for k, v in values.items()})
(out / 'session.json').write_text(json.dumps(dict(simulator=sim, save_session_id=save_id,
    build_id=revision, environment=values, scope='Texture version audit against full content; no performance or manual save qualification'), indent=2))
try:
    launch = command('launch', '--terminate-running-process', sim, bundle, '--diagnostic-startup', env=env)
    (out / 'launch.txt').write_text(launch.stdout)
    start = time.monotonic()
    last = None
    while time.monotonic() - start < 270:
        time.sleep(3)
        report = json.loads((documents / 'diagnostics-live.json').read_text())
        if report.get('build_id') != revision or not report.get('diagnostic_startup'):
            continue
        guest = report.get('guest', {})
        state = report['phase'], guest.get('current_scene')
        if state != last:
            print(state, report.get('captured_game_frames'), flush=True)
            last = state
        if report['phase'] == 'stopped':
            break
    else:
        raise RuntimeError('Timed out waiting for diagnostic completion')
    (out / 'final.json').write_text(json.dumps(report, indent=2))
    guest = report['guest']
    assert guest['checkpoint_complete'] and guest['current_scene'] == 'GOBLIN'
    assert report['batch_qa']['fast'] > 0 and report['batch_qa']['elements'] > 0, 'Batch fast path was not exercised'
    assert not any((guest['error'], report['frontend_error'], report['save_error'], report.get('platform_error', '')))
    assert report['diagnostic_resumed'] and report['save_session_id'] == save_id
    file_memory = guest['file_memory']
    assert file_memory['synchronized_copies'] and file_memory['write_bytes'] > 1000000 and file_memory['read_bytes'] > 0
    font_memory = guest['font_memory']
    assert font_memory['synchronized_copies'] and font_memory['read_bytes'] > 0 and font_memory['write_bytes'] > 0
    versions = guest['texture_versions']
    assert versions['enabled'] and versions['verifying'] and versions['version_hits'] > 0 and not versions['proof_failures']
    textures = report['textures']
    assert versions['version_hits'] == textures['version_hits'] == versions['verified_checks']
    assert versions['verified_bytes'] == textures['version_matched_bytes']
    assert textures['available'] and textures['comparisons'] > 0
    assert textures['comparison_hits'] <= textures['comparisons']
    assert textures['matched_span_bytes'] <= textures['comparison_span_bytes']
    assert textures['hashed_bytes'] == textures['copied_bytes'] > 0
    assert textures['uploads'] > 0 and textures['snapshot_bytes'] <= textures['snapshot_budget_bytes']
    assert hashlib.sha256((save / 'SAVEDATA00/SAVE0.DAT').read_bytes()).hexdigest() == save_hash
    shutil.copy2(documents / 'DiagnosticSessions' / report['session_id'] / 'last-game-frame.png', out / 'final.png')
    verification = dict(success=True, frames=report['captured_game_frames'], textures=textures, file_memory=file_memory, font_memory=font_memory, texture_versions=versions,
                        save_sha256=save_hash, scope='QA batch wrapper, startup and first Ruins encounter; not device performance')
    (out / 'verification.json').write_text(json.dumps(verification, indent=2))
    print(json.dumps(verification), flush=True)
finally:
    input_file.write_bytes(original)
    command('terminate', sim, bundle)
    command('install', sim, root / 'app-simulator/DerivedData/Build/Products/Release-iphonesimulator/OpenCrown.app')
    documents = docs()
    input_file = documents / 'diagnostic-input.txt'
    command('launch', '--terminate-running-process', sim, bundle,
            env=dict(os.environ, SIMCTL_CHILD_DCP_MUTE_AUDIO='1'))
    after = manifest(documents)
    allowed = {'diagnostics-live.json', 'diagnostics.json'}
    changed = [name for name, digest in before.items() if after.get(name) != digest and name not in allowed]
    (out / 'preservation.json').write_text(json.dumps(dict(unchanged_existing_files=not changed,
        changed_existing=changed, input_restored=input_file.read_bytes() == original), indent=2))
    assert not changed, changed
