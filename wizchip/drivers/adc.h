#ifndef __ADC_H
#define __ADC_H

#include "stm32f1xx.h"

extern volatile uint16_t adc_data[1200];

extern uint8_t flag;
void adc_init_cont(void);
void adc_init(void);
float adc_read_voltage(uint8_t channel);

#endif