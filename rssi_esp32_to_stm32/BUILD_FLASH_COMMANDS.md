# ESP32 빌드 및 플래시 명령어

## 1. ESP-IDF 환경 활성화

새 PowerShell 창을 열 때마다 실행한다.

```powershell
$env:IDF_TOOLS_PATH = 'E:\esp\.espressif'
$env:IDF_PYTHON_ENV_PATH = 'E:\esp\.espressif\python_env\idf5.5_py3.11_env'
. 'E:\esp\v5.5.2\esp-idf\export.ps1'
idf.py --version
```

## 2. Node 1, 2, 4: 40 MHz

`main\node_config.h`에서 플래시할 보드에 맞게 `NODE_ID`를 `1`, `2`, `4` 중 하나로 변경한 후 각각 빌드하고 플래시한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\rssi_esp32_to_stm32\esp32_node
notepad main\node_config.h

idf.py build
idf.py -p COM포트 flash monitor
```

예시:

```powershell
idf.py -p COM5 flash monitor
```

## 3. Node 3: 26 MHz

먼저 `main\node_config.h`의 `NODE_ID`를 `3`으로 변경한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\rssi_esp32_to_stm32\esp32_node
notepad main\node_config.h
```

Node 3 전용 설정을 연다.

```powershell
idf.py -B build_node3_26 -D SDKCONFIG=sdkconfig.node3_26 menuconfig
```

`menuconfig`에서 Main XTAL frequency를 `26 MHz`로 선택하고 저장한 다음 빌드·플래시한다.

```powershell
idf.py -B build_node3_26 -D SDKCONFIG=sdkconfig.node3_26 build
idf.py -B build_node3_26 -D SDKCONFIG=sdkconfig.node3_26 -p COM포트 flash monitor
```

## 4. Gateway: 40 MHz

시간 동기화 비밀번호 파일이 아직 없다면 예제 파일을 복사한 후 실제 AP 비밀번호를 입력한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\rssi_esp32_to_stm32\esp32_gateway

Copy-Item main\time_credentials.example.h main\time_credentials.h
notepad main\time_credentials.h
```

게이트웨이를 빌드하고 플래시한다.

```powershell
idf.py -B build_gateway_40 build
idf.py -B build_gateway_40 -p COM포트 flash monitor
```

## 5. 정상 동작 확인

Node에서 다음 로그가 출력되어야 한다.

```text
Wi-Fi associated: channel=6
DHCP lease acquired: ip=192.168.x.x gateway=192.168.x.x
SNTP started after DHCP; server=pool.ntp.org
SNTP synchronized: timestamp_ms=...
rssi_node: raw=-XX filtered_x10=-XXX count=5 flags=0x0000
```

ESP-NOW 상태는 다음과 같아야 한다.

```text
send_ok=증가하는 값 send_fail=0
```

## 6. 시리얼 모니터 종료

```text
Ctrl + ]
```

## 7. STM32F107VCT6 빌드

실제 STM32 프로젝트는 저장소 루트의 `stm32_final_term`이다. 기존 `build/Debug`에는 이전 작업 경로의 CMake 캐시가 남아 있으므로 `build/Debug_current`를 사용한다.

```powershell
cd E:\RFVisualizer_Workspace\Embedded\stm32_final_term

$bundleRoot = 'C:\Users\NY\AppData\Local\stm32cube\bundles'
$env:PATH = "$bundleRoot\gnu-tools-for-stm32\14.3.1+st.2\bin;$bundleRoot\ninja\1.13.2+st.1\bin;$bundleRoot\cmake\4.3.1+st.1\bin;$env:PATH"
$cmake = "$bundleRoot\cmake\4.3.1+st.1\bin\cmake.exe"

& $cmake --preset Debug -B build\Debug_current
& $cmake --build build\Debug_current
```

빌드 산출물:

```text
stm32_final_term/build/Debug_current/stm32_final_term.elf
stm32_final_term/build/Debug_current/stm32_final_term.hex
stm32_final_term/build/Debug_current/stm32_final_term.bin
```
