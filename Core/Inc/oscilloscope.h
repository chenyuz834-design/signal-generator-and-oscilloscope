/**
  ******************************************************************************
  * @file    oscilloscope.h
  * @brief   ADC-based oscilloscope sampling module
  ******************************************************************************
  */
#ifndef __OSCILLOSCOPE_H
#define __OSCILLOSCOPE_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- Configuration ---- */
#define OSC_BUFFER_SIZE     2048  /* Total ADC DMA buffer (double-buffered) */
#define OSC_HALF_BUF_SIZE   (OSC_BUFFER_SIZE / 2)  /* 1024 samples per half */
#define OSC_TIM_CLK         170000000UL  /* TIM3 input clock Hz */

/* ---- API ---- */
void     Oscilloscope_Init(void);
void     Oscilloscope_Start(void);
void     Oscilloscope_Stop(void);
void     Oscilloscope_SetSampleRate(uint32_t sps);
void     Oscilloscope_Poll(void);    /* Call in main loop to send data */

/* ---- State Getters ---- */
uint32_t Oscilloscope_GetSampleRate(void);
bool     Oscilloscope_IsRunning(void);

/* ---- Callbacks (called from HAL ISR context) ---- */
void     Oscilloscope_HalfCpltCallback(void);
void     Oscilloscope_CpltCallback(void);

#endif /* __OSCILLOSCOPE_H */
