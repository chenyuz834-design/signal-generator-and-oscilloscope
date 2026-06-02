/**
  ******************************************************************************
  * @file    signal_generator.c
  * @brief   DAC waveform generator (TIM6 interrupt mode)
  ******************************************************************************
  */
#include "signal_generator.h"
#include "dac.h"
#include "tim.h"
#include <string.h>

#define SIGGEN_DAC_CHANNEL DAC_CHANNEL_2

/* Pre-computed sine quarter table (0~90 degrees, 65 entries, 0~10000 scaled) */
static const int16_t SINE_Q64[65] = {
        0,   245,   490,   735,   980,  1224,  1467,  1710,
     1951,  2191,  2429,  2667,  2903,  3137,  3369,  3599,
     3827,  4052,  4276,  4496,  4714,  4929,  5141,  5350,
     5556,  5758,  5957,  6152,  6344,  6532,  6716,  6895,
     7071,  7242,  7410,  7572,  7730,  7883,  8032,  8176,
     8315,  8449,  8577,  8701,  8819,  8932,  9040,  9143,
     9239,  9330,  9415,  9495,  9569,  9638,  9700,  9757,
     9808,  9853,  9892,  9925,  9952,  9973,  9988,  9997,
    10000
};

static int16_t sine_lut(uint32_t idx)
{
    idx &= 255;
    uint32_t quarter = idx >> 6;
    uint32_t offset  = idx & 63;
    switch (quarter) {
        case 0: return  SINE_Q64[offset];
        case 1: return  SINE_Q64[64 - offset];
        case 2: return -SINE_Q64[offset];
        case 3: return -SINE_Q64[64 - offset];
        default: return 0;
    }
}

/* ---- Private State ---- */
static WaveType_t s_wave_type   = WAVE_SINE;
static uint32_t   s_freq_hz     = 1000;
static uint16_t   s_amp_mv      = 3300;
static bool       s_running     = false;

static uint16_t   s_dac_buf[SIGGEN_LUT_SIZE];
static uint16_t   s_lut_len = SIGGEN_LUT_SIZE;
static volatile uint16_t s_lut_index = 0;

static uint16_t SelectLUTLength(uint32_t freq_hz)
{
    uint16_t len = SIGGEN_LUT_SIZE;

    while ((len > SIGGEN_MIN_LUT_SIZE) &&
           (((uint64_t)freq_hz * (uint64_t)len) > SIGGEN_MAX_UPDATE_RATE))
    {
        len /= 2;
    }

    return len;
}

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
    __HAL_TIM_CLEAR_FLAG(htim, TIM_FLAG_UPDATE);
}

/* ---- Private: Build look-up table ---- */
static void BuildLUT(void)
{
    uint32_t amp_scale = (uint32_t)s_amp_mv * 10000UL / SIGGEN_VREF_MV;
    int32_t half = SIGGEN_DAC_MAX / 2;
    s_lut_len = SelectLUTLength(s_freq_hz);

    for (uint32_t i = 0; i < s_lut_len; i++)
    {
        int32_t val = 0;
        uint32_t phase = (i * SIGGEN_LUT_SIZE) / s_lut_len;

        switch (s_wave_type)
        {
        case WAVE_SINE:
            val = (int32_t)sine_lut(phase);
            break;
        case WAVE_SQUARE:
            val = (phase < SIGGEN_LUT_SIZE / 2) ? 10000 : -10000;
            break;
        case WAVE_TRIANGLE:
            if (phase < SIGGEN_LUT_SIZE / 4)
                val = (int32_t)phase * 10000 / (SIGGEN_LUT_SIZE / 4);
            else if (phase < 3 * SIGGEN_LUT_SIZE / 4)
                val = 20000 - (int32_t)phase * 10000 / (SIGGEN_LUT_SIZE / 4);
            else
                val = -40000 + (int32_t)phase * 10000 / (SIGGEN_LUT_SIZE / 4);
            break;
        case WAVE_SAWTOOTH:
            val = (int32_t)phase * 20000 / SIGGEN_LUT_SIZE - 10000;
            break;
        default:
            val = 0;
            break;
        }

        int32_t dac_val = half + (val * (int32_t)amp_scale / 10000) * half / 10000;
        if (dac_val < 0) dac_val = 0;
        if (dac_val > SIGGEN_DAC_MAX) dac_val = SIGGEN_DAC_MAX;
        s_dac_buf[i] = (uint16_t)dac_val;
    }
}

