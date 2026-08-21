/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : ESP32 Gateway UART RSSI receiver for STM32F107VC.
 ******************************************************************************
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mqtt_payload.h"
#include "rssi_line_parser.h"
#include "rssi_preprocessor.h"

#define PERIPH_BASE             0x40000000UL
#define APB2PERIPH_BASE         (PERIPH_BASE + 0x10000UL)
#define AHBPERIPH_BASE          (PERIPH_BASE + 0x20000UL)

#define GPIOA_BASE              (APB2PERIPH_BASE + 0x0800UL)
#define RCC_BASE                (AHBPERIPH_BASE + 0x1000UL)
#define USART1_BASE             (APB2PERIPH_BASE + 0x3800UL)

#define RCC_CR                  (*(volatile uint32_t *)(RCC_BASE + 0x00UL))
#define RCC_CFGR                (*(volatile uint32_t *)(RCC_BASE + 0x04UL))
#define RCC_APB2ENR             (*(volatile uint32_t *)(RCC_BASE + 0x18UL))

#define GPIOA_CRH               (*(volatile uint32_t *)(GPIOA_BASE + 0x04UL))

#define USART1_SR               (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART1_DR               (*(volatile uint32_t *)(USART1_BASE + 0x04UL))
#define USART1_BRR              (*(volatile uint32_t *)(USART1_BASE + 0x08UL))
#define USART1_CR1              (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))

#define SYST_CSR                (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR                (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR                (*(volatile uint32_t *)0xE000E018UL)
#define NVIC_ISER1              (*(volatile uint32_t *)0xE000E104UL)

#define RCC_APB2ENR_AFIOEN      (1UL << 0)
#define RCC_APB2ENR_IOPAEN      (1UL << 2)
#define RCC_APB2ENR_USART1EN    (1UL << 14)

#define USART_SR_RXNE           (1UL << 5)
#define USART_SR_TXE            (1UL << 7)
#define USART_CR1_RE            (1UL << 2)
#define USART_CR1_TE            (1UL << 3)
#define USART_CR1_RXNEIE        (1UL << 5)
#define USART_CR1_UE            (1UL << 13)

#define SYST_CSR_ENABLE         (1UL << 0)
#define SYST_CSR_TICKINT        (1UL << 1)
#define SYST_CSR_CLKSOURCE      (1UL << 2)

#define USART1_IRQn             37
#define USART1_NVIC_BIT         (USART1_IRQn - 32)

#define HSI_CLOCK_HZ            8000000UL
#define USART1_BAUDRATE         115200UL
#define UART_RX_RING_SIZE       256U

volatile uint32_t g_ms_ticks;
volatile uint32_t g_uart_rx_count;
volatile uint32_t g_parse_ok_count;
volatile uint32_t g_checksum_error_count;
volatile uint32_t g_format_error_count;
volatile uint32_t g_uart_rx_overflow_count;
volatile uint32_t g_last_parse_result;
volatile rssi_measurement_t g_latest_measurement;
volatile int g_latest_mqtt_payload_len;
char g_mqtt_payload[MQTT_PAYLOAD_MAX_LEN];

static rssi_preprocessor_t s_rssi_ctx;
static bool s_payload_dirty;
static uint64_t s_snapshot_timestamp_ms;
static volatile uint8_t s_uart_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t s_uart_rx_head;
static volatile uint16_t s_uart_rx_tail;

void SystemInit(void)
{
    /* Keep the reset clock tree: HSI 8 MHz as SYSCLK. */
    RCC_CR |= 0x00000001UL;
    RCC_CFGR = 0x00000000UL;
}

static uint32_t millis(void)
{
    return g_ms_ticks;
}

void SysTick_Handler(void)
{
    g_ms_ticks++;
}

static void systick_init(void)
{
    SYST_RVR = (HSI_CLOCK_HZ / 1000UL) - 1UL;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

static void usart1_init_115200(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /*
     * PA9  = USART1_TX, alternate function push-pull, 50 MHz: CNF=10 MODE=11.
     * PA10 = USART1_RX, input floating: CNF=01 MODE=00.
     */
    GPIOA_CRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA_CRH |=  ((0xBUL << 4) | (0x4UL << 8));

    /*
     * USARTDIV = 8 MHz / (16 * 115200) = 4.340.
     * BRR mantissa=4, fraction=5 => 0x045. Good enough for initial UART bring-up.
     */
    USART1_BRR = 0x045UL;
    USART1_CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE | USART_CR1_UE;

    NVIC_ISER1 = (1UL << USART1_NVIC_BIT);
}

void USART1_IRQHandler(void)
{
    if ((USART1_SR & USART_SR_RXNE) != 0UL) {
        uint8_t byte = (uint8_t)(USART1_DR & 0xFFU);
        uint16_t head = s_uart_rx_head;
        uint16_t next = (uint16_t)((head + 1U) % UART_RX_RING_SIZE);

        g_uart_rx_count++;
        if (next == s_uart_rx_tail) {
            g_uart_rx_overflow_count++;
        } else {
            s_uart_rx_ring[head] = byte;
            s_uart_rx_head = next;
        }
    }
}

static bool uart_rx_pop(uint8_t *out_byte)
{
    uint16_t tail = s_uart_rx_tail;
    if (tail == s_uart_rx_head) {
        return false;
    }

    *out_byte = s_uart_rx_ring[tail];
    s_uart_rx_tail = (uint16_t)((tail + 1U) % UART_RX_RING_SIZE);
    return true;
}

static void process_uart_rx(void)
{
    uint8_t byte;
    while (uart_rx_pop(&byte)) {
        rssi_measurement_t measurement;
        rssi_parse_result_t result = rssi_parser_feed_byte(byte, &measurement);
        g_last_parse_result = (uint32_t)result;

        if (result == RSSI_PARSE_OK) {
            g_latest_measurement = measurement;
            g_parse_ok_count++;
            (void)rssi_preprocessor_update(&s_rssi_ctx, &measurement, millis());
            s_snapshot_timestamp_ms = measurement.measurement_timestamp_ms;
            s_payload_dirty = true;
        } else if (result == RSSI_PARSE_CHECKSUM_ERROR) {
            g_checksum_error_count++;
            s_rssi_ctx.checksum_error_count++;
        } else if (result == RSSI_PARSE_FORMAT_ERROR) {
            g_format_error_count++;
            s_rssi_ctx.format_error_count++;
        }
    }
}

int __io_putchar(int ch)
{
    while ((USART1_SR & USART_SR_TXE) == 0UL) {
    }
    USART1_DR = (uint8_t)ch;
    return ch;
}

int main(void)
{
    rssi_parser_init();
    rssi_preprocessor_init(&s_rssi_ctx);
    systick_init();
    usart1_init_115200();

    printf("STM32 RSSI receiver ready\r\n");

    for (;;) {
        process_uart_rx();
        rssi_preprocessor_update_timeouts(&s_rssi_ctx, millis());

        if (s_payload_dirty) {
            s_payload_dirty = false;
            g_latest_mqtt_payload_len = mqtt_payload_build_snapshot(g_mqtt_payload,
                                                                    sizeof(g_mqtt_payload),
                                                                    "gw-01",
                                                                    &s_rssi_ctx,
                                                                    millis(),
                                                                    s_snapshot_timestamp_ms);
            if (g_latest_mqtt_payload_len > 0) {
                printf("%s\r\n", g_mqtt_payload);
            } else {
                printf("ERR mqtt payload build failed\r\n");
            }
        }
    }
}
