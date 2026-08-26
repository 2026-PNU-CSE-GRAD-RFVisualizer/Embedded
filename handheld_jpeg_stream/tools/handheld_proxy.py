"""Bridge Handheld UDP control and TCP JPEG traffic through Tailscale.

Local ESP32-S3 connections:
  UDP <local>:9200 -> <hub>:9200
  TCP <local>:9102 <-> <hub>:9102
"""

from __future__ import annotations

import argparse
import socket
import threading
import time


RFHC_MAGIC = b"RFHC"
RFHC_PACKET_SIZE = 52


class Stats:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.udp_received = 0
        self.udp_forwarded = 0
        self.udp_invalid = 0
        self.udp_errors = 0
        self.tcp_connections = 0
        self.tcp_errors = 0
        self.jpeg_bytes_to_esp = 0
        self.esp_bytes_to_hub = 0

    def add(self, name: str, amount: int = 1) -> None:
        with self.lock:
            setattr(self, name, getattr(self, name) + amount)

    def line(self) -> str:
        with self.lock:
            return (
                "stats: "
                f"udp_rx={self.udp_received} "
                f"udp_fwd={self.udp_forwarded} "
                f"udp_invalid={self.udp_invalid} "
                f"udp_errors={self.udp_errors} "
                f"tcp_connections={self.tcp_connections} "
                f"tcp_errors={self.tcp_errors} "
                f"jpeg_to_esp={self.jpeg_bytes_to_esp}B "
                f"esp_to_hub={self.esp_bytes_to_hub}B"
            )


def udp_forward_loop(args: argparse.Namespace, stop: threading.Event, stats: Stats) -> None:
    destination = (args.hub, args.hub_udp_port)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener, socket.socket(
        socket.AF_INET, socket.SOCK_DGRAM
    ) as sender:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.listen_host, args.local_udp_port))
        listener.settimeout(1.0)
        print(
            f"RFHC UDP {args.listen_host}:{args.local_udp_port} "
            f"-> {args.hub}:{args.hub_udp_port}"
        )
        while not stop.is_set():
            try:
                packet, _source = listener.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError as exc:
                if not stop.is_set():
                    stats.add("udp_errors")
                    print(f"UDP receive error: {exc}")
                continue

            stats.add("udp_received")
            if len(packet) != RFHC_PACKET_SIZE or packet[:4] != RFHC_MAGIC:
                stats.add("udp_invalid")
                continue
            try:
                sender.sendto(packet, destination)
                stats.add("udp_forwarded")
            except OSError as exc:
                stats.add("udp_errors")
                print(f"UDP forward error: {exc}")


def pump(
    source: socket.socket,
    destination: socket.socket,
    stop_connection: threading.Event,
    stats: Stats,
    counter: str,
) -> None:
    try:
        while not stop_connection.is_set():
            data = source.recv(64 * 1024)
            if not data:
                break
            destination.sendall(data)
            stats.add(counter, len(data))
    except OSError:
        if not stop_connection.is_set():
            stats.add("tcp_errors")
    finally:
        stop_connection.set()
        for connection in (source, destination):
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def handle_viewer(
    viewer: socket.socket,
    viewer_address: tuple[str, int],
    args: argparse.Namespace,
    stats: Stats,
) -> None:
    try:
        hub = socket.create_connection(
            (args.hub, args.hub_viewer_port), timeout=args.connect_timeout
        )
    except OSError as exc:
        stats.add("tcp_errors")
        print(f"JPEG hub connect failed for viewer {viewer_address}: {exc}")
        viewer.close()
        return

    stats.add("tcp_connections")
    print(
        f"JPEG viewer {viewer_address[0]}:{viewer_address[1]} connected "
        f"to {args.hub}:{args.hub_viewer_port}"
    )
    viewer.settimeout(None)
    hub.settimeout(None)
    stop_connection = threading.Event()
    downstream = threading.Thread(
        target=pump,
        args=(hub, viewer, stop_connection, stats, "jpeg_bytes_to_esp"),
        daemon=True,
    )
    upstream = threading.Thread(
        target=pump,
        args=(viewer, hub, stop_connection, stats, "esp_bytes_to_hub"),
        daemon=True,
    )
    downstream.start()
    upstream.start()
    stop_connection.wait()
    viewer.close()
    hub.close()
    downstream.join(timeout=1.0)
    upstream.join(timeout=1.0)
    print("JPEG viewer disconnected")


def tcp_viewer_loop(args: argparse.Namespace, stop: threading.Event, stats: Stats) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.listen_host, args.local_viewer_port))
        listener.listen(2)
        listener.settimeout(1.0)
        print(
            f"JPEG TCP {args.listen_host}:{args.local_viewer_port} "
            f"<-> {args.hub}:{args.hub_viewer_port}"
        )
        while not stop.is_set():
            try:
                viewer, address = listener.accept()
            except socket.timeout:
                continue
            except OSError as exc:
                if not stop.is_set():
                    stats.add("tcp_errors")
                    print(f"TCP accept error: {exc}")
                continue
            handle_viewer(viewer, address, args, stats)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Proxy Handheld UDP control and TCP JPEG viewer traffic over Tailscale."
    )
    parser.add_argument("--hub", required=True, help="Network Hub Tailscale IP or MagicDNS")
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--local-udp-port", type=int, default=9200)
    parser.add_argument("--hub-udp-port", type=int, default=9200)
    parser.add_argument("--local-viewer-port", type=int, default=9102)
    parser.add_argument("--hub-viewer-port", type=int, default=9102)
    parser.add_argument("--connect-timeout", type=float, default=5.0)
    parser.add_argument("--stats-interval", type=float, default=5.0)
    parser.add_argument(
        "--disable-control",
        action="store_true",
        help="do not forward UDP 9200; keep only the JPEG TCP path for a frozen-camera test",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.connect_timeout <= 0 or args.stats_interval <= 0:
        raise SystemExit("timeouts and intervals must be positive")

    stop = threading.Event()
    stats = Stats()
    tcp_thread = threading.Thread(
        target=tcp_viewer_loop, args=(args, stop, stats), daemon=True
    )
    threads = [tcp_thread]
    if not args.disable_control:
        udp_thread = threading.Thread(
            target=udp_forward_loop, args=(args, stop, stats), daemon=True
        )
        threads.append(udp_thread)
        udp_thread.start()
    else:
        print("RFHC UDP forwarding disabled; Backend camera should become stale/frozen")
    tcp_thread.start()
    print("Press Ctrl+C to stop the combined Handheld proxy.")

    try:
        while all(thread.is_alive() for thread in threads):
            time.sleep(args.stats_interval)
            print(stats.line())
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        try:
            for thread in threads:
                thread.join(timeout=2.0)
        except KeyboardInterrupt:
            # A second Ctrl+C should shorten shutdown without a traceback.
            pass
        print(stats.line())
        print("Handheld proxy stopped")


if __name__ == "__main__":
    main()
