from pathlib import Path
p=Path('handheld_jpeg_stream/README.md')
with p.open('a',encoding='utf-8') as f:f.write('''
검증 기록 (2026-09-09): ESP-IDF 5.5.2 / ESP32-S3 기본 스트리밍 전체 Build 성공.
App 854,848바이트, App partition 여유 44%. RFJF Host Test 5/5,
RFHC Host Test 7/7 통과. 기존 디코더+직렬 DMA, RGB565 benchmark+BNO,
Control UDP 각 구성의 주요 C 파일 4개는 `-Werror -fsyntax-only` 검사 통과
(각 구성의 별도 전체 링크/Flash 시험을 의미하지 않음). 실물 FPS·색상·BNO 동시 안정성 미검증.
''')
# Prepare exact, small central-document additions; preserve existing sections.
script=Path('.codex-build/update_central.py')
script.write_text('''from pathlib import Path
root=Path(r"E:/RFVisualizer_Workspace/RFVisualizer-Docs")
p=root/"CURRENT_STATUS.md"
s=p.read_text(encoding="utf-8")
anchor="### 임베디드\\n"
addition="""
- **2026-09-09 JPEG 성능 비교 빌드**: `esp_new_jpeg` 1.0.2, 내부 DMA 16행 버퍼 2개로
  packing/전송 중첩, 구간별 집계 로그·RGB565 단독 benchmark·독립 BNO 서비스 옵션 추가.
  ESP-IDF 5.5.2 / ESP32-S3 전체 빌드와 RFJF 5/5·RFHC 7/7 Host Test 통과.
  새 펌웨어 Flash, FPS 개선, 색상·BNO 동시 안정성은 아직 미검증.
"""
assert anchor in s
if "2026-09-09 JPEG 성능 비교 빌드" not in s:
    p.write_text(s.replace(anchor,anchor+addition,1),encoding="utf-8")
p=root/"embedded/EMBEDDED.md"
s=p.read_text(encoding="utf-8")
anchor="전체 RGB565 Triple Buffer를 기본 전략으로 사용하지 않는다."
addition="""

2026-09-09 성능 비교 구현은 JPEG에 `esp_new_jpeg` 1.0.2를 기본 사용하고
RGB565 전체 프레임 768,000바이트를 PSRAM에 유지한다. TJpgDec 비교 옵션도 유지한다.
LCD는 내부 DMA 16행 버퍼 2개(합계 76,800바이트)를 번갈아 사용하여 다음 stripe
packing과 이전 전송을 중첩한다. 한 번에 미완료 DMA는 하나이며, window 명령 전에
완료를 확인한다. 16 MHz 클록·RGB666 순서·BNO gate를 유지하고 timeout은 재시작한다.
RGB332/palette256에도 같은 출력 경로를 사용한다. RGB565 무제한 로컬 benchmark와
UDP와 독립된 BNO 서비스 옵션으로 비교하며, block decode 중첩은 아직 적용하지 않았다.
기본 전체 빌드 완료, 새 경로의 실물 FPS·화면·BNO 안정성은 미검증이다.
"""
assert anchor in s
if "2026-09-09 성능 비교 구현" not in s:
    p.write_text(s.replace(anchor,anchor+addition,1),encoding="utf-8")
print("Updated CURRENT_STATUS.md and embedded/EMBEDDED.md; no interface changes.")
''',encoding='utf-8')
