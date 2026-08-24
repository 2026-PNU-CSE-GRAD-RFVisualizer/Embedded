# RFVisualizer Handheld 통합 규약 및 파트별 작업

작성일: 2026-08-21  
대상 보드: ESP32-S3-DevKitC-1-N16R8 (Flash 16 MB, PSRAM 8 MB)

2026-08-24 진행 상태:

- Backend가 RFHC v1 Parser, UDP Listener, PositionProvider 및 Graphics WebSocket을 구현했다고 회신했다.
- Embedded RFHC v1 Serializer와 Host Test를 구현했다.
- Backend 공유 52-byte 테스트 벡터와 CRC `0x0AE927E5`가 완전히 일치했다.
- Embedded와 Backend는 RFHC v1 Control Wire 규격 동결에 동의한다.
- 실제 `q_mount`, 버튼 GPIO, UDP 실물 통합 및 Graphics Camera 축 시험은 남아 있다.

## 1. 목적과 현재 완료 상태

최종 목표는 핸드헬드의 방향과 위치를 Graphics 카메라에 반영하고, 그 결과를 서버에서 영상으로 받아 LCD에서 실시간으로 확인하는 것이다.

```text
BNO085 Quaternion ──UDP──> Backend ──WebSocket──> Graphics Camera
Position Update 버튼 ────> Backend PositionProvider ───────> Graphics Camera
Graphics Render ──TCP 9101──> image_relay ──TCP 9102──> ESP32-S3 ──> LCD
```

확인된 완료 항목:

- 실제 보드는 N16R8이다.
- BNO085 단독 Quaternion 실물 시험을 완료했다.
- 서버 더미 데이터로 JPEG 수신, 디코딩 및 LCD 출력을 완료했다.
- JPEG 수신 Firmware에는 Frame 검증, 최신 Frame 우선 처리 및 재연결 경로가 구현되어 있다.

이제 남은 핵심 작업:

1. Handheld Control v1 규약 합의 및 중앙 문서 반영
2. 버튼 입력 및 UDP Quaternion/Event 송신 구현
3. Backend의 PositionProvider와 Handheld 수신 경로 구현
4. Graphics 카메라 Orientation/Position 연동
5. 기존 BNO085 코드와 JPEG-LCD 코드를 하나의 N16R8 Firmware로 통합
6. 360도 회전, 위치 갱신, 지연 및 장애 복구 통합 시험

## 2. 반드시 구분할 개념

BNO085는 위치 `(x, y, z)`를 측정하지 않는다. BNO085가 제공하는 값은 기기의 방향 Quaternion이다.

- Orientation: BNO085가 지속적으로 측정하고 Handheld가 50 Hz로 전송한다.
- Position: Backend가 `PositionProvider`를 통해 제공한다.
- Position Update 버튼: Handheld가 좌표를 계산해서 보내는 버튼이 아니라, Backend의 최신 유효 위치를 Graphics 카메라에 적용해 달라고 요청하는 버튼이다.
- Recenter 버튼: 현재 Handheld 방향을 Graphics의 기준 정면으로 다시 설정한다.
- LCD 영상: Position 또는 Orientation이 변경될 때 Graphics가 다시 렌더링한 최신 Frame을 연속으로 받는다.

따라서 360도 확인 중에는 버튼을 계속 누르지 않는다. Position Update를 한 번 수행한 뒤 Handheld를 돌리면 Quaternion이 계속 전송되고 렌더링 화면이 연속으로 바뀌어야 한다.

## 3. Handheld Control v1 동결 제안

이 절은 세 파트가 승인한 후 `RFVisualizer-Docs/INTERFACE.md`에 반영하고 v1으로 동결한다. 구현 중 임의 변경하지 않는다.

### 3.1 전송 방식

| 항목 | v1 값 |
|---|---|
| Handheld → Backend | UDP |
| 기본 Port | `9200` |
| 전송 주기 | 50 Hz, 20 ms |
| Byte Order | 모든 다중 Byte 필드 Big-endian |
| Float | IEEE-754 binary32의 32-bit 표현을 Big-endian으로 전송 |
| Packet 크기 | 52 byte 고정 |
| 무결성 | CRC32/IEEE |
| 수신 Timeout | 마지막 유효 Packet 이후 500 ms |

Port `9200`이 배포 환경에서 충돌하면 세 파트가 함께 다른 값을 정하고 문서를 먼저 변경한다.

### 3.2 52-byte Packet Layout

