"""Forward RFHC v1 UDP packets from the local Wi-Fi to a Tailscale peer."""

from __future__ import annotations

import argparse
import socket
import time


PACKET_SIZE = 52
MAGIC = b"RFHC"
DEFAULT_PORT = 9200


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Listen for ESP32-S3 RFHC packets on the local LAN and forward "
            "them unchanged to a Backend over Tailscale."
        )
    )
    parser.add_argument("backend", help="Backend Tailscale IPv4 or MagicDNS name")
    parser.add_argument("--backend-port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--listen-port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--stats-interval", type=float, default=5.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.stats_interval <= 0:
        raise SystemExit("--stats-interval must be positive")

    destination = (args.backend, args.backend_port)
    received = 0
    forwarded = 0
    invalid = 0
    send_errors = 0
    last_stats = time.monotonic()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener, socket.socket(
        socket.AF_INET, socket.SOCK_DGRAM
    ) as sender:
        listener.bind((args.listen_host, args.listen_port))
        listener.settimeout(1.0)
        print(
            f"RFHC proxy listening on {args.listen_host}:{args.listen_port}; "
            f"forwarding to {args.backend}:{args.backend_port}"
        )
        print("Press Ctrl+C to stop.")

        try:
            while True:
                try:
                    packet, source = listener.recvfrom(65535)
                except socket.timeout:
                    packet = None

                if packet is not None:
                    received += 1
                    if len(packet) != PACKET_SIZE or packet[:4] != MAGIC:
                        invalid += 1
                    else:
                        try:
                            sender.sendto(packet, destination)
                            forwarded += 1
                        except OSError as exc:
                            send_errors += 1
                            print(f"forward error from {source[0]}:{source[1]}: {exc}")

                now = time.monotonic()
                if now - last_stats >= args.stats_interval:
                    print(
                        f"stats: received={received}, forwarded={forwarded}, "
                        f"invalid={invalid}, send_errors={send_errors}"
                    )
                    last_stats = now
        except KeyboardInterrupt:
            print(
                f"stopped: received={received}, forwarded={forwarded}, "
                f"invalid={invalid}, send_errors={send_errors}"
            )


if __name__ == "__main__":
    main()
