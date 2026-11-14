#!/usr/bin/env python3
"""Package a relocatable, ad-hoc-signed Mac app. No game or player files are copied."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import time


def run(*args):
    return subprocess.check_output([str(x) for x in args], text=True, stderr=subprocess.STDOUT)


def dependencies(binary):
    deps = []
    for line in run('/usr/bin/otool', '-L', binary).splitlines()[1:]:
        name = line.strip().split(' (', 1)[0]
        if not name.startswith(('/System/Library/', '/usr/lib/')):
            raise RuntimeError(f'{binary.name}: non-system dependency {name}')
        deps.append(name)
    return deps


def package(build, output, replace=False, identifier='org.runty.opencrown'):
    root = Path(__file__).resolve().parent.parent
    build = build.resolve()
    output = output.absolute()
    if output.suffix != '.app' or output.name in ('.app', '..app'):
        raise RuntimeError('output must name a .app bundle')
    if output.exists() and not replace:
        raise RuntimeError(f'{output} exists; use --replace to retain it as a backup and replace it')
    if output.is_symlink():
        raise RuntimeError('refusing a symlink output')
    version = re.search(r'set\(OPENCROWN_VERSION "([\d.]+)"', (root/'linux-port/CMakeLists.txt').read_text()).group(1)
    source = run('git', '-C', root, 'rev-parse', 'HEAD').strip()
    dirty = bool(run('git', '-C', root, 'status', '--porcelain').strip())
    minimum = []
    for name in ('opencrown-launcher', 'dcp_run'):
        binary = build/name
        if run('/usr/bin/lipo', '-archs', binary).strip() != 'x86_64':
            raise RuntimeError(f'{name}: expected the complete x86_64 runtime')
        dependencies(binary)
        match = re.search(r'\bminos (\S+)', run('/usr/bin/vtool', '-show-build', binary))
        if not match:
            raise RuntimeError(f'{name}: missing deployment target')
        minimum.append(match.group(1))
    minimum_os = max(minimum, key=lambda x: tuple(map(int, x.split('.'))))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.opencrown-package-', dir=output.parent) as scratch:
        work = Path(scratch)
        app = work/'OpenCrown.app'
        executables = app/'Contents/MacOS'
        resources = app/'Contents/Resources'
        licenses = resources/'licenses'
        executables.mkdir(parents=True)
        licenses.mkdir(parents=True)
        manifest = {'version': version, 'source_commit': source, 'source_dirty': dirty,
                    'architecture': 'x86_64', 'deployment_target': minimum_os,
                    'game_files': 'external, not included', 'binaries': {}}
        for name in ('opencrown-launcher', 'dcp_run'):
            binary = executables/name
            shutil.copyfile(build/name, binary)
            binary.chmod(0o755)
            run('/usr/bin/strip', '-S', binary)
            run('/usr/bin/codesign', '--force', '--timestamp=none', '--sign', '-', binary)
            manifest['binaries'][name] = {'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                                         'dependencies': dependencies(binary)}
        texts = {
            'SDL3.txt': build/'_deps/sdl3-src/LICENSE.txt',
            'SDL3-hidapi-BSD.txt': build/'_deps/sdl3-src/src/hidapi/LICENSE-bsd.txt',
            'FreeType-FTL.txt': build/'_deps/freetype-src/docs/FTL.TXT',
            'MbedTLS-Apache-2.0.txt': build/'_deps/mbedtls-src/LICENSE',
            'miniupnpc-BSD-3.txt': build/'_deps/miniupnpc-src/LICENSE',
            'DearImGui-MIT.txt': build/'_deps/imgui-src/LICENSE.txt',
            'AMD-FidelityFX-FSR1-MIT.txt': root/'linux-port/packaging/licenses/AMD-FidelityFX-FSR1-MIT.txt',
        }
        for name, source_path in texts.items():
            shutil.copyfile(source_path, licenses/name)
        notices = (root/'linux-port/packaging/THIRD-PARTY-NOTICES.txt').read_text().replace('for Linux', 'for macOS')
        (resources/'THIRD-PARTY-NOTICES.txt').write_text(notices)
        iconset = work/'OpenCrown.iconset'
        iconset.mkdir()
        svg = root/'linux-port/packaging/opencrown.svg' # original project artwork, no game icon
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                name = f'icon_{size}x{size}' + ('@2x' if scale == 2 else '') + '.png'
                run('/usr/bin/sips', '-s', 'format', 'png', '-z', size*scale, size*scale, svg, '--out', iconset/name)
        run('/usr/bin/iconutil', '-c', 'icns', iconset, '-o', resources/'OpenCrown.icns')
        (resources/'build-info.json').write_text(json.dumps(manifest, indent=2)+'\n')
        (app/'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': identifier, 'CFBundleName': 'OpenCrown', 'CFBundleDisplayName': 'OpenCrown',
            'CFBundleExecutable': 'opencrown-launcher', 'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': version, 'CFBundleVersion': version,
            'CFBundleIconFile': 'OpenCrown.icns', 'NSHighResolutionCapable': True,
            'LSMinimumSystemVersion': minimum_os, 'LSApplicationCategoryType': 'public.app-category.games',
            'OpenCrownSourceRevision': source + ('-dirty' if dirty else ''),
            'NSHumanReadableCopyright': 'OpenCrown contributors. No game files included.',
        }))
        run('/usr/bin/codesign', '--force', '--timestamp=none', '--sign', '-', app)
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', app)
        backup = None
        if output.exists():
            backup = output.with_name(output.name + '.previous-' + time.strftime('%Y%m%d-%H%M%S') + f'-{os.getpid()}')
            output.rename(backup)
            print(f'Previous bundle retained: {backup}')
        try:
            app.rename(output)
        except OSError:
            if backup is not None and not output.exists():
                backup.rename(output)
            raise
    print(f'Packaged {output} ({source}{"-dirty" if dirty else ""}); macOS target {minimum_os}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--replace', action='store_true')
    parser.add_argument('--identifier', default='org.runty.opencrown')
    args = parser.parse_args()
    try:
        package(args.build_dir, args.output, args.replace, args.identifier)
    except subprocess.CalledProcessError as error:
        parser.exit(1, f'package: {error}\n{error.output}')
    except (OSError, RuntimeError) as error:
        parser.exit(1, f'package: {error}\n')


if __name__ == '__main__':
    main()
