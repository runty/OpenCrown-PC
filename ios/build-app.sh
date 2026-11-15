#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo 'Usage: ios/build-app.sh EXTERNAL_APP_OUTPUT DEVELOPMENT_TEAM REAL_GAME_OUTPUT' >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/.." && pwd)
output=$1
team=$2
game_output=$3
build_root=$(cd "$game_output/.." && pwd)
sdl_source=${DCP_SDL_SOURCE:-$build_root/macos-rendered/_deps/sdl3-src}
platform=${DCP_BUILD_PLATFORM:-ios}
extended_memory=${DCP_EXTENDED_MEMORY:-0}
case "$extended_memory" in
    0) memory_properties='{}'; memory_requested=false ;;
    1) memory_properties=$'\n        com.apple.developer.kernel.increased-memory-limit: true\n        com.apple.developer.kernel.extended-virtual-addressing: true'; memory_requested=true ;;
    *) echo 'DCP_EXTENDED_MEMORY must be 0 or 1' >&2; exit 2 ;;
esac
case "$platform" in
    ios) sdk=iphoneos; library_platform=ios; platform_library=iphoneos; destination='generic/platform=iOS' ;;
    simulator) sdk=iphonesimulator; library_platform=simulator; platform_library=iphonesimulator; destination='generic/platform=iOS Simulator' ;;
    *) echo 'DCP_BUILD_PLATFORM must be ios or simulator' >&2; exit 2 ;;
esac
python3 - "$output" <<'PY'
from pathlib import Path
import sys
path=Path(sys.argv[1]).resolve()
if len(path.parts)<3 or path.parts[1]!='Volumes' or not Path('/Volumes',path.parts[2]).is_mount():
    raise SystemExit('App output must be on a mounted external disk')
PY
mkdir -p "$output/Source/ThirdPartyNotices"
cp "$source_root/ios/app.mm" "$output/Source/main.mm"
cp "$game_output/$library_platform/libcpu-fixtures.a" "$output/Source/"
cp "$game_output/$library_platform/libfreetype.a" "$output/Source/"
cp "$game_output/$library_platform/libdcp-fp80.a" "$output/Source/"
python3 - "$game_output/$library_platform/artifact.json" "$output/Source" "$library_platform" <<'PY'
import hashlib,json,sys
from pathlib import Path
metadata=Path(sys.argv[1]);destination=Path(sys.argv[2])
if not metadata.is_file():
    raise SystemExit('Missing game artifact manifest; rebuild ios/game/build.py first')
data=json.loads(metadata.read_text())
expected={'libcpu-fixtures.a','libfreetype.a','libdcp-fp80.a'}
if data.get('schema')!=1 or data.get('platform')!=sys.argv[3] or set(data.get('libraries',{}))!=expected:
    raise SystemExit('Invalid game artifact manifest or target platform')
for name,wanted in data['libraries'].items():
    if hashlib.sha256((destination/name).read_bytes()).hexdigest()!=wanted:
        raise SystemExit('Game artifact changed while packaging: '+name)
(destination/'build-inputs.json').write_text(json.dumps(data,indent=2)+'\n')
PY
renderer="$build_root/ios-render/$platform_library"
if [[ $platform == simulator ]]; then renderer="$build_root/ios-render/simulator"; fi
cp "$renderer/libopencrown-mobile-replay.a" "$renderer/libopencrown-mobile-metal.a" "$renderer/sdl/libSDL3.a" "$output/Source/"
cp "$build_root/platform-probe/$platform_library/libdcp-platform.a" "$output/Source/"
cp "$build_root/storage-probe/$library_platform/libstorage.a" "$output/Source/"
for notices in "$game_output/ThirdPartyNotices" "$renderer/ThirdPartyNotices"; do
    if [[ -d $notices ]]; then cp "$notices/"* "$output/Source/ThirdPartyNotices/"; fi
done
app_icon_name=""
if [[ -n ${DCP_APP_ICON:-} ]]; then
    icon_options=("$DCP_APP_ICON" "$output/Source/Assets.xcassets")
    if [[ ${DCP_APP_ICON_ALLOW_UPSCALE:-0} == 1 ]]; then icon_options+=(--allow-upscale); fi
    python3 "$source_root/ios/make-app-icon.py" "${icon_options[@]}"
    app_icon_name=AppIcon
fi
revision=$(git -C "$source_root" rev-parse HEAD)
if [[ -n $(git -C "$source_root" status --porcelain) ]]; then revision="$revision-dirty"; fi
cat > "$output/project.yml" <<YAML
name: OpenCrownMobile
options:
  deploymentTarget:
    iOS: '18.0'