static void UpdateTimerPeriod(void)
{
    uint32_t update_rate = s_freq_hz * (uint32_t)s_lut_len;
    ConfigureTimerRate(&htim6, SIGGEN_TIM_CLK, update_rate);
}

static bool StartOutput(void)
{
    HAL_TIM_Base_Stop_IT(&htim6);
    HAL_DAC_Stop(&hdac1, SIGGEN_DAC_CHANNEL);
    HAL_DAC_Start(&hdac1, SIGGEN_DAC_CHANNEL);

    s_lut_index = 0;
    __HAL_TIM_SET_COUNTER(&htim6, 0);
    __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);

    HAL_DAC_SetValue(&hdac1, SIGGEN_DAC_CHANNEL, DAC_ALIGN_12B_R, s_dac_buf[0]);
    return (HAL_TIM_Base_Start_IT(&htim6) == HAL_OK);
}

static void StopOutput(void)
{
    HAL_TIM_Base_Stop_IT(&htim6);
    HAL_DAC_Stop(&hdac1, SIGGEN_DAC_CHANNEL);
    HAL_DAC_Start(&hdac1, SIGGEN_DAC_CHANNEL);
    HAL_DAC_SetValue(&hdac1, SIGGEN_DAC_CHANNEL, DAC_ALIGN_12B_R, 0);
    s_lut_index = 0;
}

/* ---- Public API ---- */

void SignalGen_Init(void)
{
    s_wave_type = WAVE_SINE;
    s_freq_hz   = 1000;
    s_amp_mv    = 3300;
    s_running   = false;
    s_lut_len   = SIGGEN_LUT_SIZE;
    BuildLUT();
    UpdateTimerPeriod();
}

void SignalGen_SetWaveform(WaveType_t type)
{
    if (type > WAVE_SAWTOOTH) return;

    bool was_running = s_running;
    if (was_running) {
        StopOutput();
    }

    s_wave_type = type;
    BuildLUT();

    if (was_running) {
        s_running = StartOutput();
    }
}

void SignalGen_SetFrequency(uint32_t freq_hz)
{
    if (freq_hz < 1) freq_hz = 1;
    if (freq_hz > SIGGEN_MAX_FREQ_HZ) freq_hz = SIGGEN_MAX_FREQ_HZ;

    bool was_running = s_running;
    if (was_running) {
        StopOutput();
    }

    s_freq_hz = freq_hz;
    BuildLUT();
    UpdateTimerPeriod();

    if (was_running) {
        s_running = StartOutput();
    }
}

void SignalGen_SetAmplitude(uint16_t amplitude_mv)
{
    if (amplitude_mv > SIGGEN_VREF_MV) amplitude_mv = SIGGEN_VREF_MV;

    bool was_running = s_running;
    if (was_running) {
        StopOutput();
    }

    s_amp_mv = amplitude_mv;
    BuildLUT();

    if (was_running) {
        s_running = StartOutput();
    }
}

void SignalGen_Start(void)
{
    if (s_running) return;
    BuildLUT();
    UpdateTimerPeriod();
    s_running = StartOutput();
}

void SignalGen_Stop(void)
{
    StopOutput();
    s_running = false;
}

void SignalGen_TIM6_Callback(void)
{
    if (!s_running) {
        return;
    }

    HAL_DAC_SetValue(&hdac1, SIGGEN_DAC_CHANNEL, DAC_ALIGN_12B_R, s_dac_buf[s_lut_index]);
    s_lut_index++;
    if (s_lut_index >= s_lut_len) {
        s_lut_index = 0;
    }
}

WaveType_t SignalGen_GetWaveform(void)  { return s_wave_type; }
uint32_t   SignalGen_GetFrequency(void) { return s_freq_hz;   }
uint16_t   SignalGen_GetAmplitude(void) { return s_amp_mv;    }
bool       SignalGen_IsRunning(void)    { return s_running;   }
