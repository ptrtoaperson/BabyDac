#include "stm32f1xx.h"
#include "system_stm32f1xx.h"

#include "HSE_CLK.h"
#if 1
#include "stm32f1xx.h"
#include "system_stm32f1xx.h"
#include "HSE_CLK.h"

void X_Clock_Init(void) {
    /* 1. Enable HSE (High Speed External clock) */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0); // Wait until HSE is ready

    /* 2. Configure Flash prefetch and latency */
    FLASH->ACR |= FLASH_ACR_PRFTBE;          // Enable prefetch buffer
    FLASH->ACR &= ~FLASH_ACR_LATENCY;        // Clear latency bits
    FLASH->ACR |= FLASH_ACR_LATENCY_2;       // 2 wait states for 72 MHz

    /* 3. Set AHB, APB1, and APB2 prescalers */
    RCC->CFGR &= ~(RCC_CFGR_HPRE   |          // AHB prescaler
                   RCC_CFGR_PPRE1  |          // APB1 prescaler
                   RCC_CFGR_PPRE2);          // APB2 prescaler

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;         // AHB = SYSCLK / 1 = 72 MHz
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;        // APB1 = HCLK / 2 = 36 MHz (max)
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;        // APB2 = HCLK / 1 = 72 MHz

    /* 4. Configure PLL: source = HSE, multiplier = 9 → 8 MHz × 9 = 72 MHz */
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC     |
                   RCC_CFGR_PLLXTPRE   |
                   RCC_CFGR_PLLMULL);
    
    // FIX: In stm32f1xx.h, setting the mask bit itself selects HSE
    RCC->CFGR |= RCC_CFGR_PLLSRC;            // PLL source = HSE (not divided)
    RCC->CFGR |= RCC_CFGR_PLLMULL9;          // PLL multiplier = 9

    /* 5. Enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0); // Wait until PLL is ready

    /* 6. Select PLL as system clock */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;            // Select PLL as system clock
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL); // Wait for switch

    /* 7. Stability Delay */
    for(volatile int i = 0; i < 4000000; i++); 


    /* 7. Stability Delay */
    for(volatile int i = 0; i < 4000000; i++); 

    /* 8. Update SystemCoreClock variable manually */
    SystemCoreClock = 72000000UL; // 72 MHz
}

#else
void X_Clock_Init(void) {
    /* 1. Enable HSE (High Speed External clock) */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0); // Wait until HSE is ready

    /* 2. Configure Flash prefetch and latency */
    // For 72 MHz, we need 2 wait states (Flash is slower than the CPU)
    FLASH->ACR |= FLASH_ACR_PRFTBE;          
    FLASH->ACR &= ~FLASH_ACR_LATENCY;        
    FLASH->ACR |= FLASH_ACR_LATENCY_2;       

    /* 3. Set AHB, APB1, and APB2 prescalers */
    RCC->CFGR &= ~(RCC_CFGR_HPRE   |          
                   RCC_CFGR_PPRE1  |          
                   RCC_CFGR_PPRE2);           

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1;         // AHB = 72 MHz
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;        // APB1 = 36 MHz (Max allowed is 36MHz)
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;        // APB2 = 72 MHz

    /* 4. Configure PLL for 16 MHz Crystal */
    // Math: (16 MHz HSE / 2) * 9 = 72 MHz
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC   |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL);

    RCC->CFGR |= RCC_CFGR_PLLSRC;            // HSE as PLL source
    RCC->CFGR |= RCC_CFGR_PLLXTPRE_HSE_DIV2; // Divide 16 MHz by 2 = 8 MHz
    RCC->CFGR |= RCC_CFGR_PLLMULL9;          // Multiply 8 MHz by 9 = 72 MHz

    /* 5. Enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);  // Wait until PLL is locked

    /* 6. Select PLL as system clock */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL); // Wait for switch

    /* 7. Stability Delay */
    // Use volatile so the compiler doesn't delete the loop during optimization
    for(volatile int i = 0; i < 4000000; i++); 

    /* 8. Update SystemCoreClock variable */
    // Using the CMSIS function is safer than hardcoding the value
    //SystemCoreClockUpdate(); 
}
#endif