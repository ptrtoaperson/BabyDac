#include "triggers.h"


void trig_dac_init(void)
{
    //  Enable Clock for GPIOA and DAC
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; // GPIOA clock
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;  // DAC clock

    // Configure PA4 and PA5 as Analog Mode
    // For F103: Analog mode is CNF=00, MODE=00
    GPIOA->CRL &= ~(GPIO_CRL_MODE4 | GPIO_CRL_CNF4 |
                    GPIO_CRL_MODE5 | GPIO_CRL_CNF5);

    // Enable DAC
    DAC->CR |= (DAC_CR_EN1 | DAC_CR_EN2);
}


void trig_set_level_A(uint16_t val) {
    // Channel 1 -> PA4 (12-bit, 0-4095)
    DAC->DHR12R1 = (val & 0x0FFF);

}


void trig_set_level_B(uint16_t val) {
    // Channel 2 -> PA5 (12-bit, 0-4095)
    DAC->DHR12R2 = (val & 0x0FFF);
}

// initialie the trigger pins as input

//initialize the toggle pins as output

// create an ISR for the trigger pins

//initialize PWM  to the toggle pins





