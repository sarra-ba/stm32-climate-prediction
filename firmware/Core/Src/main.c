/**
  **********************************************************************************************************************
  * @file    main.c
  * @author  MCD Application Team + User
  * @brief   STM32U5 IoT Webserver + ThingSpeak Demo with UART debug
  **********************************************************************************************************************
  */

/* Includes ----------------------------------------------------------------------------------------------------------*/
/* Includes ----------------------------------------------------------------------------------------------------------*/
#include "webserver_http_response.h"
#include "webserver_http_encoder.h"
#include "net_connect.h"
#include "net_interface.h"
#include "mx_wifi.h"
#include "thingspeak.h"
#include "store_forward.h"  

/* Private function prototypes ---------------------------------------------------------------------------------------*/
void SystemClock_Config(void);
void Error_Handler(void);

/* Main program ------------------------------------------------------------------------------------------------------*/
int main(void)
{
    /* HAL initialization */
    HAL_Init();

    /* Enable instruction cache (important for performance) */
    instruction_cache_enable();

    /* Configure system clock */
    SystemClock_Config();

    /* Initialize board support package (critical for EMW3080 Wi-Fi and peripherals) */
    bsp_init();

    /* Initialize console UART for debug prints */
    if (webserver_console_config() != WEBSERVER_OK)
    {
        Error_Handler();
    }

    /* Debug banner */
    printf("\r\n\r\n");
    printf("===========================================\r\n");
    printf("  STM32 IoT Webserver + ThingSpeak Demo  \r\n");
    printf("===========================================\r\n");
    printf("UART Debug Active. Starting application...\r\n\r\n");

    /* Start the webserver application */
    app_entry();

    /* Should never reach here */
    while (1)
    {
    }
}

/* System Clock Configuration - 120 MHz -----------------------------------------------------------------------------*/
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Oscillators */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
    RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV4;
    RCC_OscInitStruct.PLL.PLLM = 3;
    RCC_OscInitStruct.PLL.PLLN = 15;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }

    /* Configure the Systick */
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
}

/* Error Handler ----------------------------------------------------------------------------------------------------*/
void Error_Handler(void)
{
    printf("ERROR: System error occurred!\r\n");
    __disable_irq();
    while (1)
    {
    }
}
