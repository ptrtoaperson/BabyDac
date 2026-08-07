#ifndef _DELAY_H
#define _DELAY_H
#include "system_stm32f1xx.h"
#include "stm32f1xx.h"
void TIM2_Init(void);
void _us_delay(uint32_t us);

#endif