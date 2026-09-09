# ESP32-S3 Handheld JPEG Stream Client

`Network-Backend-Article/image_relay`의 viewer 포트에서 JPEG 프레임을 받는
독립 ESP-IDF 예제다. 현재 대상 보드는 `ESP32-S3-DEVKITC-1-N16R8`이다.

현재 구현 범위는 **TCP 수신 → 프레임 검증 → JPEG 디코딩 → NT35510 출력**
까지다. GPIO 방식 LCD 드라이버와 보드 설정은 이 프로젝트 내부에 독립적으로
포함한다. 부팅 시 전체 화면을 빨강, 초록, 파랑 순서로 확인한 뒤 검정 화면에서
서버 JPEG를 기다린다.

## 전체 연결

```text
Graphics/SIBR
  JPEG 800x480 생성
       │ TCP :9101
       ▼
Network-Backend-Article/image_relay
       │ TCP :9102
       ▼
ESP32-S3 handheld_jpeg_stream
  header 검증 → JPEG PSRAM A/B → RGB565 PSRAM → NT35510
```

측정 백엔드의 HTTP `:8000`, MQTT `:1883`, 실시간 RSSI WebSocket `/frames`와
JPEG 스트림은 서로 다른 경로다. ESP32-S3는 이미지 중계 서버의 **viewer
포트 9102**에 TCP client로 접속한다.

## 와이어 규격

참고 저장소 `image_relay/protocol.py`와 동일하며 모든 정수는 big-endian이다.

```text
22-byte header
  magic     uint32  0x52464A46 ('RFJF')
  version   uint8   1
  flags     uint8   0=JPEG, 1=RGB332+zlib, 2=palette256+zlib
  seq       uint32  frame sequence
  ts_ms     uint64  Unix epoch milliseconds
  length    uint32  following encoded payload byte count
payload     byte[length]
```

서버 규격 상한은 8 MiB지만 N16R8 보드의 PSRAM도 8 MiB이므로 그대로 할당하면 위험하다. 이
예제는 기본 512 KiB PSRAM 버퍼 2개를 사용하며 더 큰 프레임은 연결을 끊고
재접속한다. Graphics 쪽은 800x480 JPEG, quality 60~75를 우선 사용하고 실제
최대 크기를 계측한 뒤 `JPEG_STREAM_MAX_FRAME_BYTES`를 조정한다.

## 서버에서 이미지를 받는 순서

1. `Network-Backend-Article`에서 중계 서버를 먼저 실행한다.

   ```powershell
   python -m image_relay --ingest-port 9101 --viewer-port 9102
   ```

2. Graphics는 `<서버 IP>:9101`로 22-byte header와 JPEG를 보낸다.
3. ESP32-S3는 같은 Wi-Fi에서 `<서버 IP>:9102`로 접속한다.
4. Windows 방화벽에서 TCP 9102 inbound를 허용한다.
5. 서버가 끊기거나 timeout이 나면 클라이언트는 1초 뒤 재접속한다.

서버만 먼저 검증하려면 참고 저장소에서 다음을 실행한다.

```powershell
python -m image_relay.fake_viewer --save-dir frames_out
python -m image_relay.fake_producer --fps 10 --count 40
```

## 설정과 빌드

실제 SSID, 비밀번호, 서버 IP는 commit하지 않고 로컬 `sdkconfig`에만 둔다.

```powershell
idf.py set-target esp32s3
idf.py menuconfig
# RFVisualizer handheld JPEG stream 메뉴에서 Wi-Fi와 서버 주소 설정
idf.py build
idf.py -p COMx flash monitor
```

`sdkconfig.defaults`에는 N16R8의 16 MiB Flash/8 MiB PSRAM 설정이 들어 있다.

## 구현상 안전장치

- TCP `recv()`가 header/payload를 조각내도 정확한 길이까지 반복 수신
- magic/version/length 및 형식별 payload 검증
- `flags=1` 해제 결과 384,000 byte, `flags=2` 해제 결과 384,512 byte 검증
- `flags=2`의 RGB565 big-endian 팔레트를 매 프레임 LCD DMA lookup으로 변환
- 네트워크 byte order를 byte 단위로 해석하여 struct padding 문제 제거
- PSRAM JPEG buffer A/B와 1칸 ready queue 사용
- 디코더가 느리면 대기 중인 오래된 frame을 버리고 최신 frame 유지
- sequence gap, stale drop, invalid JPEG, reconnect 통계 유지
- socket receive timeout과 지수형 reconnect backoff

