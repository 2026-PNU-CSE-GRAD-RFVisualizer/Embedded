# AGENTS.md

이 문서는 `Embedded` 저장소 전체에 적용된다.

## 1. 기본 원칙

작업 시작 시 `RFVisualizer-Docs` 전체를 읽지 않는다.

기본적으로 다음만 확인한다.

1. 이 `AGENTS.md`
2. 사용자가 지정한 파일
3. 수정 대상 Firmware, Bridge, 설정, 테스트
4. 필요할 경우 해당 디렉터리의 Protocol 또는 Wiring 문서 관련 절

중앙 문서 저장소:

- GitHub: https://github.com/2026-PNU-CSE-GRAD-RFVisualizer/RFVisualizer-Docs
- 권장 로컬 위치: `../RFVisualizer-Docs`

## 2. 중앙 문서 선택 규칙

| 작업 상황 | 읽을 문서 | 범위 |
|---|---|---|
| 프로젝트 목표나 임베디드 책임 판단 | `PROJECT.md` | 목표, 파트 책임, 설계 원칙 |
| 현재 구현·실물 검증 상태 판단 | `CURRENT_STATUS.md` | 전체 요약과 임베디드 절 |
| Packet·UART·JSON·MQTT·Node ID·RSSI·좌표 변경 | `INTERFACE.md` | 관련 인터페이스 절과 변경 절차 |
| 임베디드 전체 구조나 Handheld 설계 파악 | `embedded/EMBEDDED.md` | 관련 기능 절만 |
| 내부 버그 수정·리팩터링·테스트 보강 | 중앙 문서 불필요 | 대상 코드와 로컬 문서만 확인 |

다음 작업에서는 `INTERFACE.md` 확인이 필수다.

- ESP-NOW Packet 구조 또는 Version 변경
- Gateway → STM32 UART Line 변경
- STM32 JSON Schema 변경
- MQTT Topic 또는 Payload 변경
- Node ID 매핑 변경
- `rssi`, `rssi_raw`, x10 Scale 의미 변경
- Timestamp 또는 좌표 형식 변경
- Handheld Control Packet 또는 JPEG Protocol 정의
- Backend와 Graphics도 영향을 받는 변경

문서는 관련 제목과 주변 절만 읽는다. 과거 계획서나 중간보고서는 현재 규격 확인에 사용하지 않는다.

## 3. 판단 우선순위

1. 현재 동작 코드와 통과한 테스트
2. 공통 계약이 관련되면 `INTERFACE.md`
3. 현재 상태가 관련되면 `CURRENT_STATUS.md`
4. 임베디드 설계가 관련되면 `embedded/EMBEDDED.md`
5. 이 저장소의 Protocol, Wiring, Test 문서
6. 과거 계획서·보고서·AI 작업 지시서

코드와 중앙 문서가 충돌하면 구현 결함인지 문서 미갱신인지 확인한다.

## 4. 현재 기준 파이프라인

```text
ESP32 원격 RSSI Node 1~4
        │ ESP-NOW
        ▼
ESP32 Gateway + Local RSSI Node 5
        │ UART 115200 bps
        ▼
STM32F107VCT6
        │ JSON over Serial
        ▼
Python Serial-MQTT Bridge
        │ MQTT
        ▼
Network Backend
```

각 ESP32 Node가 MQTT Broker에 직접 접속하는 구조로 임의 변경하지 않는다.

## 5. 임베디드 파트 경계

이 저장소의 책임:

- ESP32 RSSI Node
- BSSID/Channel 기반 RSSI 측정
- Raw/Filtered RSSI
- ESP-NOW Packet과 CRC32
- ESP32 Gateway와 Local Node 5
- UART Line Protocol
- STM32 Parser, Ring Buffer, Timeout
- MQTT-ready JSON
- Python Serial-MQTT Bridge
- 향후 IMU, 버튼, JPEG 수신, LCD 출력

Backend Experiment DB와 Graphics RF Solver를 중복 구현하지 않는다.

## 6. 유지할 인터페이스 의미

### Node ID

| 내부 ID | 외부 ID |
|---:|---|
| `1` | `node-01` |
| `2` | `node-02` |
| `3` | `node-03` |
| `4` | `node-04` |
| `5` | `gw-01` |

### RSSI

```text
rssi              = Filtered RSSI, dBm
rssi_raw          = Raw RSSI, dBm
rssi_filtered_x10 = Filtered RSSI × 10
status/error_flags = 0이면 정상
```

