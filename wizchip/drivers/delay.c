#include "stm32f1xx.h"
#include "delay.h"
void TIM2_Init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->CR1 &= ~TIM_CR1_CEN; 
    // Assuming 72MHz System Clock: 72MHz / 72 = 1MHz (1 tick per 1us)
    TIM2->PSC = 72 - 1;        
    TIM2->CR1 |= TIM_CR1_OPM;  // One Pulse Mode
}

void _us_delay(uint32_t us) {
    if (us <= 1) return;
    TIM2->ARR = us - 1;
    TIM2->CNT = 0;
    TIM2->SR &= ~TIM_SR_UIF;
    TIM2->CR1 |= TIM_CR1_CEN;

    while (!(TIM2->SR & TIM_SR_UIF));
}