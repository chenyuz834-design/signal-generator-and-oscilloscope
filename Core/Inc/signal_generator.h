/**
  ******************************************************************************
  * @file    signal_generator.h
  * @brief   DAC-based signal generator (sine / square / triangle / sawtooth)
  ******************************************************************************
  */
#ifndef __SIGNAL_GENERATOR_H
#define __SIGNAL_GENERATOR_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- Configuration ---- */
#define SIGGEN_LUT_SIZE     256   /* Points per waveform period */
#define SIGGEN_MIN_LUT_SIZE 4     /* Fewer points are used at high frequency */
#define SIGGEN_DAC_MAX      4095  /* 12-bit DAC full scale     */
#define SIGGEN_VREF_MV      3300  /* DAC reference voltage mV  */
#define SIGGEN_TIM_CLK      170000000UL  /* TIM6 input clock Hz */
#define SIGGEN_MAX_FREQ_HZ  5000UL
#define SIGGEN_MAX_UPDATE_RATE 50000UL /* TIM6 interrupt update ceiling */

/* ---- Waveform Types ---- */
typedef enum {
    WAVE_SINE     = 0,
    WAVE_SQUARE   = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAWTOOTH = 3
} WaveType_t;

/* ---- API ---- */
void     SignalGen_Init(void);
void     SignalGen_SetWaveform(WaveType_t type);
void     SignalGen_SetFrequency(uint32_t freq_hz);
void     SignalGen_SetAmplitude(uint16_t amplitude_mv);
void     SignalGen_Start(void);
void     SignalGen_Stop(void);
void     SignalGen_TIM6_Callback(void);

/* ---- State Getters ---- */
WaveType_t SignalGen_GetWaveform(void);
uint32_t   SignalGen_GetFrequency(void);
uint16_t   SignalGen_GetAmplitude(void);
bool       SignalGen_IsRunning(void);

#endif /* __SIGNAL_GENERATOR_H */
