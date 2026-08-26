# Graphics/Network 전달용 — Handheld RGB332+zlib 10 FPS

## 결론

Handheld 영상의 10 FPS 경로는 **800×480 RGB332 raw frame을 zlib level 1로 압축**해서 전송한다.

JPEG 파일을 만든 뒤 zlib로 압축하는 방식이 아니다. Graphics의 RGB/RGBA 렌더 결과를 RGB332 384,000 byte로 먼저 변환하고, 그 byte 배열 자체를 zlib로 압축한다.

```text
Graphics render
  -> 800×480 RGB/RGBA readback
  -> RGB332 1 byte/pixel 변환
  -> zlib wrapper, level 1
  -> RFJF v1 flags=1
  -> Network Hub TCP 9101
  -> byte 그대로 relay
  -> viewer TCP 9102
  -> ESP32 zlib 해제 + LCD 출력
```

## Graphics 구현 계약

- 해상도: `800×480`
- 픽셀 배열: 왼쪽 위부터 row-major, stride/padding 없이 1 byte/pixel
- 압축 전 크기: 정확히 `800 × 480 = 384,000 byte`
- RGB332 변환:

```text
rgb332 = (R & 0xE0) | ((G >> 3) & 0x1C) | (B >> 6)
```

- 압축: 표준 **zlib wrapper** 사용, level `1`/`Z_BEST_SPEED`
- 사용 금지: raw DEFLATE, gzip, `zlib(JPEG bytes)`, PNG bytes
- 목표 송신률: `10 FPS` (100 ms마다 한 프레임)
- 처리나 송신이 밀리면 대기열을 늘리지 말고 오래된 프레임을 버려 최신 프레임 우선
- Graphics 송신 대상: Network Hub ingest `100.85.80.106:9101/TCP`

OpenGL의 `glReadPixels` 결과가 bottom-left origin이면 상하 반전해서 보낸다. 실제 readback이 `BGR/BGRA`라면 R/B를 바로 사용하지 말고 올바른 채널로 RGB332를 만든다. 이 두 항목은 색상 이상·상하 반전 방지를 위해 test pattern으로 확인한다.

## RFJF v1 Frame

Header는 기존 22 byte를 유지한다. 모든 다중 byte 정수는 network byte order(big-endian)다.

| Offset | 크기 | 필드 | 값 |
|---:|---:|---|---|
| 0 | 4 | magic | `RFJF`, `0x52464A46` |
| 4 | 1 | version | `1` |
| 5 | 1 | flags | `1` = RGB332+zlib |
| 6 | 4 | seq | 프레임마다 증가하는 uint32 |
| 10 | 8 | timestamp_ms | Unix epoch milliseconds, uint64 |
| 18 | 4 | length | 압축 payload 길이, uint32 |
| 22 | length | payload | RGB332 384,000 byte의 zlib 결과 |

TCP에서는 header와 payload를 각각 한 번의 `send()`로 모두 전송했다고 가정하면 안 된다. `send_all` 반복으로 끝까지 보내야 한다. 연결이 끊어지면 재연결한다.

## C++ 핵심 예시

```cpp
std::vector<uint8_t> rgb332(800 * 480);
for (size_t i = 0; i < rgb332.size(); ++i) {
    const uint8_t r = rgb[i * 3 + 0];
    const uint8_t g = rgb[i * 3 + 1];
    const uint8_t b = rgb[i * 3 + 2];
    rgb332[i] = (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
}

uLongf compressed_len = compressBound(rgb332.size());
std::vector<uint8_t> compressed(compressed_len);
int rc = compress2(compressed.data(), &compressed_len,
                   rgb332.data(), rgb332.size(), Z_BEST_SPEED);
if (rc != Z_OK) {
    // 이 프레임은 버리고 오류를 계측한다.
}
compressed.resize(compressed_len);
```

송신 코드에는 최소한 `seq`, 원본 크기, 압축 크기, 압축 시간, 송신 시간, drop/reconnect 횟수를 로그로 남긴다. 렌더 루프 정지를 줄이려면 readback/PBO와 압축·송신 worker를 분리하고, worker 입력 슬롯은 1~2개만 두어 최신 프레임으로 교체한다.

## Network 파트 요구사항

Network relay는 `flags=1`을 JPEG로 검사·디코딩·재압축하지 않는다. 완전한 `22-byte header + length-byte payload`를 읽고 viewer `9102/TCP`로 byte 그대로 중계한다. 느린 viewer 때문에 producer 프레임이 계속 쌓이지 않도록 최신 프레임 정책을 유지한다.

## 통합 시험 순서와 합격 기준

1. Graphics가 빨강/초록/파랑/흰색 블록과 위/아래 글자가 있는 800×480 test pattern을 `flags=1`로 보낸다.
2. Network 로그에서 ingest와 viewer 전송 frame 수, invalid 0, TCP error 0을 확인한다.
3. ESP32에서 `received ... flags=1`과 아래 표시 로그를 확인한다.
4. 색 순서, 상하 방향, 줄 밀림이 없는지 확인한다.
5. 실제 움직이는 렌더 화면으로 30초 이상 실행하고 표시 FPS와 drop을 확인한다.

```text
rgb332_zlib: displayed seq=..., compressed=... B, inflate=... ms, draw=... ms, total=... ms
rgb332_zlib: 10-frame window=... ms, displayed fps=...
```

목표는 ESP32의 `inflate + draw < 100 ms`, 10-frame window 표시율 약 `10 FPS`다. Sequence가 일부 건너뛰더라도 최신 화면을 유지하면서 displayed FPS가 목표에 도달하면 정상적인 frame drop일 수 있다.

## 현재 상태와 문서 주의

- Embedded firmware에는 `flags=1`, zlib header 해제, 출력 크기 384,000 byte 검증, RGB332 LCD 출력이 구현돼 있다.
- Graphics GitHub `main`에는 RFJF/JPEG 송신 코드가 검색되지 않아 실제 Graphics 작업본의 파일명과 RGB/BGR readback 형식은 담당자가 확인해야 한다.
- 중앙 `RFVisualizer-Docs`의 `flags=0 JPEG 공식`, `flags=1 실험용` 표기는 팀의 최신 RGB332+zlib 결정과 충돌한다. 통합 성공 후 `INTERFACE.md`, `CURRENT_STATUS.md`, `graphics/GRAPHICS.md`, `embedded/EMBEDDED.md`를 함께 갱신해야 한다.
