#ifndef TRIGGERS_H
#define TRIGGERS_H


#include "stm32f1xx.h"


void trig_dac_init(void);
void trig_set_level_A(uint16_t val);
void trig_set_level_B(uint16_t val);

#endif