#include "uart_rx_ring.h"

#include <stddef.h>
#include <string.h>

#if (UART_RX_RING_CAPACITY == 0u) || \
    ((UART_RX_RING_CAPACITY & (UART_RX_RING_CAPACITY - 1u)) != 0u)
#error "UART_RX_RING_CAPACITY must be a power of two"
#endif

void uart_rx_ring_init(uart_rx_ring_t *ring)
{
    if (ring != NULL) {
        memset(ring, 0, sizeof(*ring));
    }
}

bool uart_rx_ring_push_isr(uart_rx_ring_t *ring, uint8_t byte)
{
    if (ring == NULL) {
        return false;
    }

    /* 용량을 2의 거듭제곱으로 제한해 ISR에서 나눗셈 없이 인덱스를 순환한다. */
    uint16_t next = (uint16_t)((ring->head + 1u) & (UART_RX_RING_CAPACITY - 1u));
    if (next == ring->tail) {
        /* head==tail을 빈 상태로 쓰기 위해 한 칸은 비워 둔다. */
        ring->overflow_count++;
        return false;
    }

    ring->data[ring->head] = byte;
    ring->head = next;
    return true;
}

bool uart_rx_ring_pop(uart_rx_ring_t *ring, uint8_t *out_byte)
{
    if (ring == NULL || out_byte == NULL || ring->tail == ring->head) {
        return false;
    }

    *out_byte = ring->data[ring->tail];
    ring->tail = (uint16_t)((ring->tail + 1u) & (UART_RX_RING_CAPACITY - 1u));
    return true;
}

uint32_t uart_rx_ring_overflow_count(const uart_rx_ring_t *ring)
{
    return ring != NULL ? ring->overflow_count : 0u;
}
