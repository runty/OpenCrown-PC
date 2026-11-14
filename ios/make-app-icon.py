#!/usr/bin/env python3
"""Build an iPhone/iPad AppIcon asset set from caller-supplied PNG artwork.

Generated art stays in the caller's output catalog; no artwork is bundled with
this script. Requires Pillow, ImageMagick, or opaque RGB input plus macOS sips.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
# idiom, point size, scale, physical pixel size; eighteen independent asset slots.
SPECS = (
    ("iphone", "20", 2, 40),
    ("iphone", "20", 3, 60),
    ("iphone", "29", 2, 58),
    ("iphone", "29", 3, 87),
    ("iphone", "40", 2, 80),
    ("iphone", "40", 3, 120),
    ("iphone", "60", 2, 120),
    ("iphone", "60", 3, 180),
    ("ipad", "20", 1, 20),
    ("ipad", "20", 2, 40),
    ("ipad", "29", 1, 29),
    ("ipad", "29", 2, 58),
    ("ipad", "40", 1, 40),
    ("ipad", "40", 2, 80),
    ("ipad", "76", 1, 76),
    ("ipad", "76", 2, 152),
    ("ipad", "83.5", 2, 167),
    ("ios-marketing", "1024", 1, 1024),
)


def run(command: list[str]) -> str:
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return result.stdout


def png_header(path: Path) -> tuple[int, int, int, int]:
    with path.open("rb") as stream:
        header = stream.read(33)
    if (
        len(header) != 33
        or header[:8] != PNG_SIGNATURE
        or header[8:16] != b"\0\0\0\rIHDR"
        or zlib.crc32(header[12:29]) != int.from_bytes(header[29:33], "big")
    ):
        raise ValueError(f"Not a valid PNG header: {path}")
    width, height, depth, color = struct.unpack(">IIBB", header[16:26])
    return width, height, depth, color


def rasterizer(source: Path, background: str):
    try:
        from PIL import Image, ImageOps
    except ImportError:
        Image = None
    if Image is not None:
        with Image.open(source) as image:
            image = ImageOps.exif_transpose(image).convert("RGBA")
            base = Image.new("RGBA", image.size, background)
            base.alpha_composite(image)
            rgb = base.convert("RGB")

        def resize(destination: Path, pixels: int) -> None:
            rgb.resize((pixels, pixels), Image.Resampling.LANCZOS).save(
                destination, "PNG"
            )

        return resize, "Pillow"
    magick = shutil.which("magick")
    if magick:

        def resize(destination: Path, pixels: int) -> None:
            run(
                [
                    magick,
                    str(source),
                    "-auto-orient",
                    "-colorspace",
                    "sRGB",
                    "-background",
                    background,
                    "-alpha",
                    "remove",
                    "-alpha",
                    "off",
                    "-filter",
                    "Lanczos",
                    "-resize",
                    f"{pixels}x{pixels}!",
                    "-depth",
                    "8",
                    "-strip",
                    "PNG24:" + str(destination),
                ]
            )

        return resize, "ImageMagick"
    # sips is sufficient when alpha/color conversion is unnecessary. Refuse an
    # alpha-bearing source instead of silently producing an App Store-invalid icon.
    sips = shutil.which("sips")
    _, _, depth, color = png_header(source)
    if not sips or depth != 8 or color != 2:
        raise ValueError(
            "Install Pillow/ImageMagick, or supply an opaque 8-bit RGB PNG for macOS sips"
        )
    alpha = run([sips, "-g", "hasAlpha", str(source)])
    if re.search(r"hasAlpha:\s*yes", alpha):
        raise ValueError("sips fallback requires PNG artwork without transparency")

    def resize(destination: Path, pixels: int) -> None:
        run(
            [
                sips,
                "--matchTo",
                "/System/Library/ColorSync/Profiles/sRGB Profile.icc",
                "--resampleHeightWidth",
                str(pixels),
                str(pixels),
                str(source),
                "--out",
                str(destination),
            ]
        )

    return resize, "sips"


def build(
    source: Path, catalog: Path, name: str, background: str, allow_upscale: bool = False
) -> str:
    source = source.resolve(strict=True)
    width, height, _, _ = png_header(source)
    minimum = 256 if allow_upscale else 1024
    if width != height or not minimum <= width <= 16384:
        raise ValueError(
            f"Artwork must be square, at least {minimum}×{minimum} and at most 16384×16384 pixels"
        )
    catalog = catalog.resolve()
    if catalog.suffix != ".xcassets":
        raise ValueError("Output must be an Assets.xcassets catalog directory")
    target = catalog / (name + ".appiconset")
    if target.is_symlink() or (target.exists() and not target.is_dir()):
        raise ValueError("App icon destination must be an ordinary directory")
    if target.exists() and source.is_relative_to(target):
        raise ValueError("Keep source artwork outside the generated app icon directory")
    resize, backend = rasterizer(source, background)
    catalog.mkdir(parents=True, exist_ok=True)
    # Finish and verify every raster before replacing any existing asset set.
    with tempfile.TemporaryDirectory(
        prefix=".opencrown-icon-", dir=catalog.parent
    ) as scratch:
        stage = Path(scratch) / "AppIcon.appiconset"
        stage.mkdir()
        images = []
        for idiom, points, scale, pixels in SPECS:
            filename = f"icon-{idiom}-{points}pt@{scale}x.png"
            destination = stage / filename
            resize(destination, pixels)
            if png_header(destination) != (pixels, pixels, 8, 2):
                raise ValueError(
                    f"Rasterizer did not produce opaque 8-bit RGB PNG: {filename}"
                )
            images.append(
                {
                    "idiom": idiom,
                    "size": f"{points}x{points}",
                    "scale": f"{scale}x",
                    "filename": filename,
                }
            )
        contents = {"images": images, "info": {"author": "xcode", "version": 1}}
        (stage / "Contents.json").write_text(json.dumps(contents, indent=2) + "\n")
        backup = Path(scratch) / "previous.appiconset"
        replaced = target.exists()
        if replaced:
            target.rename(backup)
        try:
            stage.rename(target)
        except BaseException:
            if replaced:
                backup.rename(target)
            raise
    root_contents = catalog / "Contents.json"
    if not root_contents.exists():
        root_contents.write_text(
            json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2) + "\n"
        )
    return backend


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source", type=Path, help="Caller-owned square PNG, at least 1024 pixels"
    )
    parser.add_argument(
        "catalog", type=Path, help="Generated Assets.xcassets directory"
    )
    parser.add_argument(
        "--allow-upscale",
        action="store_true",
        help="Accept artwork down to 256×256; marketing icon is enlarged to 1024×1024",
    )
    parser.add_argument(
        "--name", default="AppIcon", help="App icon asset name (default: AppIcon)"
    )
    parser.add_argument(
        "--background",
        default="#000000",
        help="Transparency flattening color, #RRGGBB (default: black)",
    )
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,63}", args.name):
        parser.error(
            "Asset name must start with a letter and contain only letters, digits, underscore or hyphen"
        )
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", args.background):
        parser.error("Background must be #RRGGBB")
    try:
        backend = build(
            args.source, args.catalog, args.name, args.background, args.allow_upscale
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Icon generation failed: {error}\n")
    print(
        f"Created 18 opaque iPhone/iPad PNGs in {args.catalog / (args.name + '.appiconset')} using {backend}"
    )


if __name__ == "__main__":
    main()
