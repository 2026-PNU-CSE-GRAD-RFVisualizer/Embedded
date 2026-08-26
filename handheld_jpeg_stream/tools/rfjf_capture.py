"""Capture and decode one RFJF v1 frame from an image relay viewer port."""

from __future__ import annotations

import argparse
import socket
import struct
import zlib
from pathlib import Path


HEADER = struct.Struct(">4sBBIQI")
MAGIC = b"RFJF"
VERSION = 1
FLAG_JPEG = 0
FLAG_RGB332_ZLIB = 1
WIDTH = 800
HEIGHT = 480
RGB332_FRAME_BYTES = WIDTH * HEIGHT


def recv_exactly(sock: socket.socket, length: int) -> bytes:
    chunks: list[bytes] = []
    remaining = length
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError(f"stream ended with {remaining} bytes missing")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture one RFJF JPEG or RGB332+zlib frame"
    )
    parser.add_argument("host", help="Image relay Tailscale IP or hostname")
    parser.add_argument("--port", type=int, default=9102)
    parser.add_argument(
        "--output", type=Path,
        help="Output file; defaults to .jpg for flags=0 and .bmp for flags=1",
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--max-bytes", type=int, default=8 * 1024 * 1024)
    return parser.parse_args()


def decode_rgb332_zlib_to_bmp(payload: bytes) -> bytes:
    inflater = zlib.decompressobj()
    rgb332 = inflater.decompress(payload, RGB332_FRAME_BYTES + 1)
    rgb332 += inflater.flush()
    if not inflater.eof or inflater.unused_data:
        raise SystemExit("payload is not one complete zlib stream")
    if len(rgb332) != RGB332_FRAME_BYTES:
        raise SystemExit(
            f"RGB332 decoded length={len(rgb332)}, "
            f"expected={RGB332_FRAME_BYTES}"
        )

    row_bytes = WIDTH * 3
    pixels = bytearray(row_bytes * HEIGHT)
    for output_row, source_row in enumerate(range(HEIGHT - 1, -1, -1)):
        source_offset = source_row * WIDTH
        output_offset = output_row * row_bytes
        for x, value in enumerate(rgb332[source_offset:source_offset + WIDTH]):
            red3 = (value >> 5) & 0x07
            green3 = (value >> 2) & 0x07
            blue2 = value & 0x03
            pixel_offset = output_offset + x * 3
            pixels[pixel_offset + 0] = (blue2 * 255) // 3
            pixels[pixel_offset + 1] = (green3 * 255) // 7
            pixels[pixel_offset + 2] = (red3 * 255) // 7

    pixel_offset = 14 + 40
    file_size = pixel_offset + len(pixels)
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    info_header = struct.pack(
        "<IiiHHIIiiII", 40, WIDTH, HEIGHT, 1, 24, 0, len(pixels),
        2835, 2835, 0, 0,
    )
    return file_header + info_header + pixels


def main() -> None:
    args = parse_args()
    with socket.create_connection((args.host, args.port), args.timeout) as sock:
        sock.settimeout(args.timeout)
        raw_header = recv_exactly(sock, HEADER.size)
        magic, version, flags, seq, timestamp_ms, length = HEADER.unpack(raw_header)
        if magic != MAGIC:
            raise SystemExit(f"bad RFJF magic: {magic!r}")
        if version != VERSION:
            raise SystemExit(f"unsupported RFJF version: {version}")
        if flags not in (FLAG_JPEG, FLAG_RGB332_ZLIB):
            raise SystemExit(f"unsupported frame flags={flags}")
        if length <= 0 or length > args.max_bytes:
            raise SystemExit(f"invalid payload length: {length}")

        payload = recv_exactly(sock, length)
        if flags == FLAG_JPEG:
            if not (payload.startswith(b"\xFF\xD8") and
                    payload.endswith(b"\xFF\xD9")):
                raise SystemExit("payload does not have JPEG SOI/EOI markers")
            output = args.output or Path("captured_rfjf_frame.jpg")
            decoded = payload
            encoding = "jpeg"
        else:
            output = args.output or Path("captured_rfjf_frame.bmp")
            decoded = decode_rgb332_zlib_to_bmp(payload)
            encoding = "rgb332-zlib -> BMP"

        output.write_bytes(decoded)
        print(
            f"saved seq={seq}, timestamp_ms={timestamp_ms}, "
            f"flags={flags}, {encoding}, payload={length} B to "
            f"{output.resolve()}"
        )


if __name__ == "__main__":
    main()
