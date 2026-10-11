#include "stm32f4xx_hal.h"
#include "canbus.hpp"

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscInit = {};
    RCC_ClkInitTypeDef clkInit = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    oscInit.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscInit.HSEState = RCC_HSE_BYPASS;
    oscInit.PLL.PLLState = RCC_PLL_ON;
    oscInit.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscInit.PLL.PLLM = 4;
    oscInit.PLL.PLLN = 180;
    oscInit.PLL.PLLP = RCC_PLLP_DIV2;
    oscInit.PLL.PLLQ = 7;
    HAL_RCC_OscConfig(&oscInit);

    clkInit.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clkInit.APB1CLKDivider = RCC_HCLK_DIV4;
    clkInit.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_5);
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpioInit = {};
    gpioInit.Pin = GPIO_PIN_0;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpioInit);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    CanBus canBus;
    canBus.start();

    uint8_t txData[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

    while (true) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
        canBus.processRx(CanChannel::HighSpeed);
        canBus.processRx(CanChannel::MediumSpeed);
        canBus.transmit(0x123, txData, sizeof(txData));
        HAL_Delay(100);
    }

    return 0;
}

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}
