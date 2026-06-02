/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "signal_generator.h"
#include "oscilloscope.h"
#include "uart_protocol.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_DAC1_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_TIM6_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* Initialize application modules */
  SignalGen_Init();
  Oscilloscope_Init();
  Protocol_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* Poll UART for incoming commands */
    Protocol_Poll();

    /* Process received command frame */
    ProtoFrame_t frame;
    if (Protocol_GetFrame(&frame))
    {
        switch (frame.cmd)
        {
        case CMD_SET_WAVEFORM:
            if (frame.data_len >= 1) {
                SignalGen_SetWaveform((WaveType_t)frame.data[0]);
                Protocol_SendAck(frame.cmd, 0);
            }
            break;

        case CMD_SET_FREQUENCY:
            if (frame.data_len >= 4) {
                uint32_t freq = ((uint32_t)frame.data[0] << 24)
                              | ((uint32_t)frame.data[1] << 16)
                              | ((uint32_t)frame.data[2] << 8)
                              |  (uint32_t)frame.data[3];
                SignalGen_SetFrequency(freq);
                Protocol_SendAck(frame.cmd, 0);
            }
            break;

        case CMD_SET_AMPLITUDE:
            if (frame.data_len >= 2) {
                uint16_t amp = ((uint16_t)frame.data[0] << 8)
                             |  (uint16_t)frame.data[1];
                SignalGen_SetAmplitude(amp);
                Protocol_SendAck(frame.cmd, 0);
            }
            break;

        case CMD_SIG_ONOFF:
            if (frame.data_len >= 1) {
                if (frame.data[0]) SignalGen_Start();
                else               SignalGen_Stop();
                Protocol_SendAck(frame.cmd, frame.data[0] && !SignalGen_IsRunning());
            }
            break;

        case CMD_SET_SAMPLERATE:
            if (frame.data_len >= 4) {
                uint32_t sps = ((uint32_t)frame.data[0] << 24)
                             | ((uint32_t)frame.data[1] << 16)
                             | ((uint32_t)frame.data[2] << 8)
                             |  (uint32_t)frame.data[3];
                Oscilloscope_SetSampleRate(sps);
                Protocol_SendAck(frame.cmd, 0);
            }
            break;

        case CMD_OSC_ONOFF:
            if (frame.data_len >= 1) {
                if (frame.data[0]) Oscilloscope_Start();
                else               Oscilloscope_Stop();
                Protocol_SendAck(frame.cmd, 0);
            }
            break;

        default:
            Protocol_SendAck(frame.cmd, 1); /* Unknown command */
            break;
        }
    }

    /* Poll oscilloscope to send captured data */
    Oscilloscope_Poll();

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* ---- HAL Callbacks ---- */

/**
  * @brief  ADC DMA half-transfer complete callback
  */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
        Oscilloscope_HalfCpltCallback();
}

/**
  * @brief  ADC DMA transfer complete callback
  */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
        Oscilloscope_CpltCallback();
}

/**
  * @brief  UART DMA transmit complete callback
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
        g_uart_tx_busy = false;
}

/**
  * @brief  Timer period elapsed callback
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
        SignalGen_TIM6_Callback();
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
