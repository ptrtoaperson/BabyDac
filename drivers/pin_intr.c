
#if 0
#include "pin_intr.h"
#include "uart.h"

extern void parser_handle_exti2_trigger(void);


void ExternIntInit(void)
{
    // Enable GPIOB and AFIO clocks
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPBEN;

    // Change PB1 to Input with Pull-up
    GPIOB->CRL &= ~(0xF << (1 * 4));
    GPIOB->CRL |= (0x8 << (1 * 4)); // Input with pull-up/down
    GPIOB->ODR |= (1 << 1);         // Select Pull-up

    // Map EXTI1 to PB1
    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI1;   // clear previous config
    AFIO->EXTICR[0] |= AFIO_EXTICR1_EXTI1_PB; // PB1 -> EXTI1

    // Configure EXTI1 for falling edge and unmask
    EXTI->IMR |= EXTI_IMR_MR1;
    EXTI->FTSR |= EXTI_FTSR_TR1;

    // Enable NVIC interrupt
    NVIC_SetPriority(EXTI1_IRQn, 0);
    NVIC_EnableIRQ(EXTI1_IRQn);
}


//W5500 interuupt
void EXTI1_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR1) {
        EXTI->PR = EXTI_PR_PR1; 
        thereISpkt = true;       
    }
}

#else 

#include "pin_intr.h"
#include "uart.h"

extern void parser_handle_exti2_trigger(void);

void ExternIntInit(void)
{
    // 1. Enable GPIOD and AFIO clocks
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPDEN;

    // 2. Change PD2 to Input with Pull-up
    // Pin 2 is in CRL. The shift math is: Pin * 4 -> 2 * 4 = 8
    GPIOD->CRL &= ~(0xF << (2 * 4));
    GPIOD->CRL |= (0x8 << (2 * 4)); // Input with pull-up/down
    GPIOD->ODR |= (1 << 2);         // Select Pull-up

    // 3. Map EXTI2 to PD2
    // EXTI2 lives in EXTICR[0] (which handles pins 0 to 3)
    AFIO->EXTICR[0] &= ~AFIO_EXTICR1_EXTI2;   // Clear previous config
    AFIO->EXTICR[0] |= AFIO_EXTICR1_EXTI2_PD; // PD2 -> EXTI2

    // 4. Configure EXTI2 for falling edge and unmask
    EXTI->IMR |= EXTI_IMR_MR2;
    EXTI->FTSR |= EXTI_FTSR_TR2;

    // 5. Enable NVIC interrupt for EXTI2
    NVIC_SetPriority(EXTI2_IRQn, 0);
    NVIC_EnableIRQ(EXTI2_IRQn);
}

// W5500 interrupt handler for PD2
void EXTI2_IRQHandler(void) {
    // Check specifically if line 2 triggered the interrupt
    if (EXTI->PR & EXTI_PR_PR2) {
        thereISpkt = true;
        parser_handle_exti2_trigger();
        EXTI->PR = EXTI_PR_PR2; // Clear the shared EXTI2 pending flag
    }
}

#endif