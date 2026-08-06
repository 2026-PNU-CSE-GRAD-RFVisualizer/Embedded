# ESP32-S3 + BNO085 standalone test

Standalone ESP-IDF project for validating a BNO085 before integrating it with
the handheld LCD, buttons, network, or JPEG pipeline.

## Target

- Board expected by `sdkconfig.defaults`: ESP32-S3-DevKitC-1-N16R8
- ESP-IDF: tested at build time with v5.5.2
- Transport: I2C with active-low `INT` and `RESET`
- Driver: official CEVA SH-2 source pinned in `components/ceva_sh2/VERSION.txt`
- Default report: Game Rotation Vector, 50 Hz
- Default I2C: address `0x4A`, 100 kHz

The repository's older handheld documentation also mentions an N8R8 board.
Check the module marking before flashing. If the physical board is N8R8, change
the flash size to 8 MB with `idf.py menuconfig` before use.

## Wiring

| BNO085 breakout | ESP32-S3 | DevKitC-1 header | Purpose |
|---|---:|---:|---|
| SDA | GPIO39 | J3-9 | I2C data |
| SCL | GPIO40 | J3-8 | I2C clock |
| INT / INTN | GPIO41 | J3-7 | Active-low data-ready interrupt |
| RST / RESETN | GPIO42 | J3-6 | Active-low hardware reset |
| GND | GND | J3-1, J3-21, or J3-22 | Common ground |
| 3V3/VCC | 3V3 only after checking the breakout specification | J1-1 or J1-2 | Sensor power |

GPIO39-42 do not overlap the existing LCD test's GPIO0-18, GPIO21, GPIO47,
and GPIO48 assignment.

### Power and mode warnings

- Do not connect an unknown `VIN` pin to 5 V until the exact breakout model's
  regulator and level-shifter specification has been checked.
- I2C, INT, and RESET logic must be 3.3 V compatible.
- SDA and SCL need pull-ups to 3.3 V. Many breakouts include them; if yours
  does not, add approximately 2.2-4.7 kohm pull-ups.
- The BNO085 must be strapped for I2C mode. If PS0/PS1 are exposed, consult the
  breakout documentation and set both for I2C before reset.
- SA0 selects `0x4A` or `0x4B`. Change the address in `idf.py menuconfig` if
  the default does not match the board.
- Keep the first test wires short and leave the LCD disconnected.

## Build and flash

Open an ESP-IDF terminal, then run:

```powershell
cd E:\RFVisualizer_Workspace\Embedded\handheld_bno085_test
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the board's port. Configuration options are under
`BNO085 test configuration` in:

```powershell
idf.py menuconfig
```

The report can be switched between Game Rotation Vector and Rotation Vector.
The I2C address, I2C speed, and report interval are also configurable there.

## Expected boot log

```text
I (...) bno085_test: handheld_bno085_test start
I (...) bno085_hal: I2C ready: address=0x4A speed=100000 ...
I (...) bno085: part=... version=... build=...
I (...) bno085: report enabled: GAME_ROTATION_VECTOR interval=20000 us
I (...) bno085_test: GAME_ROTATION_VECTOR seq=5 q=[...] norm=1.00000 ...
I (...) bno085_stats: samples=... rate=49...Hz ...
```

An I2C ACK alone is not a pass. Product ID and valid quaternion reports must
also be received.

## Test sequence

1. Run at 100 kHz with Game Rotation Vector at 50 Hz.
2. Confirm Product ID is printed once per boot.
3. Keep the sensor still, then rotate it 90 and 180 degrees around each axis.
4. Confirm quaternion values change smoothly, remain finite, and have norm
   between 0.97 and 1.03.
5. Run Game Rotation Vector for 30 minutes at 100 kHz.
6. Switch to Rotation Vector and repeat the motion test.
7. Compare a 30-minute run at 400 kHz.
8. Perform ten power cycles, ten ESP32 resets, and ten BNO085 RESET cycles.
9. Finish with a one-hour continuous run.

Expected stable values are approximately 45-55 Hz, zero non-finite samples,
zero unrecoverable I2C errors, and no permanently stalled report stream.

## Statistics

Every five seconds the application prints:

- sample count and effective report rate
- sensor sequence loss
- I2C errors and timeouts
- short reads and invalid SHTP packets
- unexpected resets and recoveries
- invalid quaternion norms and non-finite values
- minimum and maximum sample intervals

## Implementation notes

- The GPIO ISR only records that data is ready. I2C access and SH-2 parsing run
  from the application task.
- BNO08x I2C transfers are read as a four-byte header followed by a full read
  that starts at the header again.
- SHTP packet length is checked against the fixed CEVA 1024-byte input buffer.
- After repeated I2C errors or a one-second report stall, the application closes
  SH-2, recreates the I2C bus, hardware-resets the BNO085, rechecks Product ID,
  and restores the selected report.

## Test result record

```text
Test Date:
ESP-IDF Version:
ESP32-S3 Board:
BNO085 Breakout:
CEVA SH-2 Commit: b514b1e2586ddc195e553dac89fc94c637b25298
I2C Address:
I2C Speed:
Report Type:
Report Interval:
Test Duration:
Sample Count:
Effective Rate:
Sequence Loss:
I2C Errors:
Timeouts:
Short Reads:
Invalid Packets:
Unexpected Resets:
Recoveries:
Norm Errors:
Non-finite Samples:
Result: PASS / FAIL
```
