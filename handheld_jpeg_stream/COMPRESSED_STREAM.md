# RGB332 zlib network test

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

This flag assignment is an embedded-side experimental extension. Synchronize
it with the central `INTERFACE.md` and the production image relay before using
it as the final cross-repository contract.
