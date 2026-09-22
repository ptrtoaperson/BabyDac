
#if 1

#include "togglepins.h"
#include "stm32f1xx.h"
#include "parser.h"

#define TGP_SW_PWM_MAX_HZ 5000.0f

extern struct ltc268x_dev *p_dac;

// Counters and reload values per TGP bank and line.
volatile uint32_t tgp_counters[TGP_BANK_COUNT][TGP_PER_BANK] = {{0, 0, 0}, {0, 0, 0}};
volatile uint32_t tgp_high_reload[TGP_BANK_COUNT][TGP_PER_BANK] = {{0, 0, 0}, {0, 0, 0}};
volatile uint32_t tgp_low_reload[TGP_BANK_COUNT][TGP_PER_BANK] = {{0, 0, 0}, {0, 0, 0}};
volatile uint8_t tgp_is_high[TGP_BANK_COUNT][TGP_PER_BANK] = {{0, 0, 0}, {0, 0, 0}};

static inline void set_tgp_pin_level(uint8_t bank, uint8_t tgp_num, uint8_t high)
{
    if (bank == 0u) {
        if (tgp_num == 0u) {
            if (high) GPIOC->BSRR = GPIO_BSRR_BS6;
            else GPIOC->BSRR = GPIO_BSRR_BR6;
        } else if (tgp_num == 1u) {
            if (high) GPIOC->BSRR = GPIO_BSRR_BS7;
            else GPIOC->BSRR = GPIO_BSRR_BR7;
        } else if (tgp_num == 2u) {
            if (high) GPIOC->BSRR = GPIO_BSRR_BS8;
            else GPIOC->BSRR = GPIO_BSRR_BR8;
        }
    } else {
        if (tgp_num == 0u) {
            if (high) GPIOA->BSRR = GPIO_BSRR_BS0;
            else GPIOA->BSRR = GPIO_BSRR_BR0;
        } else if (tgp_num == 1u) {
            if (high) GPIOA->BSRR = GPIO_BSRR_BS1;
            else GPIOA->BSRR = GPIO_BSRR_BR1;
        } else if (tgp_num == 2u) {
            if (high) GPIOA->BSRR = GPIO_BSRR_BS2;
            else GPIOA->BSRR = GPIO_BSRR_BR2;
        }
    }
}

void LTC2686_TGP_Init(void) {
    // 1. Enable GPIOC, GPIOA and TIM2 clocks
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // 2. Configure bank 0 pins PC6, PC7, PC8 as General Purpose Output (Push-Pull, 50MHz)
    GPIOC->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    GPIOC->CRL |= GPIO_CRL_MODE6 | GPIO_CRL_MODE7; // Output mode, Max speed
    
    GPIOC->CRH &= ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8);
    GPIOC->CRH |= GPIO_CRH_MODE8;

    // 3. Configure bank 1 pins PA0, PA1, PA2 as General Purpose Output (Push-Pull, 50MHz)
    GPIOA->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0 |
                    GPIO_CRL_MODE1 | GPIO_CRL_CNF1 |
                    GPIO_CRL_MODE2 | GPIO_CRL_CNF2);
    GPIOA->CRL |= (GPIO_CRL_MODE0 | GPIO_CRL_MODE1 | GPIO_CRL_MODE2);

    // 4. Setup TIM2 for 100us intervals (10kHz interrupt)
    TIM2->PSC = 720 - 1;   // 72MHz / 720 = 100kHz clock
    TIM2->ARR = 10 - 1;    // 100kHz / 10 = 10kHz (100us)
    TIM2->DIER |= TIM_DIER_UIE; // Enable update interrupt
    
    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;

    for (uint8_t bank = 0; bank < TGP_BANK_COUNT; bank++) {
        for (uint8_t i = 0; i < TGP_PER_BANK; i++) {
            set_tgp_pin_level(bank, i, 0u);
        }
    }
}

/**
 * @brief Sets frequency for one TGP line in one bank.
 * @param bank 0 for LTC2686 group, 1 for LTC2688 group
 * @param tgp_num 0/1/2
 */
void Set_TGP_Frequency_Bank(uint8_t bank, uint8_t tgp_num, float frequency_hz) {
    Set_TGP_Frequency_Duty_Bank(bank, tgp_num, frequency_hz, 50u);
}

