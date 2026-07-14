# Codex 작업 지시서: ESP32 4~5개 RSSI 측정 → STM32 전달 1차 프로토타입

작성 목적: Codex가 바로 구현을 시작할 수 있도록, **처음 구현해야 할 최소 기능**을 명확히 정의한다.  
대상: 3DGS 기반 공간 데이터 시각화 프로젝트의 임베디드 1단계  
핵심 목표: **여러 ESP32 노드가 Wi-Fi RSSI를 측정하고, 그 결과를 STM32가 받을 수 있게 만드는 것**

---

## 0. 현재 프로젝트 맥락

최종 프로젝트는 실내 공간에서 Wi-Fi RSSI 데이터를 측정하고, 이를 3DGS 기반 공간 시각화에 활용하는 것이다.

최종 핸드헬드 디바이스는 아래 구조를 목표로 한다.

- 보드: `ESP32-S3-DEVKITC-1-N8R8`
- LCD: Waveshare 4inch 480×800, NT35510, 16-bit 8080 Parallel
- IMU: BNO085/BNO080 우선
- 화면 출력: JPEG double buffer + RGB565 line/tile buffer
- 위치 이동: 버튼 기반 Position Snap
- RSSI 자동 위치 추정은 실험 기능

하지만 **이번 Codex 작업은 최종 핸드헬드 디바이스가 아니라, 가장 먼저 필요한 RSSI 계측 노드 통신 프로토타입**이다.

이번 단계에서는 LCD, IMU, JPEG, Viewer 연동은 구현하지 않는다.

---

## 1. 이번 구현의 최종 목표

ESP32 4~5개를 고정 노드로 사용하여 특정 AP의 RSSI를 반복 측정한다.

각 ESP32 노드는 다음 데이터를 생성한다.

- node_id
- sequence number
- uptime timestamp
- target AP BSSID
- raw RSSI
- filtered RSSI
- sample count
- error flags

이 데이터는 최종적으로 STM32가 수신할 수 있어야 한다.

---

## 2. 권장 아키텍처

### 2.1 추천 구조

STM32가 Wi-Fi나 ESP-NOW를 직접 받을 수 없으므로, **ESP32 Gateway를 하나 둔다.**

```text
[ESP32 RSSI Node 1] ┐
[ESP32 RSSI Node 2] ├── ESP-NOW ──→ [ESP32 Gateway] ── UART ──→ [STM32]
[ESP32 RSSI Node 3] ┤
[ESP32 RSSI Node 4] ┤
[ESP32 RSSI Node 5] ┘
```

### 2.2 역할 분리

#### ESP32 RSSI Node

- 특정 AP의 RSSI 측정
- Moving Average 또는 Median Filter 적용
- ESP-NOW로 Gateway에 측정 패킷 전송
- 송신 실패 횟수 기록
- 주기적으로 상태 패킷 전송

#### ESP32 Gateway

- 여러 ESP32 Node의 ESP-NOW 패킷 수신
- node_id별 최신 seq 관리
- 중복 패킷, 누락 패킷 감지
- STM32로 UART line protocol 전송
- Gateway 상태 로그 출력

#### STM32 Receiver

- UART로 Gateway 메시지 수신
- line 단위 파싱
- checksum 검증
- node_id별 최신 RSSI 저장
- 일정 시간 수신이 없는 노드 timeout 처리
- 디버그 UART 또는 SWV로 수신 결과 출력

---

## 3. 이 구조를 선택한 이유

처음부터 ESP32 4~5개를 STM32에 각각 UART로 직접 연결하면 STM32 UART 포트가 부족하거나 배선이 복잡해질 수 있다.

반대로 ESP-NOW Gateway 구조를 쓰면 다음 장점이 있다.

- 고정 노드 배치가 자유롭다.
- STM32는 UART 하나만 처리하면 된다.
- 여러 노드 확장성이 좋다.
- MQTT Broker 없이도 빠르게 1차 프로토타입을 만들 수 있다.
- 나중에 Gateway를 MQTT Publisher로 바꾸기도 쉽다.

