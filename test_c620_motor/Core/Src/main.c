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
#include "fdcan.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

FDCAN_TxHeaderTypeDef tx_Header;
FDCAN_TxHeaderTypeDef hb_Header;
uint8_t  hb_data[8];
uint8_t  start_ok = 0;
uint8_t  tx_stat  = 0;
uint32_t fdcan_clk = 0;
uint32_t alive_cnt = 0;
uint32_t loop_cnt  = 0;
volatile uint32_t g_fault = 0;

void Txheader_Init(void)
{
        tx_Header.Identifier = 0x200 ;
        tx_Header.DataLength = FDCAN_DLC_BYTES_8 ;
        tx_Header.IdType = FDCAN_STANDARD_ID ;
        tx_Header.BitRateSwitch = FDCAN_BRS_OFF ;
        tx_Header.ErrorStateIndicator = FDCAN_ESI_ACTIVE ;
        tx_Header.FDFormat = FDCAN_CLASSIC_CAN ;
        tx_Header.MessageMarker = 0 ;
        tx_Header.TxEventFifoControl =FDCAN_NO_TX_EVENTS ;
        tx_Header.TxFrameType = FDCAN_DATA_FRAME ;

        hb_Header.Identifier = 0x700 ;
        hb_Header.DataLength = FDCAN_DLC_BYTES_8 ;
        hb_Header.IdType = FDCAN_STANDARD_ID ;
        hb_Header.BitRateSwitch = FDCAN_BRS_OFF ;
        hb_Header.ErrorStateIndicator = FDCAN_ESI_ACTIVE ;
        hb_Header.FDFormat = FDCAN_CLASSIC_CAN ;
        hb_Header.MessageMarker = 0 ;
        hb_Header.TxEventFifoControl = FDCAN_NO_TX_EVENTS ;
        hb_Header.TxFrameType = FDCAN_DATA_FRAME ;
}

uint8_t tx_data[8] ;
uint16_t current1 = 410 ;

void Current_Init(void)
{
    tx_data[0] = (current1 >> 8) & 0xff ;
    tx_data[1] = current1 & 0xff ;
    tx_data[2] = 0x00 ;
    tx_data[3] = 0x00 ;
    tx_data[4] = 0x00 ;
    tx_data[5] = 0x00 ;
    tx_data[6] = 0x00 ;
    tx_data[7] = 0x00 ;
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_FDCAN1_Init();
  /* USER CODE BEGIN 2 */

  Txheader_Init() ;
  Current_Init() ;

  if (HAL_FDCAN_Start(&hfdcan1) == HAL_OK)
  {
    start_ok = 1 ;
    fdcan_clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN) ;
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      tx_stat = HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_Header, tx_data) ;

      if (loop_cnt >= 500)
      {
          loop_cnt = 0 ;
          hb_data[0] = (uint8_t)(alive_cnt & 0xFF) ; alive_cnt++ ;
          hb_data[1] = tx_stat ;
          hb_data[2] = start_ok ;
          hb_data[3] = (uint8_t)(fdcan_clk & 0xFF) ;
          hb_data[4] = (uint8_t)((fdcan_clk >> 8) & 0xFF) ;
          hb_data[5] = (uint8_t)((fdcan_clk >> 16) & 0xFF) ;
          hb_data[6] = (uint8_t)((fdcan_clk >> 24) & 0xFF) ;
          hb_data[7] = 0xA5 ;
          HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &hb_Header, hb_data) ;

          if (!start_ok)
          {
              if (HAL_FDCAN_Init(&hfdcan1) == HAL_OK)
              {
                  if (HAL_FDCAN_Start(&hfdcan1) == HAL_OK)
                  {
                      start_ok = 1 ;
                      fdcan_clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN) ;
                  }
              }
          }
      }
      loop_cnt++ ;

      HAL_Delay(2) ;
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 25;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM23 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM23)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  g_fault++ ;
  return ;   /* non-fatal: keep firmware running so CAN diagnostics remain visible */
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
