# STM32 Receiver Module

This folder contains board-portable C modules for the STM32 side.

Files:

- `rssi_line_parser.*`: byte stream and line parser for `$RSSI` messages.
- `rssi_preprocessor.*`: node table, timeout tracking, sequence loss tracking.
- `mqtt_payload.*`: MQTT-ready JSON payload formatter.
- `uart_rx_ring.*`: single-producer/single-consumer UART RX ring buffer.
- `stm32_uart_receiver_example.c`: HAL UART interrupt integration example.
- `test_parser_host.c`: PC host test.

The parser and preprocessor use no dynamic allocation.

## Integration Steps

1. Add `rssi_line_parser.c`, `rssi_preprocessor.c`, `mqtt_payload.c`, and `uart_rx_ring.c` to the STM32CubeIDE project.
2. Include the corresponding headers.
3. In the UART RX interrupt callback, only push each byte to `uart_rx_ring_push_isr()`.
4. In the main loop or a task, pop bytes, feed `rssi_parser_feed_byte()`, and call `rssi_preprocessor_update()` on `RSSI_PARSE_OK`.
5. Periodically call `mqtt_payload_build_snapshot()` and publish the resulting string through the selected MQTT/network module.

The included MQTT formatter only builds the payload string; it does not depend on any MQTT client library.
