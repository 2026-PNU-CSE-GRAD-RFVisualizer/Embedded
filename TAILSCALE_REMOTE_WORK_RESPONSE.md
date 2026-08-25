# Tailscale 원격 통합 구성 — Embedded 파트 회신

작성: Embedded 파트  
작성일: 2026-08-25  
대상: Network/Backend 파트

## 1. 결론

Network/Backend 파트의 판단과 같이, **Tailscale이 설치된 PC끼리의 통신은 바로 가능하지만 현재 ESP32-S3 Firmware가 Tailscale Tailnet에 직접 참여하는 구조는 아니다.**

따라서 통신 경로를 다음처럼 구분하는 것이 맞다.

| 경로 | 실제 송신/연결 주체 | 원격 구성 판단 |
|---|---|---|
| RSSI MQTT | Embedded 측 PC의 Python Bridge가 Broker에 접속 | Tailscale로 원격 연결 가능 |
| Handheld Control | ESP32-S3가 Backend UDP `9200`으로 송신 | 서로 다른 LAN이면 중계 또는 라우팅 필요 |
| LCD Image | ESP32-S3가 Relay TCP `9102`에 접속해 Frame 수신 | 서로 다른 LAN이면 중계 또는 라우팅 필요 |
| Graphics | Graphics PC가 Relay TCP `9101`과 Backend WebSocket에 접속 | Tailscale로 원격 연결 가능 |

Handheld Control은 **ESP32-S3에서 보내는 것이 맞다.** 다만 수신 서버인 Backend가 Network 파트 노트북에서 실행되므로, ESP32-S3가 그 노트북의 Backend에 도달할 수 있는 네트워크 경로가 필요하다.

LCD 영상은 반대 방향이다. Graphics가 생성한 Frame을 Network의 `image_relay`가 중계하고, ESP32-S3는 Relay의 viewer port `9102`에 TCP client로 연결하여 Frame을 받는다.

## 2. 경로별 Embedded 답변

### 2.1 RSSI와 MQTT Bridge

Python Serial-MQTT Bridge는 Embedded 측 PC에서 실행한다.

```text
ESP32 Nodes
  └─ESP-NOW─> ESP32 Gateway
                └─UART─> STM32
                           └─Serial─> Embedded PC Python Bridge
                                        └─MQTT/Tailscale─> Remote Broker
```

Bridge는 실행 인자 `--server`와 `--port`로 Broker 주소를 지정할 수 있다.

```powershell
python stm32_serial_mqtt_bridge.py `
  --serial-port COM4 `
  --server <Network 노트북의 Tailscale IP 또는 MagicDNS 이름> `
  --port 1883
