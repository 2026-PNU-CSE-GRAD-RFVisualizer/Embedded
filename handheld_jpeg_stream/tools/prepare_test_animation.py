"""Convert animation source images into LCD-ready baseline JPEG frames."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageOps


LCD_SIZE = (800, 480)


def convert_frame(source: Path, destination: Path, quality: int) -> None:
    with Image.open(source) as image:
        rgb = ImageOps.exif_transpose(image).convert("RGB")
        fitted = ImageOps.fit(
            rgb,
            LCD_SIZE,
            method=Image.Resampling.LANCZOS,
            centering=(0.5, 0.5),
        )
        destination.parent.mkdir(parents=True, exist_ok=True)
        fitted.save(
            destination,
            format="JPEG",
            quality=quality,
            subsampling="4:2:0",
            optimize=True,
            progressive=False,
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--quality", type=int, default=45)
    args = parser.parse_args()

    sources = sorted(args.source_dir.glob("frame_*.png"))
    if len(sources) != 10:
        raise SystemExit(f"expected 10 PNG frames, found {len(sources)}")

    total_bytes = 0
    for index, source in enumerate(sources):
        destination = args.output_dir / f"frame_{index:02d}.jpg"
        convert_frame(source, destination, args.quality)
        size = destination.stat().st_size
        total_bytes += size
        print(f"{destination.name}: {size} bytes")

    print(f"total: {total_bytes} bytes")


if __name__ == "__main__":
    main()
