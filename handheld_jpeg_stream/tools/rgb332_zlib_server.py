"""Serve baseline JPEG or RGB332+zlib frames to handheld_jpeg_stream.

Requires Pillow. JPEG mode always re-encodes input as non-progressive 800x480
JPEG because the ESP32 decoder does not accept progressive JPEG files.
"""

from __future__ import annotations

import argparse
import io
import socket
import struct
import time
import zlib
from pathlib import Path

MAGIC = 0x52464A46
VERSION = 1
FLAG_JPEG = 0
FLAG_RGB332_ZLIB = 1
WIDTH = 800
HEIGHT = 480
HEADER = struct.Struct(">IBBIQI")


def open_rgb_image(path: Path):
    try:
        from PIL import Image
    except ImportError as error:
        raise SystemExit(
            "Pillow is required: python -m pip install -r tools/requirements.txt"
        ) from error

    with Image.open(path) as source:
        return source.convert("RGB").resize(
            (WIDTH, HEIGHT), Image.Resampling.LANCZOS
        )


def encode_jpeg(path: Path, quality: int) -> bytes:
    image = open_rgb_image(path)
    try:
        output = io.BytesIO()
        image.save(
            output,
            format="JPEG",
            quality=quality,
            progressive=False,
            optimize=False,
            subsampling=2,
        )
        payload = output.getvalue()
    finally:
        image.close()
    if not payload.startswith(b"\xff\xd8") or not payload.endswith(b"\xff\xd9"):
        raise RuntimeError(f"failed to create a complete JPEG from {path}")
    return payload


def encode_rgb332_zlib(path: Path, level: int) -> bytes:
    image = open_rgb_image(path)
    try:
        rgb332 = bytearray(WIDTH * HEIGHT)
        for index, (red, green, blue) in enumerate(image.getdata()):
            rgb332[index] = (red & 0xE0) | ((green >> 3) & 0x1C) | (blue >> 6)
        return zlib.compress(rgb332, level)
    finally:
        image.close()


def image_paths(source: Path) -> list[Path]:
    if source.is_file():
        return [source]
    if not source.is_dir():
        raise SystemExit(f"image file/directory does not exist: {source}")
    return sorted(
        path for path in source.iterdir()
        if path.suffix.lower() in {".jpg", ".jpeg", ".png", ".bmp"}
    )


def load_frames(source: Path, encoding: str, level: int,
                quality: int) -> tuple[int, list[bytes]]:
    paths = image_paths(source)
    if not paths:
        raise SystemExit(f"no image files found in {source}")
    if encoding == "jpeg":
        flag = FLAG_JPEG
        frames = [encode_jpeg(path, quality) for path in paths]
    else:
        flag = FLAG_RGB332_ZLIB
        frames = [encode_rgb332_zlib(path, level) for path in paths]
    for path, payload in zip(paths, frames):
        print(f"encoded {path.name} as {encoding}: {len(payload):,} bytes")
    return flag, frames


def serve(host: str, port: int, fps: float, flag: int,
          frames: list[bytes]) -> None:
    period = 1.0 / fps
    sequence = 0
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((host, port))
        listener.listen(1)
        print(f"listening on {host}:{port}, {fps:g} FPS, flags={flag}")
        while True:
            connection, address = listener.accept()
            print(f"handheld connected: {address}")
            try:
                with connection:
                    deadline = time.perf_counter()
                    while True:
                        payload = frames[sequence % len(frames)]
                        timestamp_ms = time.time_ns() // 1_000_000
                        header = HEADER.pack(
                            MAGIC, VERSION, flag, sequence,
                            timestamp_ms, len(payload)
                        )
                        connection.sendall(header)
                        connection.sendall(payload)
                        sequence = (sequence + 1) & 0xFFFFFFFF
                        deadline += period
                        time.sleep(max(0.0, deadline - time.perf_counter()))
            except (BrokenPipeError, ConnectionResetError, TimeoutError) as error:
                print(f"handheld disconnected: {error}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path,
                        help="one image file or a directory of image files")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9102)
    parser.add_argument("--fps", type=float, default=10.0)
    parser.add_argument("--encoding", choices=("jpeg", "rgb332-zlib"),
                        default="jpeg")
    parser.add_argument("--quality", type=int, choices=range(1, 96), default=70)
    parser.add_argument("--level", type=int, choices=range(0, 10), default=1)
    args = parser.parse_args()
    flag, frames = load_frames(
        args.source, args.encoding, args.level, args.quality
    )
    serve(args.host, args.port, args.fps, flag, frames)


if __name__ == "__main__":
    main()