void Set_TGP_Frequency_Duty_Bank(uint8_t bank, uint8_t tgp_num, float frequency_hz, uint8_t duty_cycle_percent)
{
    if (bank >= TGP_BANK_COUNT || tgp_num >= TGP_PER_BANK) {
        return;
    }

    if (frequency_hz <= 0.00001f) {
        tgp_high_reload[bank][tgp_num] = 0;
        tgp_low_reload[bank][tgp_num] = 0;
        tgp_counters[bank][tgp_num] = 0;
        tgp_is_high[bank][tgp_num] = 0u;
        set_tgp_pin_level(bank, tgp_num, 0u);
        return;
    }

    if (frequency_hz > TGP_SW_PWM_MAX_HZ) {
        frequency_hz = TGP_SW_PWM_MAX_HZ;
    }

    if (duty_cycle_percent > 100u) {
        duty_cycle_percent = 100u;
    }

    // Base interrupt is 10kHz (100us/tick): period_ticks = 10000 / freq.
    float period_ticks_f = 10000.0f / frequency_hz;
    uint32_t period_ticks = (uint32_t)(period_ticks_f + 0.5f);
    if (period_ticks == 0u) {
        period_ticks = 1u;
    }

    if (duty_cycle_percent == 0u) {
        tgp_high_reload[bank][tgp_num] = 0u;
        tgp_low_reload[bank][tgp_num] = period_ticks;
        tgp_counters[bank][tgp_num] = 0u;
        tgp_is_high[bank][tgp_num] = 0u;
        set_tgp_pin_level(bank, tgp_num, 0u);
        return;
    }

    if (duty_cycle_percent >= 100u) {
        tgp_high_reload[bank][tgp_num] = period_ticks;
        tgp_low_reload[bank][tgp_num] = 0u;
        tgp_counters[bank][tgp_num] = 0u;
        tgp_is_high[bank][tgp_num] = 1u;
        set_tgp_pin_level(bank, tgp_num, 1u);
        return;
    }

    uint32_t high_ticks = (period_ticks * (uint32_t)duty_cycle_percent) / 100u;
    if (high_ticks == 0u) {
        high_ticks = 1u;
    }

    uint32_t low_ticks = period_ticks - high_ticks;
    if (low_ticks == 0u) {
        low_ticks = 1u;
    }

    tgp_high_reload[bank][tgp_num] = high_ticks;
    tgp_low_reload[bank][tgp_num] = low_ticks;
    tgp_counters[bank][tgp_num] = 0u;
    tgp_is_high[bank][tgp_num] = 1u;
    set_tgp_pin_level(bank, tgp_num, 1u);
}

// Legacy helper: keep old API mapped to bank 0.
void Set_TGP_Frequency(uint8_t tgp_num, float frequency_hz) {
    Set_TGP_Frequency_Bank(0, tgp_num, frequency_hz);
}

void LTC2686_SetPinFrequency(uint8_t tgp_num, float frequency_hz) {
    Set_TGP_Frequency_Bank(0, tgp_num, frequency_hz);
}

void LTC2686_SetPinTiming(uint8_t tgp_num, float frequency_hz) {
    Set_TGP_Frequency_Bank(0, tgp_num, frequency_hz);
}

static void sequencer_write_code(uint8_t logical_channel, uint16_t raw_code)
{
    uint8_t local_ch;
    enum ltc268x_device_id dev_id;
    ltc_dac_cs_t cs;

    if (logical_channel >= TOTAL_LOGICAL_CHANNELS) {
        return;
    }

    if (logical_channel < 8u) {
        local_ch = logical_channel;
        dev_id = LTC2686;
        cs = LTC_DAC_CS0;
    } else {
        local_ch = (uint8_t)(logical_channel - 8u);
        dev_id = LTC2688;
        cs = LTC_DAC_CS1;
    }

    ltc_write_dac_cs(LTC268X_CMD_CH_CODE_UPDATE(local_ch, dev_id), raw_code, cs);
    if (dev_id == LTC2686 && p_dac) {
        p_dac->dac_code[local_ch] = raw_code;
    }
}