---

## 4. 개발 환경

### ESP32 측

- Framework: ESP-IDF
- Language: C
- Target: 일반 ESP32 또는 ESP32-S3 모두 가능
- Transport: ESP-NOW
- Build tool: `idf.py`

### STM32 측

STM32 보드 모델이 아직 확정되지 않았으므로, Codex는 아래 중 하나로 작성한다.

1. **Generic STM32 HAL 기반 코드**
   - `stm32_receiver/` 폴더에 HAL UART 기반 예제 작성
   - 특정 칩 의존성 최소화

2. **Parser-only C module**
   - `rssi_line_parser.c`
   - `rssi_line_parser.h`
   - STM32CubeIDE 프로젝트에 쉽게 붙일 수 있도록 작성

STM32 보드가 확정되기 전까지는 **완전한 CubeMX 프로젝트보다, 이식 가능한 HAL UART + parser module**을 우선한다.

---

## 5. Repository 구조 요구사항

Codex는 아래 구조로 코드를 생성한다.

```text
rssi_esp32_to_stm32/
├── README.md
├── docs/
│   ├── protocol.md
│   ├── hardware_wiring.md
│   └── test_plan.md
├── esp32_node/
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── main/
│       ├── CMakeLists.txt
│       ├── app_main.c
│       ├── node_config.h
│       ├── rssi_measure.c
│       ├── rssi_measure.h
│       ├── rssi_filter.c
│       ├── rssi_filter.h
│       ├── espnow_packet.c
│       └── espnow_packet.h
├── esp32_gateway/
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   └── main/
│       ├── CMakeLists.txt
│       ├── app_main.c
│       ├── gateway_config.h
│       ├── espnow_receiver.c
│       ├── espnow_receiver.h
│       ├── uart_forwarder.c
│       ├── uart_forwarder.h
│       ├── line_protocol.c
│       └── line_protocol.h
└── stm32_receiver/
    ├── README.md
    ├── rssi_line_parser.c
    ├── rssi_line_parser.h
    ├── stm32_uart_receiver_example.c
    └── test_parser_host.c
```

---

## 6. ESP32 Node 상세 요구사항

### 6.1 설정값

`node_config.h`에 다음 값을 둔다.

```c
#define NODE_ID                 1
#define ESPNOW_WIFI_CHANNEL     6
#define TARGET_AP_BSSID         {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}
#define RSSI_SAMPLE_INTERVAL_MS 200
#define RSSI_PUBLISH_PERIOD_MS  1000
#define RSSI_FILTER_WINDOW      5
```

주의:

- ESP-NOW와 Wi-Fi scan은 channel 문제가 생길 수 있다.
- 1차 구현에서는 **측정 대상 AP channel과 ESP-NOW channel을 같게 맞춘다.**
- target AP channel이 다르면 scan 중 ESP-NOW 송신 안정성이 떨어질 수 있으므로, 이 문제를 README에 명시한다.

### 6.2 RSSI 측정 방식

1차 구현에서는 아래 둘 중 가능한 방식을 지원한다.

#### 방식 A: Wi-Fi scan 기반

- `esp_wifi_scan_start()`로 주변 AP scan
- target BSSID와 일치하는 AP의 RSSI 추출
- target BSSID가 없으면 error flag 설정

#### 방식 B: 연결된 AP RSSI 기반

- ESP32가 target AP에 station으로 연결되어 있을 때 사용
- 연결된 AP 정보에서 RSSI 추출
- Gateway와 ESP-NOW channel 충돌 가능성이 줄어든다.

Codex는 구현 난이도와 안정성을 고려해 **scan 기반을 기본으로 구현하되, 함수 분리로 나중에 방식 B를 추가하기 쉽게 작성한다.**

### 6.3 필터링

1차 구현은 Moving Average를 기본으로 한다.

