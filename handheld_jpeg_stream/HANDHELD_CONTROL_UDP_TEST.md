# BNO085 → RFHC UDP 실기기 시험

이 시험은 실제 BNO085 Quaternion을 ESP32-S3에서 RFHC v1 52-byte Packet으로 직렬화해 50 Hz로 전송한다. 버튼 Event, `q_mount` 최종 보정 및 JPEG 수신은 첫 시험 범위에서 제외한다.

Tailscale 구성에서는 ESP32-S3가 Tailscale 주소로 직접 전송하지 않는다.

```text
BNO085 → ESP32-S3
       → Wi-Fi UDP 192.168.0.3:9200
       → Embedded 노트북 Proxy
       → Tailscale UDP 100.85.80.106:9200
       → Network Backend
```

`192.168.0.3`과 `100.85.80.106`은 이번 시험에서 확인한 주소이며, 네트워크가 바뀌면 다시 확인한다. 주소와 Wi-Fi 비밀번호는 로컬 `sdkconfig.control`에만 저장하고 Commit하지 않는다.

## 1. Embedded 노트북 Proxy

일반 PowerShell에서 실행한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\handheld_jpeg_stream
python .\tools\rfhc_udp_proxy.py 100.85.80.106
```

Quaternion과 JPEG를 동시에 중계하는 종단 시험에서는 위 UDP 전용 Proxy 대신 다음 통합 Proxy를 실행한다.

```powershell
python .\tools\handheld_proxy.py --hub 100.85.80.106
```

통합 Proxy는 로컬 UDP `9200`을 Hub UDP `9200`으로 전달하고, 로컬 TCP `9102`와 Hub viewer TCP `9102`를 연결한다. 두 Proxy를 동시에 실행하면 UDP `9200` Port가 충돌하므로 하나만 실행한다.

Windows 방화벽에서 Python의 Private Network UDP 9200 inbound를 허용한다.

## 2. ESP32-S3 설정과 빌드

ESP-IDF 5.5 PowerShell을 별도로 열고 실행한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\handheld_jpeg_stream
.\control_udp_test.ps1 menuconfig
```

다음 값을 설정한다.

```text
RFVisualizer handheld JPEG stream
  Wi-Fi SSID                         = 바부바부쟝
  Wi-Fi password                     = 로컬 비밀번호
  Send BNO085 Handheld Control       = Yes
  Backend or proxy IPv4/hostname     = 192.168.0.3
  Handheld Control UDP port          = 9200
  Handheld numeric device ID         = 1
  Image relay host                   = 빈 값
```

그다음 빌드하고 Flash한다.

```powershell
.\control_udp_test.ps1 build
.\control_udp_test.ps1 flash-monitor -Port COMx
```

## 3. 정상 로그

```text
BNO085 service and LCD I/O gate started
waiting for Wi-Fi
Handheld Control UDP started
RFHC v1 target=192.168.0.3:9200 device_id=1 ... rate=50Hz
UDP path ready
stats: sent=... send_errors=0 serialize_errors=0 no_sample=...
```

Proxy에는 다음 통계가 표시돼야 한다.

```text
stats: received=..., forwarded=..., invalid=0, send_errors=0
```

Backend 합격 기준:

- 수신률 약 50 Hz
- CRC 오류 0
- Sequence Gap 0
- 센서를 돌리면 Quaternion 값이 연속적으로 변함
- 송신 중단 500 ms 후 stale

현재 `q_mount`는 초기 통합용 identity로 처리한다. Packet 도달과 Quaternion 변화가 확인된 뒤 실제 센서 장착 방향을 기준으로 Yaw·Pitch·Roll 90도 시험을 수행해 최종 축 변환을 확정한다.
