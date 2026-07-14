import json
import time

import paho.mqtt.client as mqtt

SERVER_IP = "172.20.10.7"   # 친구 서버의 Wi-Fi IPv4
PORT = 1883
NODE_ID = "node-02"          # 서버에 등록된 노드 이름

MEASUREMENT_TOPIC = f"rssi/{NODE_ID}"
STATUS_TOPIC = f"status/{NODE_ID}/lwt"

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="friend-01")
started_at = time.monotonic()


def now_ms():
    return int(time.time() * 1000)


def uptime_ms():
    return int((time.monotonic() - started_at) * 1000)


def publish_checked(topic, payload, qos=1, retain=False):
    info = client.publish(topic, json.dumps(payload), qos=qos, retain=retain)
    info.wait_for_publish()
    if info.rc != mqtt.MQTT_ERR_SUCCESS:
        raise RuntimeError(f"publish failed topic={topic} rc={info.rc}")


def status_payload(error_flags=0):
    return {
        "node_id": NODE_ID,
        "online": error_flags == 0,
    }


client.will_set(STATUS_TOPIC, json.dumps(status_payload(error_flags=1)), qos=1, retain=False)

print(f"connecting to {SERVER_IP}:{PORT} ...")
client.connect(SERVER_IP, PORT, keepalive=30)
client.loop_start()
publish_checked(STATUS_TOPIC, status_payload(), qos=1, retain=True)

for seq in range(1, 21):
    rssi = -58 - (seq % 5)
    payload = {
        "node_id": NODE_ID,
        "timestamp": now_ms(),
        "rssi": rssi,
        "seq": seq,
        "ap_bssid": "AA:BB:CC:DD:EE:FF",
        "rssi_raw": rssi,
        "status": 0,
    }
    publish_checked(MEASUREMENT_TOPIC, payload, qos=1)
    if seq % 5 == 0:
        publish_checked(STATUS_TOPIC, status_payload(), qos=1, retain=True)
    print("sent", seq, payload["rssi"], "dBm")
    time.sleep(1)

client.loop_stop()
client.disconnect()
print("done")
