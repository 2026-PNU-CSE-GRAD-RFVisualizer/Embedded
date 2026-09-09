from pathlib import Path
p=Path('handheld_jpeg_stream/README.md');s=p.read_text(encoding='utf-8');s=s.replace('Espressif `esp_jpeg` 1.3.1을 사용하며 ESP32-S3의 ROM TJpgDec 경로를 이용한다.\n서버가 보내는 JPEG는 progressive가 아닌 baseline JPEG여야 하며 정확히\n800x480이어야 한다. 다른 크기는 잘못된 메모리 배치를 막기 위해 거부한다.', '기본 디코더는 `esp_new_jpeg` 1.0.2이며 RGB565 little-endian 전체 프레임을 PSRAM에 출력한다.\n`HANDHELD_NEW_JPEG=n`으로 `esp_jpeg` 1.3.1과 비교할 수 있다. 현재 TJpgDec 설정은 ROM을 사용하지 않는다.\nBaseline JPEG만 지원하며, 800x480보다 작은 영상은 기존처럼 확대하고 초과 해상도는 거부한다.');s=s.replace('  → esp_jpeg decoder','  → esp_new_jpeg decoder (TJpgDec 비교 옵션 제공)');s+='''

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
''';p.write_text(s,encoding='utf-8')
