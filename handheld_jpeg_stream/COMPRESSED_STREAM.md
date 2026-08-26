# RGB332+zlib 10 FPS Handheld stream

The firmware accepts both payload types in the existing 22-byte RFJF version
1 header:

| `flags` | Payload |
|---:|---|
| `0` | Existing baseline JPEG |
| `1` | zlib-wrapped 800x480 RGB332 frame |

For `flags=1`, decompression must produce exactly 384,000 bytes. Integer header
fields remain big-endian. The receive task keeps only the latest complete
frame while the sink task inflates and displays the previous frame.

Install the test server dependency and serve a directory of images at 10 FPS:

```powershell
python -m pip install -r tools/requirements.txt
python tools/rgb332_zlib_server.py main/test_animation/frames `
  --encoding rgb332-zlib --fps 10 --port 9102
```

The ESP32 local test option must be disabled, and its configured server host
must point to the PC running this command. Expected device log:

```text
I (...) rgb332_zlib: displayed seq=..., compressed=... B, inflate=... ms, draw=... ms, total=... ms
```

Ten FPS requires `inflate + draw` to remain below 100 ms on the device and the
network to deliver the next compressed frame within the same budget. Slow or
superseded frames are dropped instead of accumulating latency.

RGB332+zlib is the selected 10 FPS Handheld integration path. `flags=0` JPEG
remains supported as a compatibility and diagnostic fallback, but full-size
800x480 JPEG decoding is not fast enough for the target display rate on the
current ESP32-S3 implementation.

The central `INTERFACE.md` and `CURRENT_STATUS.md` still describe `flags=1` as
experimental and must be synchronized with this decision before the final
cross-repository integration is marked complete.