## JPEG 디코더와 LCD 출력

기본 디코더는 `esp_new_jpeg` 1.0.2이며 RGB565 little-endian 전체 프레임을 PSRAM에 출력한다.
`HANDHELD_NEW_JPEG=n`으로 `esp_jpeg` 1.3.1과 비교할 수 있다. 현재 TJpgDec 설정은 ROM을 사용하지 않는다.
Baseline JPEG만 지원하며, 800x480보다 작은 영상은 기존처럼 확대하고 초과 해상도는 거부한다.

```text
JPEG PSRAM buffer A/B
  → esp_new_jpeg decoder (TJpgDec 비교 옵션 제공)
  → RGB565 full-frame buffer 1개 (768,000 bytes)
  → NT35510 GRAM
```

패널 표시 방향은 800x480 landscape다. 수신 JPEG A/B 1 MiB와 RGB565
768,000 bytes를 PSRAM에서 사용한다. 디코드와 검증 완료된 GPIO LCD 출력이
source fps보다 느리면 sequence gap/stale drop이 발생하는 것이 정상이며,
오래된 영상 지연이 누적되는 것보다 최신 화면을 우선한다.

정상 출력 시 monitor에 다음 로그가 반복된다.

```text
I (...) jpeg_lcd: displayed seq=..., jpeg=... B, decode=... ms, draw=... ms
```

`resolution ...; expected 800x480`이면 Graphics 출력 해상도를 바꾸고,
`progressive JPEG unsupported`이면 baseline JPEG로 인코딩한다.

## Host protocol test

ESP-IDF 없이 순수 C parser만 확인할 수 있다.

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -I main main/jpeg_stream_protocol.c test/test_protocol_host.c `
  -o test_protocol_host.exe
./test_protocol_host.exe
```

현재 환경에서는 ESP-IDF가 PATH에 없으면 firmware build를 실행할 수 없다.
Host test는 header byte order와 magic/version/length 거부를 검증한다.

Handheld Control RFHC v1 Serializer와 Backend 공유 벡터는 다음 명령으로
검증한다.

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -I main main/handheld_control_protocol.c `
  test/test_handheld_control_protocol_host.c `
  -o test_handheld_control_protocol_host.exe
