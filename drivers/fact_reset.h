#ifndef FACT_RESET_H
#define FACT_RESET_H

#include "stm32f1xx.h"

uint16_t Increment_Reset_Counter(uint8_t clr);
uint16_t Get_Total_Reset_Presses(void);
uint32_t Get_Reset_Last_Delta_Sec(void);
uint32_t Get_Reset_Window_Sec(void);
uint8_t Was_Last_Reset_Pin(void);

#endif