static void sequencer_emit_internal_irq_if_needed(uint8_t slot)
{
    uint8_t emitted = 0u;

    // Preferred mode: per-follower IRQ divisors configured via command 13.
    for (uint8_t follower = 0u; follower < TOTAL_LOGICAL_CHANNELS; follower++) {
        uint16_t per_follower_every = seq_chain_irq_every[slot][follower];
        if (per_follower_every == 0u) {
            continue;
        }

        emitted = 1u;
        if ((seqs[slot].idx % per_follower_every) == 0u) {
            if (seq_internal_irq_pending[follower] < 0xFFFFu) {
                seq_internal_irq_pending[follower]++;
            }
        }
    }

    if (emitted) {
        return;
    }

    // Backward-compatible mode: shared irq_every_points for all followers.
    {
        uint16_t irq_every = seq_irq_every_points[slot];
        uint32_t chain_mask = seq_chain_mask[slot];

        if (irq_every == 0u) {
            return;
        }

        if (chain_mask == 0u && seq_chain_target[slot] < TOTAL_LOGICAL_CHANNELS) {
            chain_mask = (1u << seq_chain_target[slot]);
        }

        if (chain_mask == 0u) {
            return;
        }

        if ((seqs[slot].idx % irq_every) == 0u) {
            for (uint8_t follower = 0u; follower < TOTAL_LOGICAL_CHANNELS; follower++) {
                if ((chain_mask & (1u << follower)) == 0u) {
                    continue;
                }
                if (seq_internal_irq_pending[follower] < 0xFFFFu) {
                    seq_internal_irq_pending[follower]++;
                }
            }
        }
    }
}

static void sequencer_step_slot(uint8_t slot)
{
    uint16_t raw_code;

    if (!seqs[slot].active || seqs[slot].idx >= seqs[slot].limit) {
        return;
    }

    raw_code = seqs[slot].buffer[seqs[slot].idx++];
    sequencer_write_code(seqs[slot].target_dac, raw_code);
    sequencer_emit_internal_irq_if_needed(slot);

    if (seqs[slot].idx >= seqs[slot].limit) {
        if (seqs[slot].pending_valid) {
            seqs[slot].limit = seqs[slot].pending_limit;
            seqs[slot].delay_ticks = seqs[slot].pending_delay_ticks;
            seqs[slot].pending_valid = 0u;
            seqs[slot].timer_counter = 0u;
        } else {
            seqs[slot].active = 0u;
        }
    }
}

// Timer 2 Interrupt Handler
void TIM2_IRQHandler(void) {
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;

        // 1. PIN TOGGLING (PWM/TGP) via Software Bit-Bang for both banks
        for (int bank = 0; bank < (int)TGP_BANK_COUNT; bank++) {
            for (int i = 0; i < (int)TGP_PER_BANK; i++) {
                uint32_t high_ticks = tgp_high_reload[bank][i];
                uint32_t low_ticks = tgp_low_reload[bank][i];

                if (high_ticks == 0u && low_ticks == 0u) {
                    continue;
                }

                if (high_ticks == 0u) {
                    continue;
                }

                if (low_ticks == 0u) {
                    continue;
                }

                tgp_counters[bank][i]++;
                uint32_t phase_ticks = tgp_is_high[bank][i] ? high_ticks : low_ticks;
                if (tgp_counters[bank][i] >= phase_ticks) {
                    tgp_counters[bank][i] = 0;
                    tgp_is_high[bank][i] = (uint8_t)!tgp_is_high[bank][i];
                    set_tgp_pin_level((uint8_t)bank, (uint8_t)i, tgp_is_high[bank][i]);
                }
            }
        }

        // 2. SOFTWARE SEQUENCER
        // mode=1: time-based stepping from delay_ticks
        // mode=3: internal IRQ-based stepping from upstream channel events
        for (int j = 0; j < (int)TOTAL_LOGICAL_CHANNELS; j++) {
            if (!seqs[j].active) {
                continue;
            }

            if (seqs[j].mode == 1u) {
                seqs[j].timer_counter++;
                if (seqs[j].timer_counter >= seqs[j].delay_ticks) {
                    seqs[j].timer_counter = 0u;
                    sequencer_step_slot((uint8_t)j);
                }
            } else if (seqs[j].mode == 3u) {
                if (seq_internal_irq_pending[j] > 0u) {
                    seq_internal_irq_pending[j]--;
                    sequencer_step_slot((uint8_t)j);
                }
            }
        }
    }
}

#else
// (The unused TIM3 code block remains down here uncompiled)
#endif