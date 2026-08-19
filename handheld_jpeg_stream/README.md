# ESP32-S3 Handheld JPEG Stream Client

`Network-Backend-Article/image_relay`의 viewer 포트에서 JPEG 프레임을 받는
독립 ESP-IDF 예제다. 대상 보드는 `ESP32-S3-DEVKITC-1-N8R8`이다.

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
  flags     uint8   0 (reserved)
  seq       uint32  frame sequence
  ts_ms     uint64  Unix epoch milliseconds
  length    uint32  following JPEG byte count
payload     byte[length]
```

서버 규격 상한은 8 MiB지만 N8R8 보드에서 그대로 할당하면 위험하다. 이
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

`sdkconfig.defaults`에는 N8R8의 8 MiB Flash/8 MiB PSRAM 설정만 들어 있다.

## 구현상 안전장치

- TCP `recv()`가 header/payload를 조각내도 정확한 길이까지 반복 수신
- magic/version/length 및 JPEG SOI(`FFD8`)/EOI(`FFD9`) 검증
- 네트워크 byte order를 byte 단위로 해석하여 struct padding 문제 제거
- PSRAM JPEG buffer A/B와 1칸 ready queue 사용
- 디코더가 느리면 대기 중인 오래된 frame을 버리고 최신 frame 유지
- sequence gap, stale drop, invalid JPEG, reconnect 통계 유지
- socket receive timeout과 지수형 reconnect backoff

## JPEG 디코더와 LCD 출력

Espressif `esp_jpeg` 1.3.1을 사용하며 ESP32-S3의 ROM TJpgDec 경로를 이용한다.
서버가 보내는 JPEG는 progressive가 아닌 baseline JPEG여야 하며 정확히
800x480이어야 한다. 다른 크기는 잘못된 메모리 배치를 막기 위해 거부한다.

```text
JPEG PSRAM buffer A/B
  → esp_jpeg decoder
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
