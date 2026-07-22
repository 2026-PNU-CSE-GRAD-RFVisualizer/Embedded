import argparse
import json
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

try:
    import serial
except ImportError:
    print("pyserial is required: python -m pip install pyserial")
    raise


DEFAULT_SERVER_IP = "192.168.0.30"
DEFAULT_PORT = 1883
DEFAULT_SERIAL_PORT = "COM4"
DEFAULT_BAUDRATE = 115200
DEFAULT_GATEWAY_ID = "gw-01"
DEFAULT_PUBLISH_MODE = "individual"
DEFAULT_POSITIONS_FILE = Path(__file__).with_name("node_positions.json")
DEFAULT_POSITION = {"pos_x": 0.0, "pos_y": 0.0, "pos_z": 0.0}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Read STM32 gateway JSON lines from UART and publish them to MQTT."
    )
    parser.add_argument("--server", default=DEFAULT_SERVER_IP)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT)
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument("--gateway-id", default=DEFAULT_GATEWAY_ID)
    parser.add_argument("--client-id", default="gw-01")
    parser.add_argument("--topic", default=None)
    parser.add_argument("--positions-file", default=str(DEFAULT_POSITIONS_FILE))
    parser.add_argument(
        "--publish-mode",
        choices=("individual", "gateway", "both"),
        default=DEFAULT_PUBLISH_MODE,
        help="individual publishes rssi/<node_id>; gateway publishes one batch",
    )
    return parser.parse_args()


def now_ms():
    return int(time.time() * 1000)


def node_name(node_id):
    try:
        numeric = int(node_id)
    except (TypeError, ValueError):
        return str(node_id)

    if numeric == 5:
        return "gw-01"

    return f"node-{numeric:02d}"


def filtered_dbm_from_x10(value):
    value = int(value)
    if value >= 0:
        return int((value + 5) / 10)
    return int((value - 5) / 10)


def publish_checked(client, topic, payload, qos=1, retain=False):
    text = json.dumps(payload, separators=(",", ":"))
    info = client.publish(topic, text, qos=qos, retain=retain)
    info.wait_for_publish()
    if info.rc != mqtt.MQTT_ERR_SUCCESS:
        raise RuntimeError(f"publish failed topic={topic} rc={info.rc}")
    return text


def status_payload(gateway_id, online):
    return {
        "node_id": gateway_id,
        "online": online,
        "timestamp": now_ms(),
    }


def load_node_positions(path):
    with open(path, "r", encoding="utf-8") as file:
        raw_positions = json.load(file)

    if not isinstance(raw_positions, dict):
        raise ValueError("positions file must contain a JSON object")

    positions = {}
    for node_id, position in raw_positions.items():
        if not isinstance(position, dict):
            raise ValueError(f"invalid position for {node_id}")

        positions[str(node_id)] = {
            "pos_x": float(position["pos_x"]),
            "pos_y": float(position["pos_y"]),
            "pos_z": float(position["pos_z"]),
        }

    return positions


def attach_position(reading, positions):
    node_id = reading["node_id"]
    position = positions.get(node_id, DEFAULT_POSITION)
    positioned = {
        "node_id": node_id,
        "timestamp": reading["timestamp"],
        "rssi": reading["rssi"],
        "seq": reading["seq"],
        "pos_x": position["pos_x"],
        "pos_y": position["pos_y"],
        "pos_z": position["pos_z"],
    }

    for key in (
        "ap_bssid",
        "rssi_raw",
        "rssi_x10",
        "age_ms",
        "valid_age_ms",
        "valid",
    ):
        if key in reading:
            positioned[key] = reading[key]

    positioned["status"] = reading["status"]
    return positioned, node_id in positions


def normalize_reading(item, batch_timestamp):
    # STM32가 보존한 과거 RSSI라도 valid=false이면 위치 계산용 MQTT로 전달하지 않는다.
    if item.get("valid") is False or item.get("timed_out") is True:
        return None

    node_id = node_name(item["node_id"])
    timestamp = int(item.get("timestamp", item.get("node_ts", batch_timestamp)))
    rssi = int(item["rssi"])
    if not -100 <= rssi <= -10:
        return None

    reading = {
        "node_id": node_id,
        "timestamp": timestamp,
        "rssi": rssi,
        "seq": int(item.get("seq", 0)),
        "status": int(item.get("status", 0)),
    }

    if "rssi_x10" in item:
        reading["rssi_x10"] = int(item["rssi_x10"])
    if "age_ms" in item:
        reading["age_ms"] = int(item["age_ms"])
    if "valid_age_ms" in item:
        reading["valid_age_ms"] = int(item["valid_age_ms"])
    if "valid" in item:
        reading["valid"] = bool(item["valid"])

    if "rssi_raw" in item:
        reading["rssi_raw"] = int(item["rssi_raw"])
    elif "rssi_raw_dbm" in item:
        reading["rssi_raw"] = int(item["rssi_raw_dbm"])

    if item.get("ap_bssid"):
        reading["ap_bssid"] = str(item["ap_bssid"])

    return reading