Magic은 ASCII `RFHC`, 정수값 `0x52464843`이다.

| Offset | 크기 | 필드 | 설명 |
|---:|---:|---|---|
| 0 | 4 | `magic` | `0x52464843` (`RFHC`) |
| 4 | 1 | `version` | `1` |
| 5 | 1 | `flags` | 아래 Flag 정의 |
| 6 | 2 | `packet_length` | 항상 `52` |
| 8 | 4 | `device_id` | `1` = `handheld-01`, RSSI Node ID와 별도 Namespace |
| 12 | 4 | `session_id` | 부팅마다 바뀌는 non-zero 임의 값 |
| 16 | 4 | `sample_seq` | 송신 Packet마다 1 증가, wrap 허용 |
| 20 | 4 | `event_seq` | 버튼 이벤트가 새로 발생할 때만 1 증가 |
| 24 | 8 | `timestamp_ms` | SNTP 동기화 시 Unix epoch ms, 미동기화 시 `0` |
| 32 | 4 | `quaternion_x` | 정규화된 Quaternion X |
| 36 | 4 | `quaternion_y` | 정규화된 Quaternion Y |
| 40 | 4 | `quaternion_z` | 정규화된 Quaternion Z |
| 44 | 4 | `quaternion_w` | 정규화된 Quaternion W |
| 48 | 4 | `crc32` | Offset 0~47에 대한 CRC32/IEEE |

CRC 파라미터:

```text
Name: CRC-32/ISO-HDLC (CRC32/IEEE)
Polynomial: 0x04C11DB7
RefIn/RefOut: true
Init: 0xFFFFFFFF
XorOut: 0xFFFFFFFF
Check("123456789"): 0xCBF43926
```

### 3.3 Flag

| Bit | 이름 | 의미 |
|---:|---|---|
| 0 | `ORIENTATION_VALID` | Quaternion 검증을 통과함 |
| 1 | `REQUEST_POSITION_UPDATE` | 최신 유효 Position 적용 요청 |
| 2 | `RECENTER_ORIENTATION` | 현재 방향을 기준 정면으로 설정 요청 |
| 3 | `TIME_SYNCED` | `timestamp_ms`가 Unix epoch ms로 유효함 |
| 4~7 | Reserved | 송신 시 반드시 0 |

Quaternion이 finite이고 norm이 `0.97~1.03`일 때만 `ORIENTATION_VALID`를 설정한다. 수신 측은 허용 범위 안의 Quaternion도 적용 전에 다시 정규화한다.

### 3.4 버튼 이벤트 유실 및 중복 처리

- 버튼을 누를 때 `event_seq`를 1 증가시킨다.
- 해당 Event Flag를 3개의 연속 Packet에 반복해서 보낸다.
- Backend는 `(device_id, session_id, event_seq, event_flag)` 조합으로 중복을 제거한다.
- 같은 이벤트는 서버에서 정확히 한 번만 처리한다.
- 두 버튼이 동시에 눌리면 두 Flag를 함께 설정할 수 있다.
- Debounce 기준은 30 ms를 기본값으로 한다.

Backend는 숫자 `device_id`를 외부 ID `handheld-01`에 매핑한다. 허용 Device ID 목록과 필요 시 허용 Source IP도 서버 설정으로 제한한다. 이 ID는 RSSI Node 내부 ID `1~5`와 다른 Handheld Namespace이므로 서로 혼용하지 않는다.

### 3.5 Timestamp와 순서 처리

- `sample_seq`는 부팅 후 임의 시작값 또는 0에서 시작하고 Packet마다 증가한다.
- Backend는 `session_id`가 바뀌면 재부팅으로 처리하고 Sequence 상태를 초기화한다.
- `TIME_SYNCED=0`이면 Backend 수신 시각을 공식 시각으로 사용한다.
- `TIME_SYNCED=1`이어도 시각 차이가 허용 범위를 벗어나면 Backend 수신 시각을 사용하고 skew를 기록한다.
- 오래된 Sequence 또는 중복 Packet은 Orientation에 다시 적용하지 않는다.
- 500 ms Timeout 후 Graphics는 마지막 Camera Orientation을 유지하되 Handheld 연결 상태를 stale로 표시한다.

## 4. 좌표계와 Quaternion 규약

### 4.1 World Position