- window size: 5
- raw RSSI가 유효 범위 밖이면 폐기
- 유효 범위: `-100 dBm <= RSSI <= -10 dBm`
- filtered value는 정수 x10 단위로 저장한다.
  - 예: `-60.8 dBm` → `-608`

float 사용을 최소화하기 위해 STM32로 보낼 때는 `rssi_filtered_x10` 정수로 보낸다.

### 6.4 ESP-NOW 패킷 구조

ESP-NOW payload는 binary struct로 보낸다.

```c
#define RSSI_PACKET_MAGIC 0x52465349u  // 'RFSI'
#define RSSI_PACKET_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  node_id;
    uint16_t payload_len;

    uint32_t seq;
    uint32_t uptime_ms;

    uint8_t  ap_bssid[6];
    int8_t   rssi_raw_dbm;
    int16_t  rssi_filtered_x10;
    uint8_t  sample_count;
    uint16_t error_flags;

    uint32_t crc32;
} rssi_node_packet_t;
```

`crc32`는 `crc32` 필드를 0으로 둔 상태에서 앞부분 전체에 대해 계산한다.

### 6.5 Node FreeRTOS Task

최소 태스크 구성:

```text
RssiMeasureTask
  - RSSI raw sample 측정
  - filter에 sample 추가

EspNowTxTask
  - 1초마다 latest filtered RSSI 패킷 전송
  - seq 증가
  - 송신 실패 횟수 기록

HealthTask
  - free heap, fail count, latest RSSI 로그 출력
```

공유 데이터는 mutex 또는 queue로 보호한다.

### 6.6 Node Acceptance Criteria

- Serial monitor에서 1초마다 node 상태 로그가 보인다.
- target AP가 보이면 RSSI 값이 갱신된다.
- target AP가 안 보이면 error flag가 설정된다.
- Gateway가 켜져 있으면 ESP-NOW 전송 성공 callback이 증가한다.
- 30분 동안 watchdog reset 없이 동작한다.

---

## 7. ESP32 Gateway 상세 요구사항

### 7.1 UART 설정

Gateway는 STM32로 UART를 통해 line protocol을 보낸다.

초기 설정:

```c
#define STM32_UART_NUM          UART_NUM_1
#define STM32_UART_BAUDRATE     115200
#define STM32_UART_TX_GPIO      17
#define STM32_UART_RX_GPIO      18
```

주의:

- 실제 핀은 보드 상황에 따라 바꿀 수 있어야 한다.
- UART RX는 STM32에서 ACK를 받을 때를 대비해 열어두지만, 1차 구현에서는 TX만 사용해도 된다.
- ESP32 TX → STM32 RX
- ESP32 GND ↔ STM32 GND 필수
- 두 보드는 모두 3.3V logic 기준으로 연결한다.

### 7.2 Gateway 내부 상태

Gateway는 최대 8개 노드를 관리한다.

```c
#define MAX_NODES 8

typedef struct {
    bool     active;
    uint8_t  node_id;
    uint32_t last_seq;
    uint32_t last_rx_uptime_ms;
    int8_t   last_raw_rssi;
    int16_t  last_filtered_x10;
    uint32_t packet_count;
    uint32_t duplicate_count;
    uint32_t lost_count;
    uint16_t last_error_flags;
} node_state_t;
```

### 7.3 ESP-NOW 수신 처리

수신 callback에서는 무거운 처리를 하지 않는다.

권장 구조:

```text
ESP-NOW receive callback
  ↓
Queue에 packet copy
  ↓
GatewayProcessTask
  ↓
node_state 갱신
  ↓
UART line 생성
  ↓
UartForwardTask
```

callback 내부에서 printf, malloc, 긴 파싱을 하지 않는다.

### 7.4 STM32 UART Line Protocol

STM32에서 JSON 파싱은 무겁고 번거로우므로, 1차 구현은 CSV-like line protocol을 사용한다.

형식:

```text
$RSSI,<node_id>,<seq>,<uptime_ms>,<rssi_raw>,<rssi_filtered_x10>,<sample_count>,<error_flags>*<checksum>\n
```

