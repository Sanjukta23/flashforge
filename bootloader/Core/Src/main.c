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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "flash.h"
#include "crc.h"
#include <stdio.h>
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
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define SOF        0xA5
#define ACK        0x79
#define NACK       0x1F

#define CMD_PING   0x01
#define CMD_ERASE  0x02
#define CMD_WRITE  0x03
#define CMD_GO     0x05

#define BYTE_TIMEOUT_MS  100

typedef enum { ST_WAIT_SOF, ST_LEN, ST_CMD, ST_PAYLOAD, ST_CRC } rx_state_t;

static void send_byte(uint8_t b)
{
    HAL_UART_Transmit(&huart2, &b, 1, 100);
}

static void handle_frame(uint8_t cmd, uint8_t *payload, uint8_t len);  /* part 3 */

static void protocol_loop(void)
{
    rx_state_t st = ST_WAIT_SOF;
    uint8_t  buf[2 + 255];        /* LEN, CMD, then payload — the CRC'd region */
    uint8_t  len = 0, got = 0;
    uint8_t  crc_bytes[4];
    uint8_t  b;

    while (1)
    {
        /* one byte at a time; timeout matters only mid-frame */
        uint32_t to = (st == ST_WAIT_SOF) ? HAL_MAX_DELAY : BYTE_TIMEOUT_MS;

        if (HAL_UART_Receive(&huart2, &b, 1, to) != HAL_OK)
        {
            st = ST_WAIT_SOF;              /* silence mid-frame: abandon, resync */
            continue;
        }

        switch (st)
        {
        case ST_WAIT_SOF:
            if (b == SOF) st = ST_LEN;     /* anything else: silently discarded */
            break;

        case ST_LEN:
            len = b;  buf[0] = b;  got = 0;
            st = ST_CMD;
            break;

        case ST_CMD:
            buf[1] = b;
            st = (len > 0) ? ST_PAYLOAD : ST_CRC;
            break;

        case ST_PAYLOAD:
            buf[2 + got] = b;
            if (++got == len) { got = 0; st = ST_CRC; }
            break;

        case ST_CRC:
            crc_bytes[got] = b;
            if (++got == 4)
            {
                /* little-endian on the wire, per protocol.md */
                uint32_t rx_crc = (uint32_t)crc_bytes[0]
                                | ((uint32_t)crc_bytes[1] << 8)
                                | ((uint32_t)crc_bytes[2] << 16)
                                | ((uint32_t)crc_bytes[3] << 24);

                if (crc_compute(buf, 2 + len) == rx_crc)
                    handle_frame(buf[1], &buf[2], len);   /* handler sends ACK/NACK */
                else
                    send_byte(NACK);

                st = ST_WAIT_SOF;
            }
            break;
        }
    }
}
#define APP_BASE_ADDR  0x08008000UL   /* where the app's vector table lives */

static void bootloader_signature(void)
{
    /* 5 fast flashes on LD2 (PA5) so we can SEE the bootloader run */
    for (int i = 0; i < 5; i++)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        HAL_Delay(100);
    }
}

static void jump_to_app(void)
{
    /* 1. Read the app's two "notes" BEFORE we tear anything down.
          (uint32_t *)APP_BASE_ADDR      = "treat this number as an address of a 32-bit word"
          *(...)                          = "the value stored there"                    */
    uint32_t app_msp   = *(uint32_t *)(APP_BASE_ADDR);        /* note 1: initial stack  */
    uint32_t app_reset = *(uint32_t *)(APP_BASE_ADDR + 4U);   /* note 2: reset handler  */

    /* 2. Ignore all alarms from here on */
    __disable_irq();

    /* 3. Switch the alarm clock off completely (HAL_Init started it) */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 4. Undo the bootloader's peripheral setup */
    HAL_DeInit();

    /* 5. Hand over the stack */
    __set_MSP(app_msp);

    /* 6. Go. Cast note 2 into "a function taking nothing, returning nothing" and call it.
          This call never returns. */
    void (*app_entry)(void) = (void (*)(void))app_reset;
    app_entry();
}

static void update_mode(void)
{
    const char banner[] = "FlashForge bootloader v0.1 - update mode\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t *)banner, sizeof(banner) - 1, 100);

    crc_init();

    uint8_t  tv[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint32_t c = crc_compute(tv, 4);
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "CRC=%08lX\r\n", (unsigned long)c);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, n, 100);

    protocol_loop();


}
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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  bootloader_signature();
  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
  {
      /* button HELD (active-low: pressed = 0) -> stay resident */
      update_mode();
  }

  jump_to_app();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static void handle_frame(uint8_t cmd, uint8_t *payload, uint8_t len)
{
    switch (cmd)
    {
    case CMD_PING:
        send_byte(ACK);
        break;

    case CMD_ERASE:
        if (len != 1) { send_byte(NACK); break; }
        flash_unlock();
        {
            flash_status_t s = flash_erase_sector(payload[0]);
            flash_lock();
            send_byte(s == FLASH_OK ? ACK : NACK);
        }
        break;

    case CMD_WRITE:
        /* payload: 4-byte little-endian address + data (multiple of 4) */
        if (len < 8 || ((len - 4) % 4) != 0) { send_byte(NACK); break; }
        {
            uint32_t addr = (uint32_t)payload[0]
                          | ((uint32_t)payload[1] << 8)
                          | ((uint32_t)payload[2] << 16)
                          | ((uint32_t)payload[3] << 24);

            /* refuse to touch bootloader territory — belt AND braces */
            if (addr < 0x08008000UL) { send_byte(NACK); break; }

            flash_status_t s = FLASH_OK;
            flash_unlock();
            for (uint8_t i = 4; i < len && s == FLASH_OK; i += 4)
            {
                uint32_t w = (uint32_t)payload[i]
                           | ((uint32_t)payload[i+1] << 8)
                           | ((uint32_t)payload[i+2] << 16)
                           | ((uint32_t)payload[i+3] << 24);
                s = flash_write_word(addr + (i - 4), w);
            }
            flash_lock();
            send_byte(s == FLASH_OK ? ACK : NACK);
        }
        break;

    case CMD_GO:
        send_byte(ACK);          /* confirm BEFORE leaving, per protocol.md */
        HAL_Delay(10);           /* let the byte drain out of the UART */
        NVIC_SystemReset();      /* clean restart; bootloader re-checks and jumps */
        break;

    default:
        send_byte(NACK);
        break;
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