- 단위는 meter다.
- `+Z`는 위쪽이다.
- `X`, `Y`는 바닥 평면이다.
- 원점과 `+X`, `+Y` 방향은 Experiment/Scene 설정에 기록한다.
- Backend Position과 Graphics Scene은 동일한 Frame ID와 원점을 사용해야 한다.
- `(0,0,0)` Placeholder를 실제 위치로 적용하지 않는다.

Position 값에는 최소한 다음 메타데이터가 함께 있어야 한다.

```json
{
  "timestamp": 1785720000000,
  "frame_id": "experiment-room-v1",
  "position_x": 1.25,
  "position_y": 3.40,
  "position_z": 1.20,
  "confidence": 0.85,
  "status": "valid",
  "source": "configured_demo"
}
```

### 4.2 Handheld 논리 축

LCD를 사용자가 정면에서 바라보는 상태를 기준으로 다음 논리 축을 사용한다.

- `+X`: 화면 오른쪽
- `+Y`: 화면 위쪽
- `+Z`: 화면에서 사용자 방향으로 나오는 방향

세 축은 오른손 좌표계다. BNO085 Breakout의 실제 장착 방향과 Handheld 논리 축 사이의 고정 회전 `q_mount`는 Embedded 설정으로 한 곳에 명시한다.

Wire Packet에는 BNO085 원시 보드 축이 아니라 `q_mount`가 반영된 Handheld 논리 축 Quaternion을 넣는 것을 원칙으로 한다. 정확한 곱셈 순서는 실제 장착 방향과 BNO085 Quaternion 정의를 확인하는 축별 시험으로 확정한다.

### 4.3 Recenter와 Camera 변환

Recenter 계산은 Graphics가 담당한다.

1. `RECENTER_ORIENTATION` 이벤트를 받은 시점의 최신 유효 Quaternion을 `q_reference`로 저장한다.
2. 이후 상대 회전 `q_relative`를 계산한다.
3. `q_relative`에 Graphics 카메라의 고정 축 변환을 적용한다.
4. Position은 바꾸지 않고 Camera Orientation만 갱신한다.

Quaternion 곱셈 방향은 사용하는 Graphics 수학 라이브러리의 convention에 따라 달라질 수 있으므로 수식만 보고 확정하지 않는다. 다음 실물 Acceptance Test로 최종 고정한다.

- Handheld를 오른쪽으로 돌리면 화면도 오른쪽을 바라본다.
- Handheld를 위로 들면 화면도 위를 바라본다.
- Roll 동작이 Yaw/Pitch로 섞이지 않는다.
- 90도, 180도, 360도 회전 후 원래 방향으로 복귀한다.

## 5. 위치 구현 방안

Backend는 위치 출처를 `PositionProvider` 인터페이스로 분리한다. Handheld와 Graphics는 위치 계산 방식에 의존하지 않는다.

```text
PositionProvider.get_latest()
  -> timestamp, frame_id, x, y, z, confidence, status, source
```

### 5.1 1차 통합/시연용 구현

실제 위치 추정 알고리즘이 준비되기 전에는 `ConfiguredPositionProvider`를 사용한다.

- 실제 측정한 시연 위치를 서버 로컬 설정에 meter 단위로 등록한다.
- Placeholder `(0,0,0)`는 등록하지 않는다.
- 현재 활성 위치를 서버의 관리 API 또는 실행 설정으로 선택한다.
- Position Update 버튼을 누르면 현재 활성 위치를 Graphics에 적용한다.
- Payload의 `source`는 `configured_demo`로 표시한다.
- 설정 파일에는 Scene의 `frame_id`를 반드시 포함한다.

이 방식은 위치 추정 완료를 의미하지 않으며 Handheld/Graphics 통합을 먼저 검증하기 위한 구현이다.

### 5.2 최종 구현

위치 추정 알고리즘이 완성되면 `EstimatedPositionProvider`로 교체한다.

- 기존 `/position/latest` 응답 Schema를 유지한다.
- `source`를 실제 알고리즘 이름으로 기록한다.
- Position Update 요청 시 최신 결과가 너무 오래되었거나 confidence가 기준 미만이면 적용하지 않는다.
- 거부 시 기존 Camera Position을 유지하고 이유를 Graphics와 로그에 전달한다.
- 다른 Experiment/Scene의 `frame_id`이면 적용을 거부한다.

권장 기본 유효성 기준:

- `position_x/y/z`가 모두 finite
- `status == "valid"`
- 위치 age 2초 이하
- `confidence >= 0.5`
- Backend Position의 `frame_id`와 Graphics Scene `frame_id`가 동일