예시:

```text
$RSSI,1,15234,3600123,-62,-608,5,0*5A
```

상태 메시지:

```text
$GWSTAT,<uptime_ms>,<rx_count>,<crc_error_count>,<queue_drop_count>*<checksum>\n
```

예시:

```text
$GWSTAT,3605000,5234,2,0*6C
```

### 7.5 Checksum 규칙

NMEA 스타일 XOR checksum을 사용한다.

- `$` 다음 문자부터 `*` 직전 문자까지 모든 byte XOR
- 결과를 2자리 대문자 hex로 출력
- STM32 parser는 checksum이 틀리면 해당 line을 폐기한다.

예:

```text
$RSSI,1,15234,3600123,-62,-608,5,0*5A
```

checksum 계산 대상:

```text
RSSI,1,15234,3600123,-62,-608,5,0
```

### 7.6 Gateway Acceptance Criteria

- 1개 node 패킷을 수신하고 UART line으로 변환한다.
- 4~5개 node가 동시에 전송해도 line이 깨지지 않는다.
- node별 seq 누락을 감지해 lost_count를 증가시킨다.
- 중복 seq는 duplicate_count를 증가시키고 STM32 전송은 선택적으로 생략한다.
- 30분 이상 동작 중 queue overflow가 없거나, overflow 발생 시 drop count가 기록된다.

---

## 8. STM32 Receiver 상세 요구사항

### 8.1 구현 범위

STM32 보드가 아직 확정되지 않았으므로, Codex는 특정 보드에 묶인 완성 프로젝트 대신 아래 파일을 만든다.

- `rssi_line_parser.c`
- `rssi_line_parser.h`
- `stm32_uart_receiver_example.c`
- `test_parser_host.c`

### 8.2 Parser 구조

동적 메모리 사용 금지.

```c
#define RSSI_LINE_MAX_LEN 128
#define RSSI_MAX_NODES 8

typedef struct {
    uint8_t  node_id;
    uint32_t seq;
    uint32_t uptime_ms;
    int8_t   rssi_raw_dbm;
    int16_t  rssi_filtered_x10;
    uint8_t  sample_count;
    uint16_t error_flags;
} rssi_measurement_t;

typedef enum {
    RSSI_PARSE_OK = 0,
    RSSI_PARSE_INCOMPLETE,
    RSSI_PARSE_CHECKSUM_ERROR,
    RSSI_PARSE_FORMAT_ERROR
} rssi_parse_result_t;
```

필수 함수:

```c
void rssi_parser_init(void);

rssi_parse_result_t rssi_parser_feed_byte(
    uint8_t byte,
    rssi_measurement_t *out_measurement
);

rssi_parse_result_t rssi_parse_line(
    const char *line,
    rssi_measurement_t *out_measurement
);
```

### 8.3 UART 수신 예제

`stm32_uart_receiver_example.c`에는 HAL 기반 예제를 작성한다.

요구사항:

- UART interrupt 또는 DMA 기반 수신 구조 예시
- byte 수신 시 `rssi_parser_feed_byte()` 호출
- parse 성공 시 node table 갱신
- checksum error count 증가
- format error count 증가

예시 node table:

```c
typedef struct {
    bool active;
    uint32_t last_seq;
    uint32_t last_update_tick;
    int8_t last_raw_rssi;
    int16_t last_filtered_x10;
    uint32_t received_count;
    uint32_t lost_count;
} stm32_node_state_t;
```

### 8.4 STM32 Acceptance Criteria

- PC에서 USB-UART로 아래 문자열을 STM32에 보내면 파싱된다.

```text
$RSSI,1,15234,3600123,-62,-608,5,0*XX
```

- checksum이 틀린 line은 폐기된다.
- 4~5개 node_id가 들어와도 node table이 갱신된다.
- line이 중간에 끊겼다가 다시 들어와도 parser가 복구된다.
- 동적 메모리를 사용하지 않는다.

---

## 9. Error Flags 정의

