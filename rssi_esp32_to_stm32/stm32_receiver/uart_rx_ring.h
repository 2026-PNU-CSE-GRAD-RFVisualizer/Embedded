#ifndef UART_RX_RING_H
#define UART_RX_RING_H

#include <stdbool.h>
#include <stdint.h>

#define UART_RX_RING_CAPACITY 256u

typedef struct {
    uint8_t data[UART_RX_RING_CAPACITY];
    /* ISR만 head를 쓰고 Main/Task만 tail을 쓰는 SPSC 구조다. */
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count;
} uart_rx_ring_t;

void uart_rx_ring_init(uart_rx_ring_t *ring);
bool uart_rx_ring_push_isr(uart_rx_ring_t *ring, uint8_t byte);
bool uart_rx_ring_pop(uart_rx_ring_t *ring, uint8_t *out_byte);
uint32_t uart_rx_ring_overflow_count(const uart_rx_ring_t *ring);

#endif