```text
-60.8 dBm → -608
```

Raw와 Filtered를 뒤바꾸거나 x10 값을 dBm으로 발행하지 않는다.

### UART

```text
Baudrate: 115200
Logic Level: 3.3 V
```

```text
$RSSI,<node_id>,<seq>,<uptime_ms>,<rssi_raw>,<rssi_filtered_x10>,<sample_count>,<error_flags>*<checksum>
```

Checksum은 `$` 다음부터 `*` 직전까지 XOR한다.

### MQTT

```text
rssi/<node_id>
gateway/<gateway_id>
status/<gateway_id>/lwt
```

새 Payload는 `rssi`, `rssi_raw`, `status`를 사용한다.

## 7. 설정과 좌표

- 좌표 단위는 meter다.
- `+Z`는 위쪽이다.
- Placeholder `(0,0,0)`을 실제 배치로 사용하지 않는다.
- 예시 설정과 실제 로컬 설정을 분리한다.
- SSID, BSSID, Channel, Gateway MAC, COM Port, Broker 주소를 코드에 최종값처럼 고정하지 않는다.
- Secret과 로컬 장치 설정을 Commit하지 않는다.
- Bridge 위치와 Backend Node Assignment가 충돌하지 않게 한다.

## 8. 상태 확인이 필요한 기능

다음 작업을 수행하거나 설명할 때만 `CURRENT_STATUS.md`의 임베디드 절을 읽는다.

- 다중 실물 Node 안정성
- BSSID/Channel 고정 검증
- Device Offset
- ESP32-S3 Handheld
- IMU Quaternion
- Position Update 버튼
- JPEG 수신
- NT35510 LCD

Build 성공, 기본 통신 성공, 장시간 실물 검증 완료를 구분한다.

## 9. 코드 변경 규칙

- Interrupt Callback에서는 최소한의 작업만 한다.
- UART는 Ring Buffer와 Main Loop/Task에서 파싱한다.
- Packet과 Line은 길이, Version, CRC/Checksum을 검증한다.
- MCU 경로에서는 정적 Buffer를 우선한다.
- Overflow, Queue Full, Timeout, Reconnect를 계측한다.
- Sequence Loss와 중복 정보를 유지한다.
- Fake RSSI는 통신 시험에만 사용한다.
- Buffer 크기와 최대 Line/Frame 크기를 명시한다.
- Hardware 변경 시 Flash, RAM, PSRAM, Logic Level을 확인한다.
- Handheld 영상은 오래된 Frame보다 최신 Frame을 우선한다.

## 10. 검증

변경 영역에 해당하는 검사만 수행한다.

### ESP32 Node

- Build
- BSSID 탐색
- Raw/Filtered RSSI
- x10 변환
- Sequence
- CRC32
- Error Flag

### Gateway

- Node 구분
- CRC/Version 거부
- 중복·누락·Queue Drop
- Local Node 5
- UART Line과 Checksum

### STM32

- Host Parser Test
- 부분/연속 Line
- 잘못된 Checksum
- Ring Buffer Overflow
- Timeout과 Sequence Loss
- JSON 최대 길이

### Python Bridge

- 이전/현재 JSON Schema
- Node ID 매핑
- x10 변환
- MQTT QoS 1
- Reconnect, Retry, LWT
- 중복 Sequence 방지

### 실물 통합

- ESP32 3대 이상
- 고정 BSSID/Channel
- 장시간 수집
- Node/AP/Gateway/UART/Broker 장애 복구
- 누락률과 복구 시간

실행하지 못한 실물 시험은 완료로 표시하지 않는다.

## 11. 중앙 문서 갱신 조건

| 변경 | 갱신 문서 |
|---|---|
| Packet, UART, JSON, MQTT 등 공통 계약 | `INTERFACE.md` |
| 구현 또는 실물 검증 상태 | `CURRENT_STATUS.md` |
| 임베디드 구조·Hardware·Task | `embedded/EMBEDDED.md` |
| 프로젝트 목표·파트 책임 | `PROJECT.md` |

단순 내부 버그 수정이나 동작이 변하지 않는 리팩터링은 중앙 문서를 수정하지 않는다.

## 12. 결과 보고

관련된 항목만 보고한다.

- 대상 Board/Firmware
- 변경 파일
- Protocol/Schema 변경 여부
- Build와 Host Test
- 실물 Test 구성과 결과
- Packet Loss, Timeout, Queue Drop
- 미검증 항목
- 중앙 문서 변경 여부
