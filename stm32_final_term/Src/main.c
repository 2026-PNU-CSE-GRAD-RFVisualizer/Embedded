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
#include "uart_rx_ring.h"

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
#define SNAPSHOT_PERIOD_MS      1000UL

volatile uint32_t g_ms_ticks;
volatile uint32_t g_uart_rx_count;
volatile uint32_t g_parse_ok_count;
volatile uint32_t g_checksum_error_count;
volatile uint32_t g_format_error_count;
volatile uint32_t g_last_parse_result;
volatile rssi_measurement_t g_latest_measurement;
volatile int g_latest_mqtt_payload_len;
char g_mqtt_payload[MQTT_PAYLOAD_MAX_LEN];

static rssi_preprocessor_t s_rssi_ctx;
static uart_rx_ring_t s_uart_rx_ring;

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
        g_uart_rx_count++;
        /* ISR에서는 바이트 저장만 하고 문자열 파싱은 Main Loop로 넘긴다. */
        (void)uart_rx_ring_push_isr(&s_uart_rx_ring, byte);
    }
}

static void process_uart_rx(void)
{
    /* 파서와 노드 테이블을 Main Context에서만 다뤄 JSON 생성과의 경쟁을 막는다. */
    uint8_t byte;
    while (uart_rx_ring_pop(&s_uart_rx_ring, &byte)) {
        rssi_measurement_t measurement;
        rssi_parse_result_t result = rssi_parser_feed_byte(byte, &measurement);
        g_last_parse_result = (uint32_t)result;

        if (result == RSSI_PARSE_OK) {
            g_latest_measurement = measurement;
            g_parse_ok_count++;
            (void)rssi_preprocessor_update(&s_rssi_ctx, &measurement, millis());
        } else if (result == RSSI_PARSE_CHECKSUM_ERROR) {
            g_checksum_error_count++;
            s_rssi_ctx.checksum_error_count++;
        } else if (result == RSSI_PARSE_FORMAT_ERROR) {
            g_format_error_count++;
            s_rssi_ctx.format_error_count++;
        }
    }

    s_rssi_ctx.uart_overflow_count =
        uart_rx_ring_overflow_count(&s_uart_rx_ring);
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
    uart_rx_ring_init(&s_uart_rx_ring);
    systick_init();
    usart1_init_115200();

    printf("STM32 RSSI receiver ready\r\n");

    uint32_t last_snapshot_ms = millis();
    for (;;) {
        process_uart_rx();

        uint32_t now_ms = millis();
        rssi_preprocessor_update_timeouts(&s_rssi_ctx, now_ms);

        /* 입력 패킷마다 출력하지 않고 1초 Snapshot으로 UART 대역폭을 제한한다. */
        if (s_rssi_ctx.total_packet_rx_count > 0u &&
            (now_ms - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            last_snapshot_ms = now_ms;
            g_latest_mqtt_payload_len = mqtt_payload_build_snapshot(g_mqtt_payload,
                                                                    sizeof(g_mqtt_payload),
                                                                    "gw-01",
                                                                    &s_rssi_ctx,
                                                                    now_ms);
            if (g_latest_mqtt_payload_len > 0) {
                printf("%s\r\n", g_mqtt_payload);
            }
        }
    }
}
