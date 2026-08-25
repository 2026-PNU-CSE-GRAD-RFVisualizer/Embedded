# Network 파트 전달용 — Tailscale Handheld UDP 통합 시험 요청

아래 메시지를 Network/Backend 담당자에게 그대로 전달한다.

> 오늘 Tailscale을 통해 Handheld Control 연동 시험을 하고 싶습니다.
>
> 1. Backend Handheld 기능을 활성화해 주세요 (`handheld_enabled=true`).
> 2. RFHC UDP Listener를 Tailscale Interface에서도 접근 가능하게 `0.0.0.0:9200`으로 실행해 주세요.
> 3. 초기 허용 Device ID에 `1` (`handheld-01`)을 넣어 주세요.
> 4. Network 노트북의 Tailscale IPv4 (`100.x.x.x`) 또는 MagicDNS 이름과 Backend 실행 명령을 알려 주세요.
> 5. Windows 방화벽과 Tailnet ACL/Grants에서 UDP inbound `9200`을 허용해 주세요.
> 6. 수신 Packet의 CRC 결과, `session_id`, `sample_seq`, 수신률, Sequence Gap, Quaternion, stale 상태를 확인할 수 있는 로그를 켜 주세요.
> 7. 가능하면 WebSocket `/handheld/control` 상태를 확인하는 방법도 알려 주세요.
>
> 먼저 Embedded PC의 RFHC 모의 송신기로 Listener와 방화벽을 확인하고, 이후 ESP32-S3의 실제 50 Hz 송신을 연결하겠습니다.

## 양쪽이 함께 실행할 첫 시험

Network 파트가 Backend를 실행하고 Tailscale IPv4를 공유한 다음, Tailscale이 설치된 Embedded PC에서 실행한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded
python handheld_jpeg_stream\tools\rfhc_udp_sender.py --self-test
python handheld_jpeg_stream\tools\rfhc_udp_sender.py <NETWORK_PC_TAILSCALE_IP> --duration 10 --rate 50 --mode yaw
```

예:

```powershell
python handheld_jpeg_stream\tools\rfhc_udp_sender.py 100.x.x.x --duration 10 --rate 50 --mode yaw
```

## 첫 시험 합격 기준

- Embedded PC가 약 500 Packet을 10초 동안 송신한다.
- Backend가 `device_id=1` Packet을 정상 수신한다.
- CRC 및 Quaternion 검증 오류가 0이다.
- Backend 수신률이 약 50 Hz이고 Sequence Gap이 0이다.
- Yaw Quaternion 값이 약 4초 주기로 변한다.
- 송신 종료 약 500 ms 후 Backend가 Handheld를 stale로 전환한다.

이 시험은 PC 모의 송신 시험이다. 실제 BNO085 값의 ESP32-S3 UDP 송신은 Embedded의 `ControlTxTask` 구현 후 별도로 수행한다.

## 버튼 Event 중복 제거 시험

같은 `event_seq=1`과 Event Flag를 첫 3개 Packet에 반복한다. Backend Watch에는 Event가 한 번만 나타나고 `dedup_dropped`는 2 증가해야 한다.

```powershell
python handheld_jpeg_stream\tools\rfhc_udp_sender.py <NETWORK_PC_TAILSCALE_IP> --duration 5 --rate 50 --mode identity --event recenter
python handheld_jpeg_stream\tools\rfhc_udp_sender.py <NETWORK_PC_TAILSCALE_IP> --duration 5 --rate 50 --mode identity --event position
```

`position` 시험에서는 Backend의 활성 Configured Position이 `demo-1`로 설정돼 있어야 하며, `position_update` 메시지의 `accepted=true`와 등록 좌표를 함께 확인한다.

## 실제 ESP32-S3 시험 시 주의

ESP32-S3에는 Tailscale Client가 없으므로 Network 노트북의 `100.x.x.x` 주소로 직접 송신할 수 있다고 가정하지 않는다. 실제 기기 시험에서는 ESP32-S3가 같은 Wi-Fi의 Embedded 노트북 LAN IPv4로 UDP를 보내고, Embedded 노트북의 Application Proxy가 그 Packet을 Network 노트북의 Tailscale IPv4로 전달하는 구성을 우선 사용한다.

Embedded 노트북에서 Proxy 실행:

```powershell
python handheld_jpeg_stream\tools\rfhc_udp_proxy.py <NETWORK_PC_TAILSCALE_IP>
```

그다음 ESP32-S3의 Backend Host에는 Network 노트북의 Tailscale 주소가 아니라 Embedded 노트북의 Wi-Fi LAN IPv4를 설정한다.
