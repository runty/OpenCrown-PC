#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: ios/build-probe.sh EXTERNAL_BUILD_DIR DEVELOPMENT_TEAM [CPU_FIXTURE_OUTPUT]" >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=$1
team=$2
python3 - "$build_dir" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1]).resolve()
if len(p.parts) < 3 or p.parts[1] != 'Volumes' or not Path('/Volumes', p.parts[2]).is_mount():
    raise SystemExit('Build directory must be on a mounted external disk')
PY
mkdir -p "$build_dir/Source"
cp "$source_dir/ios/main.mm" "$build_dir/Source/main.mm"
cpu_enabled=0
library_dependency=""
if [[ $# -eq 3 ]]; then
    cp "$3/ios/libcpu-fixtures.a" "$build_dir/Source/libcpu-fixtures.a"
    cpu_enabled=1
    library_dependency=$'      - framework: Source/libcpu-fixtures.a\n        embed: false'
else
    rm -f "$build_dir/Source/libcpu-fixtures.a"
fi
revision=$(git -C "$source_dir" rev-parse HEAD)
if [[ -n $(git -C "$source_dir" status --porcelain) ]]; then revision="$revision-dirty"; fi
cat > "$build_dir/project.yml" <<YAML
name: OpenCrownProbe
options:
  deploymentTarget:
    iOS: '18.0'
targets:
  OpenCrownProbe:
    type: application
    platform: iOS
    sources: [Source]
    dependencies:
      - sdk: UIKit.framework
      - sdk: Metal.framework
$library_dependency
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: com.wadscorp.opencrown.probe
        DEVELOPMENT_TEAM: '$team'
        CODE_SIGN_STYLE: Automatic
        TARGETED_DEVICE_FAMILY: '1,2'
        CLANG_ENABLE_OBJC_ARC: YES
        LIBRARY_SEARCH_PATHS: '\$(PROJECT_DIR)/Source'
        GCC_PREPROCESSOR_DEFINITIONS: DCP_CPU_FIXTURES=$cpu_enabled
        CURRENT_PROJECT_VERSION: 1
        MARKETING_VERSION: '0.1'
    info:
      path: Info.plist
      properties:
        CFBundleDisplayName: OpenCrown Probe
        DCPBuildID: '$revision'
        UILaunchScreen: {}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UIFileSharingEnabled: true
        LSSupportsOpeningDocumentsInPlace: true
        UISupportedInterfaceOrientations: [UIInterfaceOrientationPortrait, UIInterfaceOrientationLandscapeLeft, UIInterfaceOrientationLandscapeRight]
YAML
xcodegen generate --spec "$build_dir/project.yml" --project "$build_dir"
xcodebuild -project "$build_dir/OpenCrownProbe.xcodeproj" -scheme OpenCrownProbe -configuration Release -destination generic/platform=iOS -derivedDataPath "$build_dir/DerivedData" -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
codesign --verify --deep --strict "$build_dir/DerivedData/Build/Products/Release-iphoneos/OpenCrownProbe.app"
