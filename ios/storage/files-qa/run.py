#!/usr/bin/env python3
"""Exercise the real Simulator Files picker with an isolated local fixture."""
import argparse,json,subprocess,uuid
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
p.add_argument('--storage',type=Path,required=True)
p.add_argument('--simulator',required=True)
a=p.parse_args();out=a.output.resolve();storage=a.storage.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;lib=storage/'simulator/libstorage.a'
if not lib.is_file():p.error('missing Simulator storage library')
suffix=uuid.uuid4().hex
bundle='com.wadscorp.opencrown.qa.files'+suffix
display='OpenCrown QA '+suffix[:8]
q=lambda x:json.dumps(str(x))
spec=f'''name: FilesPickerQA
options:
  deploymentTarget:
    iOS: '18.0'
targets:
  FilesQA:
    type: application
    platform: iOS
    sources:
      - path: {q(source/'app.mm')}
    dependencies:
      - framework: {q(lib)}
        embed: false
      - sdk: UIKit.framework
      - sdk: UniformTypeIdentifiers.framework
      - sdk: Foundation.framework
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: {q(bundle)}
        LIBRARY_SEARCH_PATHS: {q(lib.parent)}
        HEADER_SEARCH_PATHS: {q(source.parent)}
        CLANG_CXX_LANGUAGE_STANDARD: c++20
        CLANG_ENABLE_OBJC_ARC: YES
        GENERATE_INFOPLIST_FILE: NO
        TARGETED_DEVICE_FAMILY: '1,2'
        ARCHS: arm64
    info:
      path: Info.plist
      properties:
        CFBundleDisplayName: {q(display)}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UIFileSharingEnabled: true
        LSSupportsOpeningDocumentsInPlace: true
        UILaunchScreen: {{}}
  FilesTests:
    type: bundle.ui-testing
    platform: iOS
    sources:
      - path: {q(source/'FilesTests.mm')}
    dependencies:
      - target: FilesQA
    settings:
      base:
        TEST_TARGET_NAME: FilesQA
        PRODUCT_BUNDLE_IDENTIFIER: {q(bundle+'.tests')}
schemes:
  FilesQA:
    build:
      targets:
        FilesQA: all
        FilesTests: [test]
    test:
      targets: [FilesTests]
'''
(out/'project.yml').write_text(spec)
def run(cmd,log):
 with (out/log).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
run(['xcodegen','generate','--spec',str(out/'project.yml'),'--project',str(out)],'generate.log')
base=['xcodebuild','-project',str(out/'FilesPickerQA.xcodeproj'),'-scheme','FilesQA','-configuration','Release',
      '-destination','platform=iOS Simulator,id='+a.simulator,'-derivedDataPath',str(out/'DerivedData'),'CODE_SIGNING_ALLOWED=NO']
run([*base,'build-for-testing'],'build.log')
run([*base,'-resultBundlePath',str(out/'test.xcresult'),'test-without-building'],'test.log')
root=Path(subprocess.check_output(['xcrun','simctl','get_app_container',a.simulator,bundle,'data'],text=True).strip())/'Documents'
name=(root/'qa-export-name.txt').read_text().strip()
if len(name)!=32 or any(c not in '0123456789abcdef' for c in name):raise RuntimeError('unexpected export name')
slot=root/'ProbeSaves/SAVEDATA00/SAVE0.DAT';marker=root/'protected.txt';incoming=root/'Incoming/SAVEDATA00/SAVE0.DAT'
if slot.read_bytes()!=b'before' or marker.read_bytes()!=b'marker' or incoming.read_bytes()!=b'after':raise RuntimeError('fixture contents differ')
checkpoints={p.parent.parent.name:p.read_text() for p in (root/'SaveCheckpoints').glob('*/SAVEDATA00/SAVE0.DAT')}
if not {'before','after'}.issubset(checkpoints.values()):raise RuntimeError('expected checkpoints missing')
exported=Path((root/'qa-export-path.txt').read_text()).resolve()
if exported.parent != root.resolve() or exported.name != name:raise RuntimeError('export destination is not the QA app local container')
if (exported/'SAVEDATA00/SAVE0.DAT').read_bytes()!=b'before':raise RuntimeError('exported copy differs')
result={'success':True,'simulator':a.simulator,'bundle':bundle,'current_save':'before',
        'exported_save':'before','checkpoints':checkpoints,'marker':'marker',
        'export_folder':name,'scope':'Synthetic data, QA app local container and real Simulator Files picker; no iCloud or physical device'}
(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
