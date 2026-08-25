"""Send RFHC v1 Handheld Control packets to the Network Backend.

This is a PC-side integration aid. It verifies the UDP listener, firewall,
RFHC parser, sequence handling, stale timeout, and Backend-to-Graphics path
before the ESP32-S3 ControlTxTask is available.
"""

from __future__ import annotations

import argparse
import math
import secrets
import socket
import struct
import time
import zlib


MAGIC = b"RFHC"
VERSION = 1
PACKET_SIZE = 52
DEFAULT_PORT = 9200
ORIENTATION_VALID = 0x01
REQUEST_POSITION_UPDATE = 0x02
RECENTER_ORIENTATION = 0x04
EVENT_REPEAT_COUNT = 3

GOLDEN_VECTOR = bytes.fromhex(
    "524648430101003400000001123456780000000100000000"
    "00000000000000000000000000000000000000003F8000000AE927E5"
)


def build_packet(
    *,
    session_id: int,
    sample_seq: int,
    quaternion: tuple[float, float, float, float],
    device_id: int = 1,
    flags: int = ORIENTATION_VALID,
    event_seq: int = 0,
) -> bytes:
    if session_id == 0:
        raise ValueError("session_id must be non-zero")

    x, y, z, w = quaternion
    norm = math.sqrt(x * x + y * y + z * z + w * w)
    if not 0.97 <= norm <= 1.03:
        raise ValueError(f"invalid quaternion norm: {norm:.6f}")

    body = struct.pack(
        ">4sBBHIIIIQffff",
        MAGIC,
        VERSION,
        flags,
        PACKET_SIZE,
        device_id,
        session_id,
        sample_seq & 0xFFFFFFFF,
        event_seq,
        0,  # timestamp_ms: TIME_SYNCED is not set
        x,
        y,
        z,
        w,
    )
    return body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def quaternion_for_mode(mode: str, elapsed: float) -> tuple[float, float, float, float]:
    if mode == "identity":
        return (0.0, 0.0, 0.0, 1.0)

    # Oscillate yaw between approximately -45 and +45 degrees so a consumer
    # can visibly confirm that live orientation updates are flowing.
    angle = (math.pi / 4.0) * math.sin(2.0 * math.pi * elapsed / 4.0)
    return (0.0, 0.0, math.sin(angle / 2.0), math.cos(angle / 2.0))


def self_test() -> None:
    packet = build_packet(
        session_id=0x12345678,
        sample_seq=1,
        quaternion=(0.0, 0.0, 0.0, 1.0),
    )
    if packet != GOLDEN_VECTOR:
        raise AssertionError(
            f"RFHC golden vector mismatch\nexpected={GOLDEN_VECTOR.hex()}\n"
            f"actual  ={packet.hex()}"
        )
    print("RFHC v1 self-test passed: 52-byte Backend golden vector matched")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send RFHC v1 packets to the Handheld Backend UDP listener."
    )
    parser.add_argument("host", nargs="?", help="Backend LAN IPv4 or hostname")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--duration", type=float, default=10.0, help="seconds")
    parser.add_argument("--rate", type=float, default=50.0, help="packets/second")
    parser.add_argument("--mode", choices=("identity", "yaw"), default="yaw")
    parser.add_argument(
        "--event",
        choices=("none", "position", "recenter"),
        default="none",
        help="repeat one event flag in the first three packets",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.self_test:
        self_test()
        return
    if not args.host:
        raise SystemExit("host is required unless --self-test is used")
    if args.duration <= 0 or args.rate <= 0:
        raise SystemExit("--duration and --rate must be positive")

    session_id = secrets.randbits(32) or 1
    interval = 1.0 / args.rate
    start = time.monotonic()
    deadline = start + args.duration
    next_send = start
    sample_seq = 1
    sent = 0
    event_flag = {
        "none": 0,
        "position": REQUEST_POSITION_UPDATE,
        "recenter": RECENTER_ORIENTATION,
    }[args.event]

    print(
        f"sending RFHC v1 to {args.host}:{args.port}, rate={args.rate:g} Hz, "
        f"duration={args.duration:g} s, mode={args.mode}, "
        f"event={args.event}, session_id=0x{session_id:08X}"
    )

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        while True:
            now = time.monotonic()
            if now >= deadline:
                break
            if now < next_send:
                time.sleep(next_send - now)
                now = time.monotonic()

            packet = build_packet(
                session_id=session_id,
                sample_seq=sample_seq,
                quaternion=quaternion_for_mode(args.mode, now - start),
                flags=(
                    ORIENTATION_VALID | event_flag
                    if event_flag and sample_seq <= EVENT_REPEAT_COUNT
                    else ORIENTATION_VALID
                ),
                event_seq=1 if event_flag else 0,
            )
            udp.sendto(packet, (args.host, args.port))
            sent += 1
            sample_seq = (sample_seq + 1) & 0xFFFFFFFF
            next_send += interval

    elapsed = time.monotonic() - start
    print(
        f"done: sent={sent}, elapsed={elapsed:.3f} s, "
        f"average_rate={sent / elapsed:.2f} Hz"
    )
    print("Backend should mark the Handheld stale about 500 ms after this stops.")


if __name__ == "__main__":
    main()