def normalize_gateway_payload(raw_payload, gateway_id):
    timestamp = now_ms()

    if "readings" in raw_payload:
        readings = raw_payload.get("readings") or []
        normalized_readings = []
        for item in readings:
            if "node_id" not in item or "rssi" not in item:
                continue

            reading = normalize_reading(item, timestamp)
            if reading is not None:
                normalized_readings.append(reading)

        normalized = {
            "gateway_id": raw_payload.get("gateway_id", gateway_id),
            "timestamp": timestamp,
            "readings": normalized_readings,
        }
        for key in (
            "schema_version",
            "active_node_count",
            "rx_count",
            "accepted_count",
            "checksum_errors",
            "format_errors",
            "uart_overflows",
        ):
            if key in raw_payload:
                normalized[key] = raw_payload[key]
        return normalized

    # Compatibility with the older STM32 payload:
    # {"device_id":...,"nodes":[{"node_id":5,"rssi_filtered_x10":-598,...}]}
    if "nodes" in raw_payload:
        readings = []
        for item in raw_payload.get("nodes") or []:
            if "node_id" not in item:
                continue

            if "rssi" in item:
                rssi = int(item["rssi"])
            elif "rssi_filtered_x10" in item:
                rssi = filtered_dbm_from_x10(item["rssi_filtered_x10"])
            else:
                continue

            if not -100 <= rssi <= -10:
                continue

            reading = {
                "node_id": node_name(item["node_id"]),
                "timestamp": timestamp,
                "rssi": rssi,
                "seq": int(item.get("seq", 0)),
                "status": 0,
            }
            if "rssi_raw_dbm" in item:
                reading["rssi_raw"] = int(item["rssi_raw_dbm"])
            readings.append(reading)

        return {
            "gateway_id": gateway_id,
            "timestamp": timestamp,
            "readings": readings,
        }

    raise ValueError("payload has neither readings nor nodes")


def main():
    args = parse_args()
    data_topic = args.topic or f"gateway/{args.gateway_id}"
    status_topic = f"status/{args.gateway_id}/lwt"
    try:
        positions = load_node_positions(args.positions_file)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit(f"positions config error: {exc}") from exc

    warned_missing_positions = set()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=args.client_id)
    client.will_set(
        status_topic,
        json.dumps(status_payload(args.gateway_id, online=False), separators=(",", ":")),
        qos=1,
        retain=True,
    )

    print(f"connecting mqtt {args.server}:{args.port} ...")
    client.connect(args.server, args.port, keepalive=30)
    client.loop_start()
    publish_checked(client, status_topic, status_payload(args.gateway_id, online=True), qos=1, retain=True)

    print(f"opening serial {args.serial_port} @ {args.baudrate} ...")
    with serial.Serial(args.serial_port, args.baudrate, timeout=1) as ser:
        if args.publish_mode in ("gateway", "both"):
            print(f"gateway batch topic: {data_topic}")
        if args.publish_mode in ("individual", "both"):
            print("individual topics: rssi/<node_id>")

        while True:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            if not line.startswith("{"):
                print("skip:", line)
                continue

            try:
                raw_payload = json.loads(line)
                payload = normalize_gateway_payload(raw_payload, args.gateway_id)
            except (json.JSONDecodeError, ValueError, TypeError) as exc:
                print(f"bad payload: {exc}: {line}")
                continue

            if not payload["readings"]:
                print("skip: payload has no valid readings")
                continue

            positioned_readings = []
            for reading in payload["readings"]:
                positioned, position_found = attach_position(reading, positions)
                positioned_readings.append(positioned)
                if not position_found and reading["node_id"] not in warned_missing_positions:
                    warned_missing_positions.add(reading["node_id"])
                    print(f"warning: {reading['node_id']} position missing; using 0,0,0")
            payload["readings"] = positioned_readings

            if args.publish_mode in ("gateway", "both"):
                sent = publish_checked(client, data_topic, payload, qos=1)
                print(f"sent {data_topic}:", sent)

            if args.publish_mode in ("individual", "both"):
                for reading in payload["readings"]:
                    topic = f"rssi/{reading['node_id']}"
                    sent = publish_checked(client, topic, reading, qos=1)
                    print(f"sent {topic}:", sent)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("stopping...")
        sys.exit(0)
