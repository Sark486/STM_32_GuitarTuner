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
#include "dma.h"
#include "i2s.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include "stm32f4xx_hal_gpio.h"
#include "mic.h"
#include "level.h"
#include "pitch.h"
#include "note.h"
#if TUNER_CAPTURE
#include "capture.h"
#include "replay.h"
#endif
#include "cycles.h"
#include <stddef.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

static_assert(MIC_BLOCK_SAMPLES % PITCH_BLOCK_SIZE == 0, "MIC_BLOCK_SAMPLES doesn't divide evenly by PITCH_BLOCK_SIZE");
static_assert(MIC_BLOCK_SAMPLES <= (PITCH_HOP * PITCH_DECIM), "MIC_BLOCK_SAMPLES is bigger than one full hop");
static const size_t pushes_per_mic_block = MIC_BLOCK_SAMPLES / PITCH_BLOCK_SIZE;

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

static void convert_32_to_16(int32_t const *samples, int16_t *out, size_t size);

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
  MX_USART2_UART_Init();
  MX_I2S2_Init();
  /* USER CODE BEGIN 2 */

  mic_init(&hi2s2);
  level_init(mic_get_sample_rate_hz());
#if TUNER_CAPTURE
  capture_init(&huart2);
#endif
  cycles_init();
  HAL_StatusTypeDef mic_status = mic_start();
  
  if (mic_status == HAL_OK)
  {
    printf("Mic started\r\n");
  }
  else
  {
    printf("Mic start failed: %d\r\n", mic_status);
  }

  static int32_t samples[MIC_BLOCK_SAMPLES];
  static float dbfs;
  static pitch_t pitch_state;
  static int16_t buffer[MIC_BLOCK_SAMPLES];
  pitch_init(&pitch_state);


#if TUNER_CAPTURE
  static bool capture_filled = false; // the capture buffer holds a recording (c) or an upload (u)
#endif
  
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint8_t in[1];
    if (HAL_UART_Receive(&huart2, in, 1, 0) == HAL_OK)
    {
#if TUNER_CAPTURE
      if(in[0] == 'c'){
        printf("Capture start\r\n");
        uint32_t start = HAL_GetTick();

        bool record_result = capture_record();
        capture_filled = record_result;
        printf("Record result=%s time=%" PRIu32 "\r\n", record_result? "OK" : "FAILED", HAL_GetTick() - start);
        if (record_result)
        {
          bool send_result = capture_send();
          printf("Send result=%s\r\n", send_result? "OK" : "FAILED");
        }
      }
      else if (in[0] == 'u')
      {
        // one recording from the PC into the capture buffer (tools/replay_compare.py)
        capture_filled = capture_receive();
      }
      else if (in[0] == 'r')
      {
        // the capture buffer through the pitch module, every push timed
        if (capture_filled)
        {
          replay_run(&pitch_state, capture_get_buffer(), capture_get_sample_count());
          mic_resync(); // main was busy for about a second: skip the halves that piled up
        }
        else
        {
          printf("REPLAY FAILED no data, send c or u first\r\n");
        }
      }

      // Discard any bytes that arrived while the board was busy.
      uint8_t discard;
      while (HAL_UART_Receive(&huart2, &discard, 1, 0) == HAL_OK)
      {
        // ignore it
      }
      pitch_init(&pitch_state);
#endif
    }
    if (mic_read_block(samples))
    {
      HAL_GPIO_WritePin(MARK1_GPIO_Port, MARK1_Pin, GPIO_PIN_SET);
      bool got_pitch_result = false;
      pitch_result_t pitch_result;

      // not used right now but will be used later, keeping it here to not miss timing/memory issues
      (void)level_process(samples, MIC_BLOCK_SAMPLES, &dbfs);

      convert_32_to_16(samples, buffer, MIC_BLOCK_SAMPLES);
      for (size_t i = 0; i < pushes_per_mic_block; i++)
      {
        if (pitch_push(&pitch_state, buffer + (i * PITCH_BLOCK_SIZE), PITCH_BLOCK_SIZE, &pitch_result))
        {
          got_pitch_result = true;
        }
      }
      if (got_pitch_result)
      {
        if (pitch_result.valid && pitch_result.confidence <= PITCH_YIN_THRESHOLD)
        {
          note_result_t note_result;
          bool got_note_result = note_from_freq(pitch_result.freq_hz, NOTE_A4_DEFAULT_HZ, &note_result);
          if (got_note_result)
          {
            printf("Note:%s,%.2f\r\n", note_name(note_result.pitch_class), (double)note_result.cents);
          }
        }
      }
      HAL_GPIO_WritePin(MARK1_GPIO_Port, MARK1_Pin, GPIO_PIN_RESET);
    }
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 12;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

static void convert_32_to_16(int32_t const *samples, int16_t *out, size_t size)
{
  for(size_t i = 0; i < size; i++)
  {
    out[i] = (int16_t)(samples[i] >> 8);
  }
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
