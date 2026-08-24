# Handheld Control v1 — Embedded 검증 결과 및 Backend 회신

작성: Embedded 파트  
작성일: 2026-08-24  
대상: Network/Backend 파트, Graphics 파트 공유 가능

## 1. 결론

Backend가 제시한 RFHC v1 52-byte 규격에 동의한다. Embedded C Serializer와 Host Test를 구현했고, Backend 공유 테스트 벡터의 전체 52 byte 및 CRC `0x0AE927E5`가 완전히 일치했다.

Control Wire 규격은 다음 값으로 동결하는 데 동의한다.

- UDP Port `9200`
- 50 Hz, 20 ms
- 52 byte 고정 RFHC v1
- 모든 다중 Byte 필드 Big-endian
- Float IEEE-754 binary32 Big-endian
- CRC-32/ISO-HDLC, Offset 0~47 계산, Offset 48~51 저장
- 500 ms 수신 Timeout 후 stale
- 버튼 Event Flag 3 Packet 반복
- `(device_id, session_id, event_seq, flag)` 중복 제거
- `device_id=1` → `handheld-01`

## 2. 공유 테스트 벡터 검증 결과

Backend 입력:

```text
version=1
flags=0x01
device_id=1
session_id=0x12345678
sample_seq=1
event_seq=0
timestamp_ms=0
quaternion=(0,0,0,1)
```

Embedded Serializer 출력:

```text
52464843010100340000000112345678000000010000000000000000000000000000000000000000000000003F8000000AE927E5
```

검증 결과:

```text
6/6 RFHC serializer tests passed; backend vector matched
```

추가로 확인한 항목:

- `"123456789"` CRC32/IEEE = `0xCBF43926`
- Reserved Flag 거부
- Orientation Valid 상태의 잘못된 Quaternion norm 거부
- `device_id=0` 거부
- `session_id=0` 거부

구현 위치:

- `handheld_jpeg_stream/main/handheld_control_protocol.h`
- `handheld_jpeg_stream/main/handheld_control_protocol.c`
- `handheld_jpeg_stream/test/test_handheld_control_protocol_host.c`

## 3. Backend 질문 답변

### 3.1 CRC 범위

맞다. Offset `0~47`의 48 byte에 CRC-32/ISO-HDLC를 계산하고, Big-endian 결과를 Offset `48~51`에 저장한다.

### 3.2 Float 인코딩

맞다. IEEE-754 binary32의 비트 표현을 Big-endian으로 보낸다. `1.0f`는 `3F800000`이다.

### 3.3 Wire Quaternion과 q_mount

Wire Packet에는 BNO085 Breakout 원시 축이 아니라 `q_mount`를 반영한 Handheld 논리 축 Quaternion을 보낸다.

Handheld 논리 축:

- `+X`: LCD 화면 오른쪽
- `+Y`: LCD 화면 위쪽
- `+Z`: LCD 화면에서 사용자 쪽으로 나오는 방향

다만 실제 `q_mount` 상수와 곱셈 순서는 BNO085 Breakout의 최종 장착 방향을 고정한 후 축별 90도 실물 시험으로 확정해야 한다. Serializer는 Wire 형식까지 구현됐고, IMU→논리 축 변환 연결은 아직 남아 있다. Backend는 별도의 BNO085 보드 축 보정을 하지 않고 이미 논리 축으로 변환된 Quaternion을 받는 것으로 구현하면 된다.

### 3.4 TIME_SYNCED=0

맞다. `TIME_SYNCED=0`이면 Embedded는 `timestamp_ms=0`을 보내며 Backend는 수신 시각을 사용한다.

### 3.5 허용 Device ID와 Source IP

- 초기 통합 허용 Device ID: `1` (`handheld-01`)
- 초기 통합에서는 DHCP 변경 가능성이 있으므로 Source IP 제한을 사용하지 않는다.
- 실제 시연 네트워크의 고정 IP가 결정되면 선택적으로 Source IP Allowlist를 추가한다.
- Backend는 미등록 `device_id`를 거부하고 계측한다.

## 4. Backend 구현 현황 검토

보내준 문서 기준으로 Backend는 기존 합의안의 필수 기능을 모두 구현했다.

- RFHC Parser/CRC/Quaternion 검증
- Sequence 통계와 Event 중복 제거
- UDP `9200`
- 500 ms stale
- PositionProvider 및 유효성 검사
- WebSocket `/handheld/control`
- 관리 API와 최신 상태 재전송
- 기본 비활성화로 기존 RSSI 경로 보호

Embedded 공유 벡터가 일치하므로 Control Wire 규격 관점의 Backend 수정 요청은 없다.

단, 실제 통합 전에 다음 서버 설정값을 공유해야 한다.

- 실제 Backend IPv4 또는 Hostname
- `handheld_enabled=true` 설정 방법
- `device_id=1` Allowlist 설정 방법
- 시연 Scene `frame_id`
- Configured Position의 실제 meter 좌표와 활성 위치 선택 방법
- WebSocket 전체 URL 예: `ws://<server>:8000/handheld/control`

## 5. Embedded 다음 작업

1. BNO085 Component를 `handheld_jpeg_stream` 기반 통합 Firmware로 이식한다.
2. BNO085 최종 장착 방향을 고정하고 `q_mount`를 결정한다.
3. Button GPIO 두 개를 확정하고 30 ms Debounce를 구현한다.
4. UDP `9200` ControlTxTask를 구현한다.
5. 부팅마다 non-zero `session_id`를 생성한다.
6. `sample_seq`, `event_seq`, Event 3 Packet 반복을 구현한다.
7. `TIME_SYNCED=0`에서는 timestamp 0으로 송신한다.
8. Serializer 오류, UDP 송신 오류, Queue Drop을 계측한다.
9. Backend 실제 환경에서 Control 경로 통합 시험을 수행한다.

## 6. 중앙 문서

RFHC v1 Wire 규격은 Embedded와 Backend 사이에서 일치했다. 다음으로 `RFVisualizer-Docs/INTERFACE.md`의 Handheld Control 초안을 위 동결 규격으로 갱신해야 한다.

Graphics Camera 축 변환과 실제 `q_mount`가 확정되면 Quaternion 좌표축 절을 최종 보완한다. `CURRENT_STATUS.md`에는 Backend 구현 완료와 Embedded Serializer 공유 벡터 일치 상태를 기록하되, 실제 UDP 실물 통합 시험 완료로 표시하지 않는다.