ESP32 Node가 설정하는 error flag는 다음과 같이 정의한다.

```c
#define RSSI_ERR_NONE             0x0000
#define RSSI_ERR_AP_NOT_FOUND     0x0001
#define RSSI_ERR_SCAN_FAILED      0x0002
#define RSSI_ERR_FILTER_EMPTY     0x0004
#define RSSI_ERR_ESPNOW_SEND_FAIL 0x0008
#define RSSI_ERR_TIME_INVALID     0x0010
#define RSSI_ERR_LOW_HEAP         0x0020
```

Gateway는 error_flags를 그대로 STM32로 전달한다.

---

## 10. 구현 순서

Codex는 한 번에 모든 기능을 만들려고 하지 말고 아래 순서로 커밋 단위를 나눈다.

### Step 1: Protocol and Parser

- `docs/protocol.md`
- ESP-NOW binary packet struct
- UART line protocol
- XOR checksum 함수
- STM32 parser module
- host parser test

완료 조건:

- host 환경에서 line checksum 생성/검증 테스트 가능

### Step 2: ESP32 Gateway UART 출력

- ESP32 Gateway 프로젝트 생성
- UART 초기화
- 가짜 rssi packet을 1초마다 line protocol로 출력

완료 조건:

- Serial terminal에서 `$RSSI,...*XX` line 확인

### Step 3: ESP32 Node RSSI 측정

- Node 프로젝트 생성
- Wi-Fi scan으로 target BSSID RSSI 측정
- moving average 적용
- serial log 출력

완료 조건:

- node serial monitor에서 RSSI raw/filtered 출력

### Step 4: ESP-NOW Node → Gateway

- ESP-NOW 초기화
- Node가 Gateway MAC으로 binary packet 송신
- Gateway가 수신 후 UART line으로 변환

완료 조건:

- Node 1개에서 측정한 RSSI가 Gateway UART로 출력

### Step 5: Multi-node

- node_id 1~5 설정 방법 문서화
- 4~5개 Node 동시 송신
- Gateway node table 갱신
- 중복/누락 count 기록

완료 조건:

- 4~5개 node_id의 RSSI line이 STM32로 전송됨

### Step 6: STM32 수신 통합

- STM32 HAL UART 예제 연결
- parser module 사용
- node table 표시

완료 조건:

- STM32가 node별 최신 RSSI를 저장하고 timeout을 감지함

---

## 11. Codex가 지켜야 할 구현 원칙

### 반드시 지킬 것

- C로 작성한다.
- ESP32는 ESP-IDF 기반으로 작성한다.
- FreeRTOS queue를 사용해 callback과 task를 분리한다.
- STM32 parser는 동적 메모리를 사용하지 않는다.
- STM32로는 JSON이 아니라 line protocol을 보낸다.
- RSSI filtered value는 float 대신 x10 정수로 전달한다.
- 모든 protocol struct와 line format은 문서화한다.
- `README.md`에 빌드/플래시/테스트 방법을 작성한다.

### 하지 말 것

- LCD, IMU, JPEG, 3DGS Viewer 기능을 이번 단계에 넣지 않는다.
- ESP32 receive callback 안에서 긴 작업을 하지 않는다.
- STM32에서 malloc/free를 사용하지 않는다.
- STM32에서 cJSON 같은 무거운 JSON parser를 사용하지 않는다.
- 처음부터 MQTT를 넣지 않는다.
- 처음부터 배터리/OTA까지 넣지 않는다.

---

## 12. Test Plan

### 12.1 단일 노드 테스트

```text
Node 1 → Gateway → PC Serial
```

확인:

- 1초마다 RSSI line 출력
- seq 증가
- RSSI 값 정상 범위
- checksum 정상

### 12.2 STM32 parser 단독 테스트

PC에서 STM32 UART에 샘플 line 송신.

확인:

- 정상 line parse OK
- checksum error line 폐기
- format error line 폐기
- 중간에 깨진 line 이후 복구

