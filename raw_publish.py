import json
import time

import paho.mqtt.client as mqtt

SERVER_IP = "172.20.10.7"   # 주호 노트북(서버)의 Wi-Fi IPv4
PORT = 1883
NODE_ID = "node-03"          # 친구가 흉내낼 노드 이름 (원하는 대로 바꿔도 됨)

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="friend-01")

# 비정상 종료 시 오프라인 알림(LWT)
client.will_set(f"status/{NODE_ID}/lwt",
                json.dumps({"node_id": NODE_ID, "online": False}),
                qos=1, retain=True)

print(f"connecting to {SERVER_IP}:{PORT} ...")
client.connect(SERVER_IP, PORT, keepalive=30)
client.loop_start()
client.publish(f"status/{NODE_ID}/lwt",
               json.dumps({"node_id": NODE_ID, "online": True}),
               qos=1, retain=True)

# 20초 동안 1초에 한 번씩 RSSI 전송
for seq in range(1, 21):
    payload = {
        "node_id": NODE_ID,
        "timestamp": int(time.time() * 1000),
        "ap_bssid": "AA:BB:CC:DD:EE:FF",
        "rssi": -58 - (seq % 5),   # -58 ~ -62 사이로 살짝 변화
        "seq": seq,
        "status": 0,
    }
    client.publish(f"rssi/{NODE_ID}", json.dumps(payload), qos=0)
    print("sent", seq, payload["rssi"], "dBm")
    time.sleep(1)

client.publish(f"status/{NODE_ID}/lwt",
               json.dumps({"node_id": NODE_ID, "online": False}),
               qos=1, retain=True)
client.loop_stop()
client.disconnect()
print("done")