./test_handheld_control_protocol_host.exe
```

정상 결과:

```text
7/7 RFHC serializer tests passed; backend vector and button flags matched
```

RFHC 송신을 켜면 GPIO17 텔레포트 버튼과 GPIO19 Height-cycle 버튼을 내부
Pull-up의 active-low 입력으로 읽는다. 기존 50 Hz Control Task가 25 ms
소프트웨어 debounce를 수행하며, 매 Packet에 현재 눌림 상태를 각각 `flags`
bit1·bit2로 보낸다. `event_seq`는 항상 0이고 별도 버튼 Task나 Event 반복
송신은 없다. GPIO19는 Native USB D-와 공유되므로 Flash/Monitor에는
USB-to-UART 포트를 사용한다.


## JPEG 성능 비교 빌드 (2026-09-09)

이번 변경은 성능 측정용 구현이다. 8/10/12 FPS 달성, LCD 색상 안정성,
BNO085 50 Hz 유지 여부는 새 펌웨어를 Flash한 실물 시험으로 확인해야 한다.
RFJF/Control UDP 규격, 배선, LCD 초기화, 16 MHz 클록은 유지한다.

기본값:

- `HANDHELD_NEW_JPEG=y`: esp_new_jpeg 1.0.2. 전체 프레임 디코드 후 LCD 출력.
- `HANDHELD_LCD_PING_PONG=y`, `HANDHELD_LCD_STRIPE_ROWS=16`:
  38,400바이트 내부 DMA 버퍼 두 개. 기존 32행 단일 버퍼와 합계 크기가 같다.
- RGB565, RGB332, palette256 출력 모두 다음 stripe packing과 이전 DMA를 중첩한다.
- 미완료 DMA는 한 개만 유지한다. Queue depth는 1이며, 다음 window 명령 전에 완료를 확인한다.
- BNO mutex는 송신한 task가 완료 확인 후 해제한다. 다음 stripe packing 동안에도 잠시
  유지되므로 BNO 샘플 간격을 실물 비교해야 한다. DMA timeout은 재시작으로 복구한다.

`idf.py menuconfig`에서 다음 순서로 동일한 800x480 JPEG 시퀀스를 비교한다.

| 비교 | NEW_JPEG | LCD_PING_PONG | STRIPE_ROWS |
|---|---|---|---:|
| 계측 포함 기존 경로 | n | n | 32 |
| 디코더만 변경 | y | n | 32 |
| 기본 개선 경로 | y | y | 16 |

`HANDHELD_RGB565_BENCHMARK=y`는 내장 JPEG 하나를 한 번 디코드한 뒤,
동일한 RGB565 LCD 경로를 대기 주기 없이 반복한다. Wi-Fi는 시작하지 않는다.
`HANDHELD_BNO085_SERVICE`로 BNO 부하를 독립 비교할 수 있다. 네트워크 시험에서도
이 옵션을 사용하면 BNO ON / UDP OFF가 가능하다. Control UDP ON 시험은 기존 옵션을 사용한다.
벤치마크를 마친 뒤 `HANDHELD_RGB565_BENCHMARK=n`으로 되돌려 스트리밍을 빌드한다.

로그 해석:

- `stream`: 수신 FPS, 표시 성공 FPS, 해당 구간 stale drop/sequence gap/실패 횟수.
- `JPEG`: 헤더 분석·디코더 준비/해제·확대 포함 decode 평균/최대, draw 평균/최대.
  기존 로그의 decode(디코더 호출만 측정)와 측정 범위가 다르다.
- `LCD`: 전체 평균/최대, pack, gate 대기, window command, 잔여 DMA wait,
  DMA 요청부터 완료 callback까지의 `dma_span`. 단위는 ms, 구간별 평균이다.
- `dma_span`은 packing과 겹치므로 다른 항목과 단순 합산하지 않는다.
  `LCD fps`는 호출 간 간격을 포함하므로 네트워크 모드에서 LCD 단독 최대 속도가 아니다.
- 정상 로그는 약 1초 간격으로 집계한다. 프레임별 수신/표시 로그는 DEBUG 레벨이다.
  수신이 멈추면 표시 task 기반 통계도 멈추므로 reconnect/error 로그를 함께 확인한다.

각 조건에서 워밍업 후 300초 이상 수집한다. 화면 색상/가로 줄, BNO sample rate와
sequence loss/I2C error, DMA timeout을 함께 기록한다. 작은 JPEG 크기가 항상 빠른
디코드를 의미하지 않으므로 동일 소스와 subsampling을 사용한다.
아직 block decode와 LCD 전송의 중첩은 적용하지 않았다. 디코드와 전체 draw는 직렬이며,
다음 단계 필요성은 이번 실측으로 판단한다.

검증 기록 (2026-09-09): ESP-IDF 5.5.2 / ESP32-S3 기본 스트리밍 전체 Build 성공.
App 854,848바이트, App partition 여유 44%. RFJF Host Test 5/5,
RFHC Host Test 7/7 통과. 기존 디코더+직렬 DMA, RGB565 benchmark+BNO,
Control UDP 각 구성의 주요 C 파일 4개는 `-Werror -fsyntax-only` 검사 통과
(각 구성의 별도 전체 링크/Flash 시험을 의미하지 않음). 실물 FPS·색상·BNO 동시 안정성 미검증.


### RGB565 packing 최적화 (2026-09-10)

픽셀마다 수행하던 R/G/B 확장식을 두 개의 256-entry 변환표 조회로 교체했다.
표는 내부 DRAM 2,048바이트를 사용하며, 입력 상위/하위 바이트가 기여하는
RGB666 비트를 각각 저장한다. 해상도·색상 양자화·픽셀 순서·패널 전송량은
변하지 않는다. RGB332/palette256의 기존 lookup 경로도 그대로 유지한다.

`test/test_rgb666_pack_host.c`는 기존 변환식을 독립 기준으로 사용해 모든
65,536가지 RGB565 색상을 픽셀 쌍의 양쪽 위치에서 검증하고, 보색 쌍과
100,000개 추가 쌍 및 출력 버퍼 경계도 검사한다. 모든 결과는 비트 단위로 일치했다.

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Werror -I main test/test_rgb666_pack_host.c -o test_rgb666_pack.exe
.\test_rgb666_pack.exe
```

실물 속도 향상 폭은 미측정이다. 같은 영상·BNO/UDP 조건에서 이전 `pack` 약
62 ms와 Flash 후 로그를 비교한다. 색상 변환의 수학적 동일성과 실물 속도는 별도 검증이다.
