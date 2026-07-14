# Embedded

3D Gaussian Splatting 기반 공간 데이터 시각화 졸업과제의 임베디드 파트 저장소입니다.

## 현재 구현 범위

```text
ESP32 RSSI Node 1..4
        │ ESP-NOW
        ▼
ESP32 Gateway + Local Node 5
        │ UART 115200 bps
        ▼
STM32F107VCT6
        │ JSON over Serial
        ▼
Python Serial-MQTT Bridge
        │ MQTT
        ▼
Server / Backend / Dashboard
```

- ESP32 대상 AP RSSI 측정 및 Moving Average 필터
- ESP-NOW 기반 다중 노드 데이터 전달
- CRC32 패킷 검증과 노드별 Sequence 누락 집계
- ESP32 Gateway에서 STM32로 UART Line Protocol 전송
- STM32 UART 인터럽트 수신, Checksum 검증 및 노드 테이블 관리
- MQTT-ready JSON Snapshot 생성
- Python Serial-MQTT Bridge를 통한 서버 연결 및 Publish
- 노드 좌표 설정 결합

## 디렉터리 구성

| 경로 | 내용 |
|---|---|
| `rssi_esp32_to_stm32/esp32_node/` | ESP32 원격 RSSI 노드 펌웨어 |
| `rssi_esp32_to_stm32/esp32_gateway/` | ESP-NOW 수신 및 UART 게이트웨이 펌웨어 |
| `rssi_esp32_to_stm32/stm32_receiver/` | 이식 가능한 STM32 파서와 호스트 테스트 |
| `rssi_esp32_to_stm32/docs/` | 통신 프로토콜, 배선 및 시험 계획 |
| `stm32_final_term/` | STM32F107VCT6 실기기 펌웨어 프로젝트 |
| `stm32_serial_mqtt_bridge.py` | STM32 Serial JSON → MQTT 브리지 |
| `node_positions.json` | 노드별 설치 좌표 설정 |
| `임베디드_파트_중간보고서_2026-07-14.md` | 현재 진척도와 중간 결과 |

## 현재 상태

- STM32 및 ESP32 Node/Gateway 빌드 성공
- STM32 Parser Host Test 통과
- STM32 J-Link Flash 및 실행 성공 이력 확보
- MQTT Broker 및 서버 연결 정상
- 다음 작업: 3개 이상 실물 노드 동시 시험, 장시간 안정성 시험, Dashboard·Graphics 최종 통합

## 주요 설정

ESP32 노드 설정:

```text
rssi_esp32_to_stm32/esp32_node/main/node_config.h
```

ESP32 게이트웨이 설정:

```text
rssi_esp32_to_stm32/esp32_gateway/main/gateway_config.h
```

MQTT 브리지 실행 예시:

```powershell
python stm32_serial_mqtt_bridge.py --serial-port COM4 --server <MQTT_SERVER_IP>
```

세부 구현 계획과 시험 결과는 저장소의 Markdown 문서를 참고합니다.
