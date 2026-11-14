#!/usr/bin/env python3
"""Exercise a copied Mac bundle with a player-supplied dump and disposable state."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--app', required=True, type=Path)
p.add_argument('--dump', required=True, type=Path)
p.add_argument('--save-copy', type=Path)
p.add_argument('--deny-build-dir', type=Path)
a = p.parse_args()
root = Path(tempfile.mkdtemp(prefix='bundle-smoke.', dir=a.app.resolve().parent))
app = root/'Relocated Café/OpenCrown.app'
app.parent.mkdir()
shutil.copytree(a.app, app)
state = root/'State Café'
(state/'saves').mkdir(parents=True)
if a.save_copy:
    shutil.copytree(a.save_copy, state/'saves/SAVEDATA00')
media = root/'External Disk Café'
media.mkdir()
(media/'Game').symlink_to(a.dump.resolve(), target_is_directory=True)
launcher = app/'Contents/MacOS/opencrown-launcher'
env = {'PATH': '/usr/bin:/bin', 'HOME': str(Path.home()), 'DCP_USER_DIR': str(state),
       'MTL_DEBUG_LAYER': '1', 'SDL_JOYSTICK_HIDAPI': '0', 'SDL_JOYSTICK_MFI': '0',
       'SDL_JOYSTICK_IOKIT': '0', 'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS': '1'}

def run(args, name, expected=0, extra=None):
    with (root/name).open('w') as out:
        done = subprocess.run([str(launcher), *args], cwd='/tmp', env=env | (extra or {}),
                              stdout=out, stderr=subprocess.STDOUT, timeout=75)
    assert done.returncode == expected, (name, done.returncode)
    return (root/name).read_text(errors='replace')

mode = None
try:
    if a.deny_build_dir:
        build = a.deny_build_dir.resolve()
        assert build != root and build not in root.parents and root not in build.parents
        mode = build.stat().st_mode & 0o7777
        build.chmod(0)
        assert not os.access(build, os.R_OK), 'build tree must actually be inaccessible'
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    text = run(['--update-check'], 'update.log')
    assert 'runty/OpenCrown-PC/releases' in text and 'disabled' in text
    run(['--validate-dump', str(media/'Game')], 'validate.log')
    run(['--validate-dump', str(root/'Missing Disk')], 'missing-validate.log', expected=1)
    run(['--configure', '--dump', str(media/'Game'), '--saves', str(state/'saves')], 'configure.log')
    settings = (state/'launcher.ini').read_text()
    assert str(media/'Game') in settings and str(state/'saves') in settings
    run(['--selftest'], 'selftest.log', extra={'DCP_LAUNCHER_SELFTEST_SHOT': str(root/'selftest.png')})
    for n in (1, 2):
        script = 'wait:30 start wait:20 start waitgame:180 wait:900 shot:running stopgame waitnogame:180 wait:30 shot:stopped quit'
        shots = root/f'session-{n}'
        shots.mkdir()
        run([], f'launch-{n}.log', extra={'DCP_LAUNCHER_PAD_SCRIPT': script, 'DCP_LAUNCHER_SHOT_DIR': str(shots)})
        log = state/'logs/launcher.log'
        text = log.read_text(errors='replace')
        assert 'TIMEOUT' not in text and 'render failed' not in text
        assert (shots/'running.png').exists() and (shots/'stopped.png').exists()
        game = state/'logs/last_run.log'
        shutil.copyfile(game, root/f'game-{n}.log')
        text = game.read_text(errors='replace')
        assert re.search(r'cpu: .*avx 1', text) and 'render:' in text
        frames = re.findall(r'FRAME (\d+) flips', text)
        assert frames and int(frames[-1]) > 100, 'guest must produce real frames'
        assert 'no system font found' not in text
        assert not re.search(r'SIGSEGV|SIGBUS|FATAL|Validation Error|validation error', text)
    assert (state/'launcher.ini').read_text() == settings, 'settings survive both app relaunches'
    # Simulate a detached external mount by moving only our own alias directory.
    shots = root/'disk-reconnect'
    shots.mkdir()
    script = 'wait:60 shot:available wait:360 shot:missing wait:360 shot:restored quit'
    proc = subprocess.Popen([str(launcher)], cwd='/tmp', env=env | {
        'DCP_LAUNCHER_PAD_SCRIPT': script, 'DCP_LAUNCHER_SHOT_DIR': str(shots)})
    def wait_file(name):
        until = time.monotonic() + 20
        while time.monotonic() < until:
            if (shots/name).exists(): return
            assert proc.poll() is None, 'launcher exited early'
            time.sleep(.05)
        raise RuntimeError(f'no screenshot {name}')
    detached = root/'Detached Disk Café'
    try:
        wait_file('available.png'); media.rename(detached)
        wait_file('missing.png'); detached.rename(media)
        assert proc.wait(timeout=20) == 0
    finally:
        if detached.exists() and not media.exists(): detached.rename(media)
        if proc.poll() is None: proc.terminate(); proc.wait(timeout=5)
    text = (state/'logs/launcher.log').read_text(errors='replace')
    assert text.count('launcher: dump available') >= 2 and 'launcher: dump unavailable' in text
    assert not re.search(r'failed assertion|Validation Error|validation error|render failed', text)
    report = {'app_relocated': True, 'spaces_and_unicode': True, 'minimal_path': env['PATH'],
              'build_tree_inaccessible': mode is not None, 'metal_launcher_selftest': True,
              'play_stop_full_relaunches': 2, 'guest_avx': True, 'settings_persist': True,
              'dump_alias_disconnect_reconnect': True, 'updates_disabled': True,
              'signature_verified': True, 'clean_account': False,
              'note': 'Disposable profile on reference host; no report submitted. Inspect saved PNGs.'}
    (root/'verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Bundle checks passed: {root}', flush=True)
finally:
    if mode is not None: a.deny_build_dir.chmod(mode)
    print(f'Evidence: {root}', flush=True)
