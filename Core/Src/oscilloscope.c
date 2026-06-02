/**
  ******************************************************************************
  * @file    oscilloscope.c
  * @brief   ADC + DMA oscilloscope sampling implementation
  ******************************************************************************
  */
#include "oscilloscope.h"
#include "uart_protocol.h"
#include "adc.h"
#include "tim.h"
#include <string.h>

/* ---- Private State ---- */
static uint32_t s_sample_rate = 50000;  /* Default 50 kSPS */
static bool     s_running     = false;

/* ADC DMA double buffer */
static uint16_t s_adc_buf[OSC_BUFFER_SIZE];

/* Flag: which half-buffer is ready to send */
static volatile uint8_t s_data_ready = 0;  /* 0=none, 1=first half, 2=second half */

static void ConfigureTimerRate(TIM_HandleTypeDef *htim, uint32_t timer_clk, uint32_t target_hz)
{
    uint32_t prescaler;
    uint32_t arr;

    if (target_hz == 0) {
        target_hz = 1;
    }

    prescaler = (uint32_t)(((uint64_t)timer_clk + ((uint64_t)target_hz * 65536ULL) - 1ULL) /
                           ((uint64_t)target_hz * 65536ULL));
    if (prescaler > 0) {
        prescaler -= 1;
    }
    if (prescaler > 65535U) {
        prescaler = 65535U;
    }

    arr = (uint32_t)((uint64_t)timer_clk / ((uint64_t)(prescaler + 1U) * target_hz));
    if (arr == 0) {
        arr = 1;
    }
    arr -= 1;
    if (arr > 65535U) {
        arr = 65535U;
    }

    __HAL_TIM_SET_PRESCALER(htim, prescaler);
    __HAL_TIM_SET_AUTORELOAD(htim, arr);
    __HAL_TIM_SET_COUNTER(htim, 0);
    HAL_TIM_GenerateEvent(htim, TIM_EVENTSOURCE_UPDATE);
}

/* ---- Private: Update TIM3 ARR for target sample rate ---- */
static void UpdateTimerPeriod(void)
{
    ConfigureTimerRate(&htim3, OSC_TIM_CLK, s_sample_rate);
}

/* ---- Public API ---- */

void Oscilloscope_Init(void)
{
    s_sample_rate = 50000;
    s_running     = false;
    s_data_ready  = 0;
    memset(s_adc_buf, 0, sizeof(s_adc_buf));
}

void Oscilloscope_Start(void)
{
    if (s_running) return;

    UpdateTimerPeriod();
    s_data_ready = 0;

    /* Calibrate ADC */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    /* Start ADC with DMA (circular, half-transfer + transfer-complete interrupts) */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_buf, OSC_BUFFER_SIZE);

    /* Start TIM3 to trigger ADC */
    HAL_TIM_Base_Start(&htim3);

    s_running = true;
}

void Oscilloscope_Stop(void)
{
    if (!s_running) return;

    HAL_TIM_Base_Stop(&htim3);
    HAL_ADC_Stop_DMA(&hadc1);

    s_running    = false;
    s_data_ready = 0;
}

void Oscilloscope_SetSampleRate(uint32_t sps)
{
    if (sps < 100)    sps = 100;
    if (sps > 50000) sps = 50000;  /* 2 Mbps UART can stream about 50 kSPS reliably */
    s_sample_rate = sps;
    UpdateTimerPeriod();
}

void Oscilloscope_Poll(void)
{
    if (!s_running) return;

    /* Check if a half-buffer is ready and UART TX is free */
    uint8_t ready = s_data_ready;
    if (ready == 0) return;

    /* Safety: if TX busy stuck for too long, force reset */
    static uint32_t tx_busy_tick = 0;
    if (g_uart_tx_busy) {
        if (tx_busy_tick == 0) {
            tx_busy_tick = HAL_GetTick();
        } else if ((HAL_GetTick() - tx_busy_tick) > 50) {
            g_uart_tx_busy = false;  /* Force reset after 50ms timeout */
            tx_busy_tick = 0;
        }
        return;
    }
    tx_busy_tick = 0;

    uint16_t *src;
    if (ready == 1)
        src = &s_adc_buf[0];                 /* First half */
    else
        src = &s_adc_buf[OSC_HALF_BUF_SIZE]; /* Second half */

    /* Clear before sending so an ISR that fires during TX is not lost. */
    s_data_ready = 0;

    /* Send waveform data to PC */
    Protocol_SendWaveData(src, OSC_HALF_BUF_SIZE);
}

/* ---- Getters ---- */
uint32_t Oscilloscope_GetSampleRate(void) { return s_sample_rate; }
bool     Oscilloscope_IsRunning(void)     { return s_running;     }

/* ---- DMA Callbacks (called from ISR context via main.c) ---- */

void Oscilloscope_HalfCpltCallback(void)
{
    /* First half of buffer is ready */
    s_data_ready = 1;
}

void Oscilloscope_CpltCallback(void)
{
    /* Second half of buffer is ready */
    s_data_ready = 2;
}