### 12.3 5개 노드 테스트

```text
Node 1~5 → Gateway → STM32
```

확인:

- node_id별 최신 값 유지
- lost_count 추정 가능
- duplicate_count 기록
- 30분 이상 동작

### 12.4 장애 테스트

- target AP 전원 끄기
- Gateway 끄기
- Node 1개 리셋
- STM32 UART 선 잠깐 분리

확인:

- error flag 또는 timeout으로 상태 파악 가능
- 재연결 후 seq가 다시 들어옴
- 시스템이 멈추지 않음

---

## 13. README에 반드시 포함할 내용

Codex는 최종 `README.md`에 아래 내용을 포함한다.

```text
1. 프로젝트 개요
2. 아키텍처 그림
3. 하드웨어 구성
4. ESP32 Node 설정 방법
5. ESP32 Gateway 설정 방법
6. STM32 UART 연결 방법
7. packet format
8. line protocol format
9. 빌드 방법
10. 플래시 방법
11. 테스트 순서
12. 알려진 제한사항
```

---

## 14. 알려진 제한사항

README에 다음 제한사항을 명시한다.

1. ESP-NOW와 Wi-Fi scan은 Wi-Fi channel 영향을 받는다.
2. 1차 구현에서는 target AP channel과 ESP-NOW channel을 같게 맞추는 것을 권장한다.
3. RSSI는 실내 환경에서 변동성이 크므로 절대 거리값으로 직접 해석하지 않는다.
4. 이 단계의 목표는 정확한 위치 추정이 아니라 안정적인 RSSI 수집과 STM32 전달이다.
5. STM32 보드가 확정되면 UART 핀과 HAL 설정은 별도 조정이 필요하다.
6. LCD, IMU, JPEG, Viewer 연동은 다음 단계에서 구현한다.

---

## 15. Codex에 그대로 넣을 첫 프롬프트

아래 내용을 Codex 첫 작업 프롬프트로 사용한다.

```text
You are implementing the first embedded prototype for a graduation project.

Build a repository named `rssi_esp32_to_stm32`.

Goal:
- 4~5 ESP32 RSSI nodes measure Wi-Fi RSSI of a target AP.
- Nodes send measurements to one ESP32 gateway using ESP-NOW.
- The ESP32 gateway forwards measurements to an STM32 over UART.
- STM32 receives line protocol messages, verifies checksum, parses values, and updates a node table.

Do not implement LCD, IMU, JPEG streaming, MQTT, or 3D viewer integration in this task.

Use:
- ESP-IDF in C for ESP32 node and gateway.
- Generic STM32 HAL-compatible C parser module for STM32.
- No dynamic memory in STM32 parser.
- FreeRTOS queues to separate ESP-NOW callbacks from processing tasks.
- CSV-like NMEA-style line protocol for Gateway → STM32.

Required repository structure:
[use the structure described in this md file]

Implement in small steps:
1. protocol docs, checksum, parser
2. gateway UART fake RSSI output
3. node RSSI scan and filtering
4. ESP-NOW node-to-gateway transfer
5. multi-node state table
6. STM32 parser integration example

Acceptance criteria:
- Gateway prints `$RSSI,...*XX` lines over UART.
- STM32 parser verifies checksum and parses RSSI messages.
- 4~5 nodes can be distinguished by node_id.
- node seq, RSSI raw, RSSI filtered x10, sample_count, and error_flags are transmitted.
- README includes build, flash, wiring, and test instructions.
```

---

## 16. 이번 단계 완료 후 다음 단계

이 단계가 끝나면 다음 순서로 확장한다.

1. Gateway UART line을 백엔드 또는 PC에서도 수신 가능하게 테스트
2. STM32에서 받은 node별 RSSI를 간단한 LCD/Serial UI로 표시
3. MQTT 구조와 비교
4. 핸드헬드 ESP32-S3의 IMU, LCD, JPEG 출력 단계로 이동
5. RSSI Fingerprinting/Position Snap 실험으로 확장
