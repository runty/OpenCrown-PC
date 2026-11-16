#!/usr/bin/env python3
"""Run picker lifecycle checks in a newly named, isolated Simulator app."""
import argparse,json,subprocess,time,uuid
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
p.add_argument('--storage',type=Path,required=True,help='storage build output containing simulator/libstorage.a')
p.add_argument('--simulator',required=True,help='explicitly authorized Simulator UDID')
a=p.parse_args();out=a.output.resolve();storage=a.storage.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;library=storage/'simulator/libstorage.a'
if not library.is_file():p.error('missing simulator storage library')
bundle='com.wadscorp.opencrown.qa.picker'+uuid.uuid4().hex
q=lambda x:json.dumps(str(x))
spec=f'''name: OpenCrownPickerQA
options:
  deploymentTarget:
    iOS: '18.0'
targets:
  PickerQA:
    type: application
    platform: iOS
    sources:
      - path: {q(source/'picker-qa.mm')}
    dependencies:
      - framework: {q(library)}
        embed: false
      - sdk: UIKit.framework
      - sdk: UniformTypeIdentifiers.framework
      - sdk: Foundation.framework
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: {q(bundle)}
        LIBRARY_SEARCH_PATHS: {q(library.parent)}
        HEADER_SEARCH_PATHS: {q(source)}
        CLANG_CXX_LANGUAGE_STANDARD: c++20
        CLANG_ENABLE_OBJC_ARC: YES
        GENERATE_INFOPLIST_FILE: NO
        TARGETED_DEVICE_FAMILY: '1,2'
        ARCHS: arm64
    info:
      path: Info.plist
      properties:
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UILaunchScreen: {{}}
'''
(out/'project.yml').write_text(spec)
def run(command,log):
 with (out/log).open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
run(['xcodegen','generate','--spec',str(out/'project.yml'),'--project',str(out)],'generate.log')
run(['xcodebuild','-project',str(out/'OpenCrownPickerQA.xcodeproj'),'-scheme','PickerQA','-configuration','Release',
     '-destination','generic/platform=iOS Simulator','-derivedDataPath',str(out/'DerivedData'),'CODE_SIGNING_ALLOWED=NO','build'],'build.log')
app=out/'DerivedData/Build/Products/Release-iphonesimulator/PickerQA.app'
subprocess.run(['xcrun','simctl','install',a.simulator,str(app)],check=True)
started=False
try:
 subprocess.run(['xcrun','simctl','launch',a.simulator,bundle],check=True);started=True
 root=Path(subprocess.check_output(['xcrun','simctl','get_app_container',a.simulator,bundle,'data'],text=True).strip())
 result=root/'Documents/picker-results.json'
 for _ in range(40):
  if result.exists():break
  time.sleep(1)
 else:raise RuntimeError('QA app did not produce a result')
 data=json.loads(result.read_text())
 if data!={'checks':16,'failed':0}:raise RuntimeError(f'Unexpected QA result: {data}')
 (out/'results.json').write_text(json.dumps({'bundle':bundle,'simulator':a.simulator,**data},indent=2)+'\n')
 print(json.dumps(data))
finally:
 if started:subprocess.run(['xcrun','simctl','terminate',a.simulator,bundle],check=False)