targets:
  OpenCrown:
    type: application
    platform: iOS
    sources:
      - path: Source
        excludes: ['*.a']
    dependencies:
      - framework: Source/libcpu-fixtures.a
        embed: false
      - framework: Source/libopencrown-mobile-replay.a
        embed: false
      - framework: Source/libopencrown-mobile-metal.a
        embed: false
      - framework: Source/libSDL3.a
        embed: false
      - framework: Source/libfreetype.a
        embed: false
      - framework: Source/libdcp-fp80.a
        embed: false
      - framework: Source/libdcp-platform.a
        embed: false
      - framework: Source/libstorage.a
        embed: false
      - sdk: UIKit.framework
      - sdk: Metal.framework
      - sdk: QuartzCore.framework
      - sdk: GameController.framework
      - sdk: Foundation.framework
      - sdk: AVFoundation.framework
      - sdk: AudioToolbox.framework
      - sdk: CoreAudio.framework
      - sdk: CoreMedia.framework
      - sdk: CoreVideo.framework
      - sdk: CoreBluetooth.framework
      - sdk: CoreGraphics.framework
      - sdk: CoreMotion.framework
      - sdk: OpenGLES.framework
      - sdk: UniformTypeIdentifiers.framework
      - sdk: CoreHaptics.framework
        weak: true
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: com.wadscorp.opencrown.mobile
        DEVELOPMENT_TEAM: '$team'
        CODE_SIGN_STYLE: Automatic
        ARCHS: arm64
        CLANG_CXX_LANGUAGE_STANDARD: c++20
        CLANG_ENABLE_OBJC_ARC: YES
        ASSETCATALOG_COMPILER_APPICON_NAME: "$app_icon_name"
        TARGETED_DEVICE_FAMILY: '1,2'
        HEADER_SEARCH_PATHS: '"$source_root/ios" "$source_root/linux-port/src" "$sdl_source/include"'
        LIBRARY_SEARCH_PATHS: '\$(PROJECT_DIR)/Source'
        CURRENT_PROJECT_VERSION: 1
        MARKETING_VERSION: '0.2'
    entitlements:
      path: OpenCrown.entitlements
      properties: $memory_properties
    info:
      path: Info.plist
      properties:
        CFBundleDisplayName: OpenCrown
        CFBundleShortVersionString: '\$(MARKETING_VERSION)'
        CFBundleVersion: '\$(CURRENT_PROJECT_VERSION)'
        DCPBuildID: '$revision'
        DCPExtendedMemoryRequested: $memory_requested
        UILaunchScreen: {}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UIFileSharingEnabled: true
        LSSupportsOpeningDocumentsInPlace: true
        UISupportedInterfaceOrientations: [UIInterfaceOrientationPortrait, UIInterfaceOrientationLandscapeLeft, UIInterfaceOrientationLandscapeRight]
YAML
xcodegen generate --spec "$output/project.yml" --project "$output"
options=()
if [[ $platform == simulator ]]; then options+=(CODE_SIGNING_ALLOWED=NO)
else options+=(-allowProvisioningUpdates -allowProvisioningDeviceRegistration); fi
xcodebuild -project "$output/OpenCrownMobile.xcodeproj" -scheme OpenCrown -configuration Release -destination "$destination" -derivedDataPath "$output/DerivedData" "${options[@]}" build
if [[ $platform == ios ]]; then codesign --verify --deep --strict "$output/DerivedData/Build/Products/Release-$sdk/OpenCrown.app"; fi

python3 - "$output" "$sdk" "$platform" "$extended_memory" <<'PYVERIFY'
import json,plistlib,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]);app=root/'DerivedData/Build/Products'/('Release-'+sys.argv[2])/'OpenCrown.app'
info=plistlib.loads((app/'Info.plist').read_bytes())
if info.get('CFBundleShortVersionString')!='0.2' or info.get('CFBundleVersion')!='1':
    raise SystemExit('App version metadata is missing or incorrect')
keys=['com.apple.developer.kernel.increased-memory-limit','com.apple.developer.kernel.extended-virtual-addressing']
result={'build_id':info.get('DCPBuildID'),'version':info['CFBundleShortVersionString'],'build':info['CFBundleVersion'],'platform':sys.argv[3],'signed_entitlements_verified':False,'extended_memory_requested':sys.argv[4]=='1'}
if sys.argv[3]=='ios':
    signed=plistlib.loads(subprocess.check_output(['codesign','-d','--entitlements',':-',str(app)],stderr=subprocess.DEVNULL))
    profile=plistlib.loads(subprocess.check_output(['security','cms','-D','-i',str(app/'embedded.mobileprovision')],stderr=subprocess.DEVNULL))['Entitlements']
    if sys.argv[4]=='1' and any(signed.get(key) is not True or profile.get(key) is not True for key in keys):
        raise SystemExit('Signed app/profile lack required memory entitlements')
    result['signed_entitlements_verified']=True
    result['memory_entitlements']={key:signed.get(key) is True and profile.get(key) is True for key in keys}
(root/'signing-verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
PYVERIFY
