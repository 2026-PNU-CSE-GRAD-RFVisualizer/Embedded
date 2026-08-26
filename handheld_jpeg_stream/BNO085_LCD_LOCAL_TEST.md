# BNO085 + NT35510 Local Integration Test

이 시험은 Wi-Fi와 JPEG server 없이 다음 두 경로를 ESP32-S3 한 대에서 동시에 실행한다.

```text
BNO085 I2C, 50 Hz → ImuTask → Quaternion 및 통계 로그
내장 JPEG 10장 → PSRAM cache → NT35510, 800×480, 10 FPS
```

핀 연결은 저장소 루트의 `HANDHELD_BNO085_LCD_PINMAP.md`를 따른다.

## 준비

- ESP32-S3-DevKitC-1-N16R8
- ESP-IDF 5.5.2 권장
- LCD와 BNO085 전원을 끈 상태에서 배선
- BNO085 I2C 주소 확인: 기본 설정은 `0x4B`

기존 JPEG용 `sdkconfig`와 `build`를 보존하기 위해 다음 전용 파일을 사용한다.

```text
sdkconfig.bno_lcd.defaults  테스트 기본값
sdkconfig.bno_lcd           테스트 로컬 설정, 최초 구성 시 생성
build-bno-lcd/              테스트 전용 빌드 디렉터리
```

## 설정과 빌드

ESP-IDF PowerShell에서 실행한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\handheld_jpeg_stream
.\local_bno_lcd_test.ps1 menuconfig
.\local_bno_lcd_test.ps1 build
.\local_bno_lcd_test.ps1 flash-monitor -Port COMx
```

센서가 `0x4A`를 사용하는 경우 Menuconfig에서 다음 값을 변경한다.

```text
RFVisualizer handheld JPEG stream
  → BNO085 7-bit I2C address
  → 0x4A
```

## 정상 로그

부팅 후 다음 로그가 모두 출력되어야 한다.

```text
LCD ready: landscape=800x480 ...
combined local BNO085 + LCD test started
bno085_lcd_test: pins: SDA=39 SCL=40 INT=41 RESET=42
bno085_hal: INT mode: 1 ms task polling
bno085_lcd_test: LCD/BNO I/O time-division enabled
lcd_gpio: transfer gate enabled; 96 rows x 5 DMA stripes
bno085: part=... version=... build=...
bno085: report enabled: GAME_ROTATION_VECTOR interval=20000 us
local_10fps: ... fps=... slow=.../10
bno085_lcd_test: GAME_ROTATION_VECTOR seq=... q=[...] norm=...
bno085_stats: samples=... rate=...Hz ...
```

## 합격 기준

- LCD 애니메이션이 깨짐 없이 연속 표시된다.
- LCD 측정값이 약 10 FPS이고 `slow=0/10`을 유지한다.
- BNO085 Product ID와 Quaternion report가 수신된다.
- BNO085 rate가 약 45~55 Hz다.
- 각 축으로 회전할 때 Quaternion이 연속적으로 변한다.
- Quaternion norm이 0.97~1.03 범위다.
- `non_finite=0`이며 복구 불가능한 I2C stall이 없다.
- 10분 Smoke Test 후 30분 연속 시험에서 재부팅이나 화면 정지가 없다.

LCD는 정상인데 BNO085 초기화가 실패하면 LCD 시험은 계속 실행된다. Monitor의 주소, 전원, I2C mode, pull-up, INT 및 RESET 오류를 먼저 확인한다.

통합 시험에서는 INT를 GPIO41에 그대로 연결하지만 GPIO ISR은 사용하지 않는다. `ImuTask`가 1 ms마다 INT 레벨을 폴링하므로, 부팅 로그에 `INT mode: 1 ms task polling`이 표시되는지 확인한다.

고정 배선에서 BNO085 I2C 전환이 LCD I80 전송과 겹치지 않도록 두 경로를 mutex로 시간 분할한다. LCD는 NT35510의 RGB666 component/GRAM write phase가 프레임 중간에 끊기지 않도록 한 프레임을 하나의 연속 DMA transaction으로 전송한다. BNO 태스크는 프레임 직후 대기 중인 FIFO 샘플을 처리한다. 통합 시험도 센서 출력 주기 20 ms(50 Hz)를 사용하며 BNO085 단독 시험 설정은 변경하지 않는다.

각 프레임은 전체 `800×480` window와 한 번의 `RAMWR` command를 사용한다. 프레임 중간에 CS를 해제하거나 BNO I2C 작업을 끼우지 않는다.

통합 시험의 LCD I80 clock은 긴 고정 배선에서 발생하는 간헐적인 색상 잡음을 줄이면서 10 FPS를 유지하기 위한 절충값인 12 MHz를 사용한다.

LCD I80 DMA 경로는 긴 16-bit Dupont 배선의 edge-rate 여유를 확보하기 위해 데이터선과 WR/DC/CS를 20 mA drive strength로 구동한다. 전체 프레임 DMA 완료는 최대 200 ms만 기다리며, 완료 interrupt가 누락되면 BNO와 공유한 I/O mutex를 해제하고 오류를 기록한 뒤 ESP32-S3를 재시작해 영구 정지를 방지한다.

통합 시험의 10개 로컬 Frame은 순차 재생하지 않고 시작 시점의 BNO Yaw를 중앙으로 자동 Recenter한 뒤 상대 Yaw에 따라 선택한다. 이는 네트워크와 Graphics 없이 센서에서 화면까지의 체감 반응 시간을 확인하는 진단 기능이며, 실제 3D Camera 렌더링을 대신하지 않는다.
