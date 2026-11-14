# iPhone and iPad device probe

P0 builds a small UIKit arm64 app, with a Metal device capability query and an
exportable diagnostics file. It reports OS build, device model, host page size,
available storage, build revision and persistent launch count. The game runtime
will be connected after the CPU backend proof in P1/P2.

Build using Xcode, XcodeGen and a configured development team. Outputs stay on a
mounted external disk. The deployment target is provisionally iOS 18.0, with
physical verification currently limited to the reference devices' 27.2.1 builds.

```sh
./ios/build-probe.sh /Volumes/office/Build/OpenCrown/ios-probe TEAM_ID
xcrun devicectl device install app --device DEVICE_ID /Volumes/office/Build/OpenCrown/ios-probe/DerivedData/Build/Products/Release-iphoneos/OpenCrownProbe.app
xcrun devicectl device process launch --device DEVICE_ID com.wadscorp.opencrown.probe
xcrun devicectl device copy from --device DEVICE_ID --domain-type appDataContainer --domain-identifier com.wadscorp.opencrown.probe --source Documents/diagnostics.json --destination /path/to/diagnostics.json
```

The app is named OpenCrown Probe and has its own bundle ID. Its navigation-bar
share button exports the same JSON, and Files exposes its Documents folder.
A launch without LLDB checks standalone execution. Manual Home Screen launch
and share-sheet delivery are additional checks. The launch counter should
increase after terminating/relaunching and survive a repeat install.

See the [implementation plan](../docs/ios-ipados-port-plan.md) and
[progress record](progress.md).