임계값은 실제 알고리즘 특성에 따라 Backend 설정으로 조정하되 Handheld Firmware에 고정하지 않는다.

## 6. Backend가 구현하고 확인할 것

아래 내용을 Backend 파트에 그대로 전달한다.

> Handheld는 BNO085 Quaternion과 버튼 이벤트를 UDP `9200`의 52-byte Handheld Control v1 Packet으로 보낼 예정입니다. Backend에서 magic/version/length/CRC/Quaternion norm/sequence를 검증하고, `(device_id, session_id, event_seq, flag)`로 버튼 이벤트 중복을 제거해 주세요. 정상 Orientation은 Graphics에 실시간 전달하고, Position Update 이벤트가 오면 `PositionProvider.get_latest()`의 최신 유효 좌표를 Graphics에 적용해 주세요. Position이 null, stale, low-confidence 또는 다른 frame_id이면 적용하지 말고 기존 Camera Position을 유지해 주세요. 초기 통합에는 실제 좌표를 설정 파일에 등록하는 `ConfiguredPositionProvider`를 사용하고, 이후 위치 추정 알고리즘 Provider로 교체할 수 있게 분리해 주세요.

Backend 작업 목록:

- [ ] UDP `9200` Listener 구현
- [ ] 52-byte Control Packet Parser와 CRC32 검증
- [ ] Version, reserved bit, length, finite, Quaternion norm 검증
- [ ] `session_id`별 Sequence loss, duplicate, out-of-order 통계
- [ ] 버튼 Event 정확히 한 번 처리
- [ ] 마지막 유효 Handheld 상태와 500 ms stale 판정
- [ ] `PositionProvider` 인터페이스 구현
- [ ] 시연용 `ConfiguredPositionProvider`와 실제 좌표 설정
- [ ] `/position/latest`에 `frame_id`, `source`, 유효 status 반영
- [ ] Position의 null/stale/confidence/frame_id 검증
- [ ] Graphics 전달용 WebSocket `/handheld/control` 구현
- [ ] Graphics 연결 해제 및 재접속 시 최신 상태 재전송
- [ ] Handheld Packet drop, invalid, timeout, position reject 로그/계측

Backend → Graphics WebSocket 메시지 권장 Schema:

```json
{
  "type": "handheld_state",
  "device_id": "handheld-01",
  "session_id": 305419896,
  "sample_seq": 15234,
  "event_seq": 7,
  "server_timestamp_ms": 1785720000123,
  "orientation_valid": true,
  "quaternion": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
  "recenter_event": false,
  "position_update_event": false,
  "stale": false
}
```

Position 적용 성공 메시지:

```json
{
  "type": "position_update",
  "device_id": "handheld-01",
  "event_seq": 7,
  "accepted": true,
  "position": {
    "frame_id": "experiment-room-v1",
    "x": 1.25,
    "y": 3.40,
    "z": 1.20,
    "confidence": 0.85,
    "source": "configured_demo"
  }
}
```

거부 시 `accepted=false`, `position=null`, `reason`을 전송한다. 예: `no_position`, `stale_position`, `low_confidence`, `frame_mismatch`.

## 7. Graphics가 구현하고 확인할 것

아래 내용을 Graphics 파트에 그대로 전달한다.

> Backend의 WebSocket `/handheld/control`에서 최신 Quaternion과 버튼 이벤트를 받아 Camera를 갱신해 주세요. Orientation은 Position과 독립적으로 계속 적용하고, Position은 `position_update` 메시지가 `accepted=true`일 때만 변경해 주세요. Recenter 시점의 Quaternion을 기준 방향으로 저장하고 Handheld 논리 축을 Graphics Camera 축으로 변환해 주세요. 렌더 결과는 800×480 baseline JPEG, quality 60~75로 만들고 기존 RFJF v1 header를 붙여 image relay ingest port `9101`로 보내 주세요. 렌더가 느리면 오래된 Frame을 누적하지 말고 항상 최신 Camera 상태를 렌더해 주세요.

Graphics 작업 목록:

- [ ] Backend WebSocket `/handheld/control` Client 구현
- [ ] 재접속과 최신 Handheld 상태 복구
- [ ] `frame_id`와 현재 Scene 좌표계 일치 확인
- [ ] Handheld 축 → Camera 축 고정 변환 구현
- [ ] Recenter 기준 Quaternion 저장 및 상대 회전 적용
- [ ] Quaternion 정규화와 비정상 값 거부
- [ ] Orientation과 Position 독립 갱신
- [ ] `accepted=true` Position만 적용
- [ ] Position 거부 시 기존 Camera Position 유지
- [ ] 500 ms stale 시 마지막 Orientation 유지
- [ ] 최신 Camera 상태 우선 렌더링
- [ ] 800×480 baseline JPEG 출력
- [ ] 권장 JPEG quality 60~75
- [ ] RFJF v1 Frame을 TCP `9101`로 전송
- [ ] Render, encode, network 시간을 각각 계측

Graphics → Relay JPEG 규격은 현재 Embedded 구현을 유지한다.

```text
22-byte RFJF v1 header, Big-endian
magic      uint32  0x52464A46 ('RFJF')
version    uint8   1
flags      uint8   0 (JPEG)
seq        uint32  Frame sequence
ts_ms      uint64  Unix epoch milliseconds
length     uint32  JPEG byte count
payload            baseline JPEG, 800x480
```

현재 Embedded 코드는 `flags=1`인 RGB332+zlib도 지원하지만, 파트 간 기본 영상 규격은 검증이 끝난 `flags=0` JPEG로 유지한다. 변경이 필요하면 먼저 공통 문서를 갱신한다.

## 8. Embedded에서 내가 해야 할 것

통합 Firmware는 현재 동작이 확인된 `handheld_jpeg_stream`을 기준으로 만들고, `handheld_bno085_test/components/bno085_espidf`와 CEVA SH-2 Component를 합친다.

### 8.1 설정과 보드

- [ ] 기준 보드를 N16R8로 통일
- [ ] Flash 16 MB, PSRAM 8 MB 설정 확인
- [ ] 실제 Partition Table과 PSRAM 부팅 로그 확인
- [ ] 실제 Wi-Fi, 서버 IP, Port는 로컬 `sdkconfig`에서만 설정
- [ ] BNO085 GPIO39~42와 LCD GPIO 충돌 재확인
- [ ] 두 버튼의 GPIO와 pull-up/pull-down 확정
- [ ] 버튼 GPIO가 strapping/Flash/PSRAM/LCD와 충돌하지 않는지 확인

### 8.2 코드 통합

- [ ] 새 통합 프로젝트 또는 `handheld_jpeg_stream` 통합 Branch 준비
- [ ] BNO085/CEVA Component 이식
- [ ] `ImuTask`에서 BNO085 service와 최신 Quaternion 관리
- [ ] `InputTask`에서 30 ms debounce와 두 버튼 Event 생성
- [ ] 52-byte Control Packet Serializer 구현
- [ ] CRC32/IEEE 구현 또는 ESP-IDF 검증된 CRC API 사용
- [ ] `ControlTxTask`에서 UDP 50 Hz 송신
- [ ] 버튼 Event를 3 Packet 반복하고 `event_seq` 관리
- [ ] Wi-Fi 연결 상태를 JPEG TCP와 UDP Tx가 공유
- [ ] 기존 `VideoRxTask`와 Frame 최신 우선 정책 유지
- [ ] JPEG 디코드/LCD 출력이 IMU와 UDP 송신을 block하지 않게 Task 분리
- [ ] `HealthTask` 통계 출력

### 8.3 Embedded 통계

최소한 다음 값을 주기적으로 출력한다.

- BNO085 sample rate, sequence loss, I2C error, recovery
- Quaternion invalid/non-finite/norm error
- UDP sent, send error, local queue drop
- Button press, debounce reject, event resend
- JPEG received, sequence gap, invalid, stale drop
- TCP reconnect, receive timeout
- JPEG decode time, LCD draw time, displayed FPS
- Internal RAM/PSRAM free 및 minimum free heap

### 8.4 Host Test

- [ ] Control Packet 정상 Vector
- [ ] Big-endian 필드 확인
- [ ] IEEE-754 Float 직렬화 확인
- [ ] CRC 정상/오류 확인
- [ ] 잘못된 magic/version/length/reserved bit 거부
- [ ] Quaternion NaN/Inf/norm 오류 거부
- [ ] `sample_seq` wrap 처리
- [ ] `session_id` 변경 처리
- [ ] 동일 `event_seq` 중복 제거용 Test Vector를 Backend와 공유
- [ ] 기존 JPEG Protocol Host Test 재실행