```

따라서 RSSI 경로는 다음 조건을 만족하면 현재 구조를 변경하지 않고 원격 시험할 수 있다.

- Embedded Bridge PC와 Network Broker PC에 Tailscale 설치
- Broker가 Tailscale Interface에서도 TCP `1883` 연결을 수신
- Windows Firewall과 Tailnet ACL에서 TCP `1883` 허용
- 실제 계정과 비밀번호가 필요하면 Bridge 설정을 별도로 합의

ESP32 Gateway와 STM32에는 Broker IP를 설정할 필요가 없다. 이 장치들은 각각 ESP-NOW와 UART/Serial까지만 담당한다.

### 2.2 Handheld Control

합의된 방향은 다음과 같다.

```text
ESP32-S3 ──UDP 9200──> Backend ──WebSocket──> Graphics
```

ESP32-S3가 BNO085 Quaternion과 버튼 Event를 RFHC v1 Packet으로 송신한다. 즉 Handheld Control의 송신 주체는 Embedded의 ESP32-S3이다.

현재 상태는 다음과 같다.

- RFHC v1 52-byte Serializer 구현 및 Backend 공유 Test Vector 일치
- Backend UDP `9200` Listener 구현 회신 확인
- ESP32-S3의 실제 `ControlTxTask`와 UDP Socket 송신은 아직 미구현
- 실제 ESP32-S3 ↔ Backend 실물 UDP 통합 시험은 미완료

`ControlTxTask`를 구현할 때 Backend IPv4/Hostname과 Port를 로컬 설정으로 분리할 예정이다. 특정 장소의 IP나 Tailscale IP를 Firmware에 최종값처럼 고정하지 않는다.

### 2.3 LCD Image

합의된 방향은 다음과 같다.

```text
Graphics ──TCP 9101──> image_relay ──TCP 9102──> ESP32-S3 ──> LCD
```

여기서 ESP32-S3는 TCP server가 아니라 **TCP client**이다. ESP32-S3가 Relay의 viewer port `9102`에 연결한 뒤, Relay가 해당 연결로 JPEG Frame을 전달한다.

현재 Firmware는 다음 항목을 `idf.py menuconfig`에서 변경할 수 있다.

- Wi-Fi SSID와 Password
- Image Relay IPv4 또는 Hostname
- Image Relay viewer port, 기본값 `9102`

따라서 같은 LAN에서는 ESP32-S3의 Relay 주소를 Network/Relay PC의 LAN IP로 설정하면 된다. 서로 다른 LAN에서는 ESP32-S3가 Network 노트북의 Tailscale `100.x` 주소로 바로 연결된다고 가정해서는 안 된다.

## 3. ESP32-S3와 Tailscale 사이의 제약

Tailscale은 설치된 장치에 Tailnet 주소와 경로를 제공한다. 현재 ESP32-S3에는 Tailscale Client가 없으므로 Network 노트북의 `100.x` 주소가 ESP32-S3에서 자동으로 접근 가능한 일반 LAN 주소가 아니다.

Tailscale 공식 문서의 subnet router는 Tailscale을 설치할 수 없는 장치를 Tailnet과 연결하는 방법을 제공한다. 다만 우리 경로처럼 **LAN의 ESP32-S3가 먼저 Tailnet의 Backend/Relay로 연결**하려면 다음 요소를 함께 검토해야 한다.

- ESP32-S3와 같은 LAN에 항상 켜져 있는 router PC 또는 Linux 장치
- IP forwarding
- `100.64.0.0/10` 목적지에 대한 LAN/DHCP 정적 경로 또는 적절한 NAT/Proxy
- Tailnet route 승인과 ACL
- Windows/Linux Firewall의 UDP `9200`, TCP `9102` 허용
- 재부팅 후 경로 유지와 장애 복구 확인

따라서 subnet router는 가능한 후보이지만, 설치만 하면 자동으로 양방향 통신이 되는 것으로 간주하지 않는다.

공식 참고 문서:

- [Tailscale Subnet Routers](https://tailscale.com/docs/features/subnet-routers)
- [Connect to devices](https://tailscale.com/kb/1452/connect-to-devices)

## 4. 권장 시험 구성

### 구성 A — 우선 권장: 역할별 원격/로컬 분리

```text
RSSI:
Embedded Hardware ──> Embedded Bridge PC ──Tailscale─> Network MQTT Broker

Handheld/LCD:
ESP32-S3 ──Local Wi-Fi─> 같은 장소의 Backend/Image Relay PC

Graphics:
Graphics PC ──Tailscale 또는 Local Network─> Backend/Image Relay
```

장점:

- 기존 Firmware와 Protocol을 거의 변경하지 않음
- 라우팅 문제와 실물 기능 문제를 분리해서 진단 가능
- RSSI 원격 협업을 먼저 시작할 수 있음

단점:

- Handheld/LCD 종단 시험 시 Backend와 Relay를 하드웨어 측 PC에서도 실행할 수 있어야 함
- Network 노트북 한 대만을 항상 중앙 허브로 사용하는 구성은 아님

### 구성 B — 원격 중앙 허브 + 로컬 Application Proxy

ESP32-S3와 같은 LAN의 Embedded PC에 작은 UDP/TCP 중계기를 둔다.

```text
Handheld Control:
ESP32-S3 ──UDP 9200─> Embedded PC Proxy
                          └─Tailscale─> Network Backend UDP 9200

