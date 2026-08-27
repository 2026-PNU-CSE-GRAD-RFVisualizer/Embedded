# Indexed zlib 10 FPS Handheld stream

The firmware accepts all three payload types in the existing 22-byte RFJF version
1 header:

| `flags` | Payload |
|---:|---|
| `0` | Existing baseline JPEG |
| `1` | zlib-wrapped 800x480 RGB332 frame |
| `2` | zlib-wrapped RGB565 palette plus 800x480 index frame |

For `flags=1`, decompression must produce exactly 384,000 bytes. Integer header
fields remain big-endian. For `flags=2`, decompression must produce exactly
384,512 bytes: 256 big-endian RGB565 entries (512 bytes), followed by 384,000
row-major palette indices. The palette is rebuilt for every frame. The receive
task keeps only the latest complete frame while the sink task inflates and
displays the previous frame.

Install the test server dependency and serve a directory of images at 10 FPS:

```powershell
python -m pip install -r tools/requirements.txt
python tools/rgb332_zlib_server.py main/test_animation/frames `
  --encoding rgb332-zlib --fps 10 --port 9102
```

The ESP32 local test option must be disabled, and its configured server host
must point to the PC running this command. Expected device log:

```text
I (...) indexed_zlib: displayed seq=... format=palette256, compressed=... B, inflate=... ms, draw=... ms, total=... ms
```

Ten FPS requires `inflate + draw` to remain below 100 ms on the device and the
network to deliver the next compressed frame within the same budget. Slow or
superseded frames are dropped instead of accumulating latency.

Palette256+zlib is implemented as the image-quality upgrade path. RGB332+zlib
remains supported for comparison and fallback, and `flags=0` JPEG remains a
compatibility and diagnostic path. Palette256 hardware color, throughput, and
300-second stability tests are still required before integration is marked
complete.
