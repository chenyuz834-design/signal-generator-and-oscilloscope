/**
  ******************************************************************************
  * @file    uart_protocol.h
  * @brief   UART communication protocol for PC ↔ MCU
  ******************************************************************************
  * Frame format:
  *   [0xAA][0x55][CMD][LEN_H][LEN_L][DATA...][XOR_CHECKSUM]
  *
  * Checksum = XOR of bytes from CMD to end of DATA (inclusive)
  ******************************************************************************
  */
#ifndef __UART_PROTOCOL_H
#define __UART_PROTOCOL_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- Frame Constants ---- */
#define PROTO_HEADER1       0xAA
#define PROTO_HEADER2       0x55
#define PROTO_MAX_DATA_LEN  32    /* Max data payload for commands (PC→MCU) */
#define PROTO_RX_BUF_SIZE   256   /* DMA circular RX buffer */

/* ---- Command Codes (PC → MCU) ---- */
#define CMD_SET_WAVEFORM    0x01  /* data[0]: 0=sine,1=square,2=tri,3=saw */
#define CMD_SET_FREQUENCY   0x02  /* data[0..3]: uint32 big-endian Hz     */
#define CMD_SET_AMPLITUDE   0x03  /* data[0..1]: uint16 big-endian mV     */
#define CMD_SIG_ONOFF       0x04  /* data[0]: 0=stop, 1=start             */
#define CMD_SET_SAMPLERATE  0x10  /* data[0..3]: uint32 big-endian SPS    */
#define CMD_OSC_ONOFF       0x11  /* data[0]: 0=stop, 1=start             */

/* ---- Response Codes (MCU → PC) ---- */
#define CMD_WAVE_DATA       0x80  /* Waveform data upload                 */
#define CMD_PARAM_ACK       0x81  /* data[0]=orig_cmd, data[1]=status     */
#define CMD_ERROR           0xFF  /* Error                                */

/* ---- Parsed Frame ---- */
typedef struct {
    uint8_t  cmd;
    uint16_t data_len;
    uint8_t  data[PROTO_MAX_DATA_LEN];
    bool     valid;
} ProtoFrame_t;

/* ---- RX Parser State ---- */
typedef enum {
    PARSE_IDLE = 0,
    PARSE_HEADER2,
    PARSE_CMD,
    PARSE_LEN_H,
    PARSE_LEN_L,
    PARSE_DATA,
    PARSE_CHECKSUM
} ParseState_t;

/* ---- API ---- */
void     Protocol_Init(void);
void     Protocol_Poll(void);                      /* Call in main loop */
bool     Protocol_GetFrame(ProtoFrame_t *frame);    /* Get next parsed frame */
void     Protocol_SendAck(uint8_t orig_cmd, uint8_t status);
void     Protocol_SendWaveData(uint16_t *data, uint16_t count);

/* TX busy flag – used by oscilloscope to skip frames */
extern volatile bool g_uart_tx_busy;

#endif /* __UART_PROTOCOL_H */