LCD Image:
ESP32-S3 ──TCP─> Embedded PC Proxy
                    └─Tailscale─> Network image_relay TCP 9102
```

ESP32-S3에는 Embedded PC의 LAN IP만 설정하고, PC Proxy가 Network 노트북의 Tailscale IP로 연결한다.

장점:

- 복잡한 LAN 전체 라우팅보다 적용 범위가 작음
- Network 노트북을 중앙 허브로 유지 가능
- 필요한 Port만 명시적으로 중계 가능

검증 필요 항목:

- UDP Packet 전달 시 Source IP에 의존하지 않는지 확인
- TCP Proxy가 RFJF Frame 경계를 변경하지 않는지 확인
- Proxy 재연결, Timeout 및 로그 계측
- 지연, Frame Drop 및 300초 연속 시험

### 구성 C — Subnet Routing

하드웨어 측 PC 또는 Linux 장치를 정식 router로 구성해 ESP32-S3의 Tailnet 경로를 제공한다. 장기적으로 깔끔할 수 있지만 라우팅, NAT, ACL과 방화벽 검증이 필요하므로 초기 통합의 첫 선택으로 두지 않는다.

## 5. Network/Backend 파트에 요청할 값

RSSI 원격 시험 전에 다음 값을 공유해 주면 된다.

- MQTT Broker의 Tailscale IP 또는 MagicDNS 이름
- MQTT Port와 인증 사용 여부
- TCP `1883` Firewall/ACL 허용 여부

Handheld 원격 시험 전에는 다음 값이 필요하다.

- Backend UDP Listener의 실제 주소와 Port `9200`
- `handheld_enabled=true` 설정 방법
- 허용 `device_id=1` 설정 방법
- Image Relay의 실제 주소와 viewer port `9102`
- Application Proxy와 subnet router 중 어느 방식을 우선 시험할지

Embedded 파트에서는 다음을 확인해 공유한다.

- 센서, Gateway, STM32, ESP32-S3, LCD의 실물 보유 위치
- 실물과 같은 LAN에서 계속 실행할 수 있는 PC 유무
- 해당 PC에 Tailscale과 Proxy 또는 routing 설정이 가능한지

## 6. 제안하는 진행 순서

1. 두 PC 사이에서 Tailscale IP로 MQTT TCP `1883` 연결을 확인한다.
2. STM32 Serial-MQTT Bridge의 `--server`를 Tailscale 주소로 지정해 RSSI Publish를 확인한다.
3. Handheld/LCD는 우선 같은 LAN에서 Backend와 Relay를 실행해 Firmware 기능을 검증한다.
4. Handheld `ControlTxTask` 구현 시 Backend 주소를 설정값으로 추가한다.
5. 원격 종단 시험이 필요하면 Application Proxy를 먼저 시험한다.
6. 장기 운영 필요성이 확인되면 subnet routing을 별도 네트워크 작업으로 검증한다.

## 7. 변경 및 검증 범위

- 이번 회신은 원격 통합 구성에 대한 정리이며 Packet, UART, JSON, MQTT Topic/Payload, RFHC 및 RFJF Wire 규격을 변경하지 않는다.
- MQTT Bridge의 Broker 주소 변경 기능과 JPEG Relay 주소 설정 기능은 현재 구현에 존재한다.
- Handheld UDP 송신은 Protocol Serializer까지만 구현됐으며 실제 `ControlTxTask`와 실물 원격 시험은 완료로 표시하지 않는다.
- subnet router와 Application Proxy는 아직 실물 환경에서 검증하지 않았다.