Backend와 Embedded는 동일한 고정 Test Vector 하나를 각자 Parser/Serializer Test에 넣어야 한다. Packet Byte와 예상 CRC가 완전히 같아야 통합 시험으로 넘어간다.

## 9. 통합 시험 순서와 완료 기준

### 9.1 Control 경로만 시험

1. LCD/JPEG를 끄고 BNO085 + 버튼 + UDP만 실행한다.
2. Backend가 50 Hz Quaternion, Sequence 및 Event를 정상 수신하는지 확인한다.
3. Packet loss, 중복 및 순서 변경을 주입한다.
4. 버튼 한 번당 서버 처리 한 번인지 확인한다.

완료 기준:

- Quaternion 유효 수신율 기록
- 버튼 100회 입력에서 누락 및 중복 처리 0회
- Handheld 재부팅 후 새 `session_id` 정상 인식
- UDP 중단 500 ms 후 stale 처리

### 9.2 Graphics 방향 시험

1. 고정 Camera Position에서 Recenter한다.
2. Yaw, Pitch, Roll을 각각 90도씩 움직인다.
3. 축 방향과 부호를 확인한다.
4. 360도 회전 후 기준 방향으로 복귀하는지 확인한다.

완료 기준:

- 세 축의 방향/부호 정상
- Recenter 시 Position 불변
- Orientation Packet 중단 시 Camera 폭주 또는 초기화 없음

### 9.3 Position 시험

1. Placeholder가 아닌 실제 측정 좌표 2개 이상을 서버에 등록한다.
2. 첫 좌표를 활성화하고 Position Update 버튼을 누른다.
3. Graphics Camera Position과 `frame_id`를 확인한다.
4. 다른 좌표를 활성화하고 다시 갱신한다.
5. null, stale, low-confidence, frame mismatch를 각각 주입한다.

완료 기준:

- 유효 Position은 정확히 한 번 적용
- 무효 Position은 적용하지 않고 기존 Position 유지
- Position 단위 meter 및 `+Z` 방향 일치

### 9.4 전체 360도 영상 시험

1. Position Update로 시작 위치를 적용한다.
2. Handheld를 360도로 움직인다.
3. Graphics가 최신 Camera 상태로 렌더한다.
4. Relay를 통해 최신 JPEG가 LCD에 표시되는지 확인한다.
5. Wi-Fi, Backend, Graphics 및 Relay를 각각 재시작해 복구를 확인한다.

완료 기준:

- 최소 5 FPS, 목표 10 FPS
- 오래된 Frame 지연 누적 없음
- 방향과 LCD 영상 움직임 일치
- Position은 버튼 이벤트 때만 변경
- 30분 이상 연속 동작에서 reset/메모리 누수 없음
- Orientation-to-display 지연, Packet loss, stale drop, reconnect 시간을 기록

## 10. 파트 합의가 필요한 체크리스트

아래 항목에 세 파트가 합의하면 v1 규약을 확정하고 `RFVisualizer-Docs/INTERFACE.md`, `CURRENT_STATUS.md`, `embedded/EMBEDDED.md`를 갱신한다.

- [ ] Handheld UDP Port `9200`
- [ ] 52-byte RFHC v1 Packet Layout
- [ ] Big-endian 및 CRC32/IEEE
- [ ] Quaternion 50 Hz, Timeout 500 ms
- [ ] 버튼 Event 3회 반복 및 `event_seq` 중복 제거
- [ ] Handheld 논리 축 정의
- [ ] Backend WebSocket `/handheld/control`
- [ ] Position `frame_id`, meter, `+Z` 위쪽
- [ ] 시연용 `ConfiguredPositionProvider`
- [ ] 최종 위치 추정 Provider 담당과 일정
- [ ] JPEG RFJF v1, TCP 9101/9102, 800×480 baseline JPEG

## 11. 중앙 문서 반영 조건

이 문서는 작업 및 합의를 위한 구체적인 v1 제안서다. 세 파트 승인 전에는 중앙 인터페이스의 최종 규격으로 간주하지 않는다.

합의 후 갱신할 중앙 문서:

- `INTERFACE.md`: RFHC Control Packet, 좌표/Quaternion, Backend→Graphics 메시지, RFJF JPEG 규격
- `CURRENT_STATUS.md`: BNO085 단독 시험과 JPEG→LCD 실물 시험 완료 상태, 통합 진행 상태
- `embedded/EMBEDDED.md`: N16R8 보드, 실제 Task/Buffer/GPIO 구성 및 실물 Throughput
