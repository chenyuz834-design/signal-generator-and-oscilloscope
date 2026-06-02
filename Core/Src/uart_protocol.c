/**
  ******************************************************************************
  * @file    uart_protocol.c
  * @brief   Binary frame protocol over UART with DMA
  ******************************************************************************
  */
#include "uart_protocol.h"
#include "usart.h"
#include <string.h>

/* ---- TX State ---- */
volatile bool g_uart_tx_busy = false;

/*
 * TX buffer – large enough for waveform data frame:
 *   Header(2) + CMD(1) + LEN(2) + sample_count(2) + samples(1024*2) + XOR(1)
 *   = 2056 bytes max
 */
#define TX_BUF_SIZE  2060
static uint8_t s_tx_buf[TX_BUF_SIZE];

/* ---- RX DMA Circular Buffer ---- */
static uint8_t  s_rx_dma_buf[PROTO_RX_BUF_SIZE];
static uint16_t s_rx_tail = 0;  /* Consumer read position */

/* ---- RX Parser ---- */
static ParseState_t s_parse_state = PARSE_IDLE;
static uint8_t      s_parse_cmd;
static uint16_t     s_parse_len;
static uint16_t     s_parse_data_idx;
static uint8_t      s_parse_xor;
static uint8_t      s_parse_data[PROTO_MAX_DATA_LEN];

/* ---- Parsed Frame Queue (simple single-slot) ---- */
static ProtoFrame_t s_frame;
static volatile bool s_frame_pending = false;

/* ---- Private: Process one received byte ---- */
static void ParseByte(uint8_t byte)
{
    switch (s_parse_state)
    {
    case PARSE_IDLE:
        if (byte == PROTO_HEADER1)
            s_parse_state = PARSE_HEADER2;
        break;

    case PARSE_HEADER2:
        if (byte == PROTO_HEADER2)
            s_parse_state = PARSE_CMD;
        else
            s_parse_state = PARSE_IDLE;
        break;

    case PARSE_CMD:
        s_parse_cmd = byte;
        s_parse_xor = byte;
        s_parse_state = PARSE_LEN_H;
        break;

    case PARSE_LEN_H:
        s_parse_len = (uint16_t)byte << 8;
        s_parse_xor ^= byte;
        s_parse_state = PARSE_LEN_L;
        break;

    case PARSE_LEN_L:
        s_parse_len |= byte;
        s_parse_xor ^= byte;
        s_parse_data_idx = 0;
        if (s_parse_len == 0)
            s_parse_state = PARSE_CHECKSUM;
        else if (s_parse_len > PROTO_MAX_DATA_LEN)
            s_parse_state = PARSE_IDLE;  /* Reject oversized frames */
        else
            s_parse_state = PARSE_DATA;
        break;

    case PARSE_DATA:
        s_parse_data[s_parse_data_idx++] = byte;
        s_parse_xor ^= byte;
        if (s_parse_data_idx >= s_parse_len)
            s_parse_state = PARSE_CHECKSUM;
        break;

    case PARSE_CHECKSUM:
        if (byte == s_parse_xor)
        {
            /* Valid frame */
            s_frame.cmd      = s_parse_cmd;
            s_frame.data_len = s_parse_len;
            memcpy(s_frame.data, s_parse_data, s_parse_len);
            s_frame.valid    = true;
            s_frame_pending  = true;
        }
        s_parse_state = PARSE_IDLE;
        break;

    default:
        s_parse_state = PARSE_IDLE;
        break;
    }
}

/* ---- Public API ---- */

void Protocol_Init(void)
{
    s_rx_tail       = 0;
    s_parse_state   = PARSE_IDLE;
    s_frame_pending = false;
    g_uart_tx_busy  = false;

    /* Start UART RX DMA in circular mode */
    HAL_UART_Receive_DMA(&huart1, s_rx_dma_buf, PROTO_RX_BUF_SIZE);
}

void Protocol_Poll(void)
{
    /* Calculate DMA write head position */
    uint16_t head = (uint16_t)((PROTO_RX_BUF_SIZE -
                    __HAL_DMA_GET_COUNTER(huart1.hdmarx)) % PROTO_RX_BUF_SIZE);

    /* Process all new bytes in the circular buffer */
    while (s_rx_tail != head)
    {
        ParseByte(s_rx_dma_buf[s_rx_tail]);
        s_rx_tail++;
        if (s_rx_tail >= PROTO_RX_BUF_SIZE)
            s_rx_tail = 0;
    }
}

bool Protocol_GetFrame(ProtoFrame_t *frame)
{
    if (!s_frame_pending) return false;

    *frame = s_frame;
    s_frame_pending = false;
    return true;
}

void Protocol_SendAck(uint8_t orig_cmd, uint8_t status)
{
    if (g_uart_tx_busy) return;

    uint8_t idx = 0;
    s_tx_buf[idx++] = PROTO_HEADER1;
    s_tx_buf[idx++] = PROTO_HEADER2;
    s_tx_buf[idx++] = CMD_PARAM_ACK;
    s_tx_buf[idx++] = 0x00;  /* LEN_H */
    s_tx_buf[idx++] = 0x02;  /* LEN_L = 2 */
    s_tx_buf[idx++] = orig_cmd;
    s_tx_buf[idx++] = status;

    /* XOR checksum: CMD ^ LEN_H ^ LEN_L ^ data */
    uint8_t xor_val = CMD_PARAM_ACK ^ 0x00 ^ 0x02 ^ orig_cmd ^ status;
    s_tx_buf[idx++] = xor_val;

    g_uart_tx_busy = true;
    if (HAL_UART_Transmit_DMA(&huart1, s_tx_buf, idx) != HAL_OK) {
        g_uart_tx_busy = false;
    }
}

void Protocol_SendWaveData(uint16_t *data, uint16_t count)
{
    if (g_uart_tx_busy) return;

    uint16_t data_len = 2 + count * 2;  /* sample_count(2) + samples(N*2) */
    uint16_t total    = 2 + 1 + 2 + data_len + 1;  /* header+cmd+len+data+xor */

    if (total > TX_BUF_SIZE) return;

    uint16_t idx = 0;
    s_tx_buf[idx++] = PROTO_HEADER1;
    s_tx_buf[idx++] = PROTO_HEADER2;
    s_tx_buf[idx++] = CMD_WAVE_DATA;
    s_tx_buf[idx++] = (uint8_t)(data_len >> 8);   /* LEN_H */
    s_tx_buf[idx++] = (uint8_t)(data_len & 0xFF); /* LEN_L */

    /* Sample count (big-endian) */
    s_tx_buf[idx++] = (uint8_t)(count >> 8);
    s_tx_buf[idx++] = (uint8_t)(count & 0xFF);

    /* Sample data (big-endian per sample) */
    for (uint16_t i = 0; i < count; i++)
    {
        uint16_t sample = data[i] & 0x0FFFU;
        s_tx_buf[idx++] = (uint8_t)(sample >> 8);
        s_tx_buf[idx++] = (uint8_t)(sample & 0xFF);
    }

    /* XOR checksum: from CMD to end of data */
    uint8_t xor_val = 0;
    for (uint16_t i = 2; i < idx; i++)
        xor_val ^= s_tx_buf[i];
    s_tx_buf[idx++] = xor_val;

    g_uart_tx_busy = true;
    if (HAL_UART_Transmit_DMA(&huart1, s_tx_buf, idx) != HAL_OK) {
        g_uart_tx_busy = false;
    }
}
