#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED

#define USE_HAL_PCD_REGISTER_CALLBACKS 0U
#define USE_HAL_RCC_REGISTER_CALLBACKS 0U

#ifndef HSE_VALUE
#define HSE_VALUE    8000000UL
#endif

#ifndef HSI_VALUE
#define HSI_VALUE    8000000UL
#endif

#ifndef LSE_VALUE
#define LSE_VALUE    32768UL
#endif

#ifndef LSI_VALUE
#define LSI_VALUE    40000UL
#endif

#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT 100UL
#endif

#ifndef LSE_STARTUP_TIMEOUT
#define LSE_STARTUP_TIMEOUT 5000UL
#endif

#ifndef VDD_VALUE
#define VDD_VALUE 3300UL
#endif

#ifndef TICK_INT_PRIORITY
#define TICK_INT_PRIORITY 0x0FUL
#endif

#ifndef USE_RTOS
#define USE_RTOS 0U
#endif

#ifndef PREFETCH_ENABLE
#define PREFETCH_ENABLE 1U
#endif

#define USE_HAL_ASSERT 0U

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f1xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f1xx_hal_gpio.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f1xx_hal_flash.h"
#include "stm32f1xx_hal_flash_ex.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f1xx_hal_cortex.h"
#endif
#ifdef HAL_PCD_MODULE_ENABLED
#include "stm32f1xx_hal_pcd.h"
#include "stm32f1xx_hal_pcd_ex.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f1xx_hal_pwr.h"
#endif

#ifndef assert_param
#define assert_param(expr) ((void)(expr))
#endif

extern const uint8_t AHBPrescTable[16];
extern const uint8_t APBPrescTable[8];

#ifdef __cplusplus
}
#endif
