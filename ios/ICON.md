# iPhone and iPad app icon

`make-app-icon.py` creates an Xcode app icon catalog from caller-supplied artwork.
The repository contains no game artwork. Keep the source PNG and generated
catalog on the external build disk.

```sh
python3 ios/make-app-icon.py /path/to/your-icon.png \
  /Volumes/office/Build/OpenCrown/ios-app/Source/Assets.xcassets
```

Use square sRGB PNG artwork between 1024×1024 and 16384×16384 pixels. The script produces
18 opaque 8-bit RGB PNGs and `AppIcon.appiconset/Contents.json`, covering iPhone
20/29/40/60 pt, iPad 20/29/40/76/83.5 pt and 1024 px App Store artwork. Each entry uses
its correct point size and 1x/2x/3x scale. Transparent input is flattened onto black;
use `--background '#RRGGBB'` to choose another color. Use `--allow-upscale` explicitly for smaller square sources down to 256×256,
including a 512×512 dump icon. The 1024px marketing image is then enlarged;
resizing adds no detail. The app builder enables this only when
`DCP_APP_ICON_ALLOW_UPSCALE=1`. Supply artwork without rounded
corners: iOS applies the icon mask.

Pillow is used when available, otherwise installed ImageMagick. On a Mac with
neither, stock `sips` handles opaque 8-bit RGB PNG input; alpha-bearing artwork is
rejected with a clear message. No packages are installed automatically. The
script validates every output before replacing an existing AppIcon set and leaves
other asset sets in the catalog intact. `--name` selects another asset name.

The app builder must include the catalog in its target sources and set
`ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon`. The icon must be compiled into
`Assets.car` before signing; copying PNGs into a finished app does not change its
Home Screen icon. Reapply the catalog when generating a fresh build tree. The
launcher and build script can pass a caller-selected PNG through `DCP_APP_ICON`
when that optional builder integration is enabled.

For an independent compile check, create an output directory and run:

```sh
xcrun actool --compile /path/to/icon-check --platform iphoneos \
  --minimum-deployment-target 18.0 --app-icon AppIcon \
  --target-device iphone --target-device ipad \
  --output-partial-info-plist /path/to/icon-check/icon-info.plist \
  /path/to/Assets.xcassets
```

Generation and asset compilation validate sizes, opacity and Xcode's icon slots.
They do not verify how the artwork reads on a Home Screen or its legal provenance.
