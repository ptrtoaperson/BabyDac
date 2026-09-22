#include "parser.h"
#include "ltc268x.h" // The Analog Devices header file
#include "triggers.h"
#include "lantask.h"
#include "wizchip_conf.h"
#include "net_config.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef int (*fptr)(char *);

fptr lfptr[] = {&set_span, &set_voltage, &start_pwm_hw, &stop_pwm_hw, &set_sequence, &set_soft_sequence, &reset, &set_trigger_level, &write_ip_config, &clear_state, &set_soft_sequence_chained, &stop_sequence_channel, &set_chain_followers, &set_chain_followers_with_irq};

// Global device handle allocated by the initialization cycle
struct ltc268x_dev *p_dac = NULL;

#define DAC0_CHANNELS 8u
#define DAC1_CHANNELS 16u
#define TOTAL_DAC_CHANNELS (DAC0_CHANNELS + DAC1_CHANNELS)

// Trigger DAC calibration model:
// V_trig = TRIG_STAGE_GAIN * (x - TRIG_STAGE_OFFSET), x is DAC pin voltage.
// Tune these three values from bench measurements if needed.
// Re-calibrated from bench points: cmd 3.3V->meas 3.33V and cmd 7.0V->meas 7.24V.
#define TRIG_STAGE_GAIN   6.199f
#define TRIG_STAGE_OFFSET 1.652f
#define TRIG_DAC_VREF     3.3f

// Global tracker for AB_SELECT register state (which register is active per channel)
// Bit N = 0 means Register A selected, Bit N = 1 means Register B selected
static uint16_t ab_reg_state[2] = {0, 0};
static uint16_t dither_toggle_state[2] = {0, 0};
static uint8_t seq_slot_edge[TOTAL_LOGICAL_CHANNELS] = {0};
static uint8_t seq_trigger_pin[TOTAL_LOGICAL_CHANNELS] = {0};
static uint8_t seq_prev_level[2] = {0xFFu, 0xFFu}; // [0]=EXTI2(PD2), [1]=EXTI3(PC3)
volatile uint16_t seq_internal_irq_pending[TOTAL_LOGICAL_CHANNELS] = {0u};
uint16_t seq_irq_every_points[TOTAL_LOGICAL_CHANNELS] = {0u};
uint8_t seq_chain_target[TOTAL_LOGICAL_CHANNELS] = {0u};
uint32_t seq_chain_mask[TOTAL_LOGICAL_CHANNELS] = {0u};
uint16_t seq_chain_irq_every[TOTAL_LOGICAL_CHANNELS][TOTAL_LOGICAL_CHANNELS] = {{0u}};

#define HWSEQ_PER_TRIGGER 8u
static uint8_t trig2_seq_dacs[HWSEQ_PER_TRIGGER]; // EXTI2 (PD2)
static uint8_t trig3_seq_dacs[HWSEQ_PER_TRIGGER]; // EXTI3 (PC3)
static uint8_t trig2_seq_count = 0u;
static uint8_t trig3_seq_count = 0u;

// Global tracker array to compute raw code -> float voltage conversions on-the-fly
static enum ltc268x_voltage_range dac_ranges[TOTAL_DAC_CHANNELS] = {
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V,
    LTC268X_VOLTAGE_RANGE_M10V_10V, LTC268X_VOLTAGE_RANGE_M10V_10V
};

typedef struct {
    uint8_t local_ch;
    enum ltc268x_device_id dev_id;
    ltc_dac_cs_t cs;
    uint8_t state_idx;
} dac_route_t;

static int map_logical_channel(uint8_t logical_ch, dac_route_t *route)
{
    if (!route)
        return -1;

    if (logical_ch < DAC0_CHANNELS) {
        route->local_ch = logical_ch;
        route->dev_id = LTC2686;
        route->cs = LTC_DAC_CS0;
        route->state_idx = 0;
        return 0;
    }

    if (logical_ch < TOTAL_DAC_CHANNELS) {
        route->local_ch = (uint8_t)(logical_ch - DAC0_CHANNELS);
        route->dev_id = LTC2688;
        route->cs = LTC_DAC_CS1;
        route->state_idx = 1;
        return 0;
    }

    return -1;
}

// Clean helper block to scale binary integers to specific analog voltages
static float code_to_voltage(uint16_t code, enum ltc268x_voltage_range range) {
    float min_v = 0.0f;
    float max_v = 5.0f;

    switch (range) {
        case LTC268X_VOLTAGE_RANGE_0V_5V:    min_v = 0.0f;   max_v = 5.0f;   break;
        case LTC268X_VOLTAGE_RANGE_0V_10V:   min_v = 0.0f;   max_v = 10.0f;  break;
        case LTC268X_VOLTAGE_RANGE_M5V_5V:   min_v = -5.0f;  max_v = 5.0f;   break;
        case LTC268X_VOLTAGE_RANGE_M10V_10V:  min_v = -10.0f; max_v = 10.0f;  break;
        case LTC268X_VOLTAGE_RANGE_M15V_15V:  min_v = -15.0f; max_v = 15.0f;  break;
    }
    return min_v + (((float)code / 65535.0f) * (max_v - min_v));
}

uint16_t trig_voltage_to_code(float v_trig)
{
    // Hardware transfer:
    // V_trig = G * (x - O), where x is DAC pin voltage.
    // x = (V_trig / G) + O
    float x = (v_trig / TRIG_STAGE_GAIN) + TRIG_STAGE_OFFSET;

    if (x < 0.0f) {
        x = 0.0f;
    } else if (x > TRIG_DAC_VREF) {
        x = TRIG_DAC_VREF;
    }

    // Round to nearest code (not truncation) for lower static error.
    float code_f = (x / TRIG_DAC_VREF) * 4095.0f;
    uint16_t code = (uint16_t)(code_f + 0.5f);

    if (code > 0x0FFFu) {
        code = 0x0FFFu;
    }
    return code;
}

static void compact_trigger_slots(uint8_t pin)
{
    uint8_t *slot_list = NULL;
    uint8_t *slot_count = NULL;

    if (pin == 2u) {
        slot_list = trig2_seq_dacs;
        slot_count = &trig2_seq_count;
    } else if (pin == 3u) {
        slot_list = trig3_seq_dacs;
        slot_count = &trig3_seq_count;
    } else {
        return;
    }

    uint8_t out = 0u;
    for (uint8_t i = 0u; i < *slot_count; i++) {
        uint8_t dac = slot_list[i];
        if (dac >= TOTAL_LOGICAL_CHANNELS) {
            continue;
        }
        if (seqs[dac].active && seqs[dac].mode == 2u && seq_trigger_pin[dac] == pin) {
            slot_list[out++] = dac;
        }
    }
    for (uint8_t i = out; i < HWSEQ_PER_TRIGGER; i++) {
        slot_list[i] = 0xFFu;
    }
    *slot_count = out;
}

static void unregister_dac_from_trigger_slots(uint8_t dac)
{
    for (uint8_t i = 0u; i < trig2_seq_count; i++) {
        if (trig2_seq_dacs[i] == dac) {
            trig2_seq_dacs[i] = 0xFFu;
        }
    }
    for (uint8_t i = 0u; i < trig3_seq_count; i++) {
        if (trig3_seq_dacs[i] == dac) {
            trig3_seq_dacs[i] = 0xFFu;
        }
    }

    compact_trigger_slots(2u);
    compact_trigger_slots(3u);
}

static uint8_t register_dac_for_trigger(uint8_t pin, uint8_t dac)
{
    uint8_t *slot_list = NULL;
    uint8_t *slot_count = NULL;

    if (pin == 2u) {
        slot_list = trig2_seq_dacs;
        slot_count = &trig2_seq_count;
    } else if (pin == 3u) {
        slot_list = trig3_seq_dacs;
        slot_count = &trig3_seq_count;
    } else {
        return 0u;
    }

    for (uint8_t i = 0u; i < *slot_count; i++) {
        if (slot_list[i] == dac) {
            return 1u;
        }
    }

    compact_trigger_slots(pin);

    if (*slot_count >= HWSEQ_PER_TRIGGER) {
        return 0u;
    }

    slot_list[*slot_count] = dac;
    (*slot_count)++;
    return 1u;
}

// One sequencer slot per logical channel (0..23)
Sequencer_t seqs[TOTAL_LOGICAL_CHANNELS];

void Sequencer_Init(void) {
    for (int i = 0; i < (int)TOTAL_LOGICAL_CHANNELS; i++) {
        seqs[i].active = 0;
        seqs[i].idx = 0;
        seqs[i].limit = 0;
        seqs[i].pending_limit = 0;
        seqs[i].delay_ticks = 0;
        seqs[i].pending_delay_ticks = 0;
        seqs[i].timer_counter = 0;
        seqs[i].target_dac = 0;
        seqs[i].pending_valid = 0;
        seqs[i].mode = 0;
        seq_internal_irq_pending[i] = 0u;
        seq_irq_every_points[i] = 0u;
        seq_chain_target[i] = 0xFFu;
        seq_chain_mask[i] = 0u;
        for (int j = 0; j < (int)TOTAL_LOGICAL_CHANNELS; j++) {
            seq_chain_irq_every[i][j] = 0u;
        }
    }

    trig2_seq_count = 0u;
    trig3_seq_count = 0u;
    for (int i = 0; i < (int)HWSEQ_PER_TRIGGER; i++) {
        trig2_seq_dacs[i] = 0xFFu;
        trig3_seq_dacs[i] = 0xFFu;
    }

    for (int i = 0; i < (int)TOTAL_LOGICAL_CHANNELS; i++) {
        seq_slot_edge[i] = 0u;
        seq_trigger_pin[i] = 0xFFu;
    }
}

uint8_t parse_command(uint8_t *dptr, uint16_t len) {
    //uart1_print("reached command parser\r\n");
    uint16_t pos = 0;

    while (pos < len) {
        uint8_t inst = dptr[pos];

        // Ignore optional trailing delimiter used by host framing.
        if (inst == 'e') {
            break;
        }

        if (inst > 13u) {
            break;
        }

        uint16_t expected_len = 0;
        if (inst == 4u || inst == 5u) {
            if ((uint16_t)(len - pos) < 6u) {
                break;
            }
            uint16_t count = (uint16_t)dptr[pos + 4u] | ((uint16_t)dptr[pos + 5u] << 8);
            expected_len = (uint16_t)(6u + (count * 2u));
        } else if (inst == 10u) {
            if ((uint16_t)(len - pos) < 10u) {
                break;
            }
            uint16_t count = (uint16_t)dptr[pos + 4u] | ((uint16_t)dptr[pos + 5u] << 8);
            expected_len = (uint16_t)(10u + (count * 2u));
        } else if (inst == 12u) {
            if ((uint16_t)(len - pos) < CHAINMAP_MIN_CMD_LEN) {
                break;
            }
            uint16_t follower_count = (uint16_t)dptr[pos + 2u];
            expected_len = (uint16_t)(CHAINMAP_MIN_CMD_LEN + follower_count);
        } else if (inst == 13u) {
            if ((uint16_t)(len - pos) < CHAINMAP_IRQ_MIN_CMD_LEN) {
                break;
            }
            uint16_t follower_count = (uint16_t)dptr[pos + 2u];
            expected_len = (uint16_t)(CHAINMAP_IRQ_MIN_CMD_LEN + (follower_count * 3u));
        } else if (inst == 6u) {
            expected_len = 4u;
        } else if (inst == 7u) {
            expected_len = TRIGDAC_CMD_LEN;
        } else if (inst == 8u) {
            expected_len = WRITE_IP_CMD_LEN;
        } else if (inst == 9u) {
            expected_len = 1u;
        } else if (inst == 11u) {
            expected_len = STOPSEQ_CMD_LEN;
        } else {
            // IDs 0..3 use fixed 4/4/12/3 byte payloads as returned by their handlers.
            static const uint16_t fixed_len[4] = {4u, 4u, 12u, 3u};
            expected_len = fixed_len[inst];
        }

        if ((uint16_t)(len - pos) < expected_len) {
            break;
        }

        fptr callptr = lfptr[inst];
        int consumed = callptr((char *)&dptr[pos]);
        if (consumed <= 0) {
            break;
        }

        pos = (uint16_t)(pos + (uint16_t)consumed);
    }
    return 1;
}

int reset(char* args) {
    if (p_dac) {
        ltc268x_software_reset(p_dac);
    }
    ltc_write_dac_cs(LTC268X_CMD_CONFIG_REG, LTC268X_CONFIG_RST, LTC_DAC_CS1);
    return 4; 
}

int clear_state(char *args)
{
    (void)args;

    // Stop all TGP generators on both banks.
    for (uint8_t bank = 0u; bank < TGP_BANK_COUNT; bank++) {
        for (uint8_t line = 0u; line < TGP_PER_BANK; line++) {
            Set_TGP_Frequency_Bank(bank, line, 0.0f);
        }
    }

    // Disable toggle/dither enables and reset A/B register select shadow.
    dither_toggle_state[0] = 0u;
    dither_toggle_state[1] = 0u;
    ab_reg_state[0] = 0u;
    ab_reg_state[1] = 0u;
    ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG, 0x0000, LTC_DAC_CS0);
    ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG, 0x0000, LTC_DAC_CS1);
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, 0x0000, LTC_DAC_CS0);
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, 0x0000, LTC_DAC_CS1);

    // Force all channels to 0V output (A/B both midscale), in normal mode.
    for (uint8_t n_dac = 0u; n_dac < TOTAL_DAC_CHANNELS; n_dac++) {
        dac_route_t route;
        if (map_logical_channel(n_dac, &route) < 0) {
            continue;
        }

        uint8_t reg_setting = LTC268X_CMD_CH_SETTING(route.local_ch, route.dev_id);
        uint8_t reg_code = LTC268X_CMD_CH_CODE(route.local_ch, route.dev_id);
        uint8_t reg_update = LTC268X_CMD_CH_CODE_UPDATE(route.local_ch, route.dev_id);
        uint16_t dac_bit = (uint16_t)(1u << LTC268X_CHANNEL_SEL(route.local_ch, route.dev_id));

        // Write A register = 0V code.
        ab_reg_state[route.state_idx] &= ~dac_bit;
        ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
        ltc_write_dac_cs(reg_code, 0x8000, route.cs);

        // Write B register = 0V code.
        ab_reg_state[route.state_idx] |= dac_bit;
        ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
        ltc_write_dac_cs(reg_code, 0x8000, route.cs);

        // Return to A, force normal mode, and update output now.
        ab_reg_state[route.state_idx] &= ~dac_bit;
        ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
        ltc_write_dac_cs(reg_setting,
                         LTC268X_CH_MODE |
                         LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V) |
                         LTC268X_CH_TD_SEL(LTC268X_SOFT_TGL),
                         route.cs);
        ltc_write_dac_cs(reg_update, 0x8000, route.cs);

        dac_ranges[n_dac] = LTC268X_VOLTAGE_RANGE_M10V_10V;
        if (route.dev_id == LTC2686 && p_dac) {
            p_dac->dac_code[route.local_ch] = 0x8000;
        }
    }

    // Reset sequencer runtime state and trigger slot bookkeeping.
    Sequencer_Init();
    memset(seq_slot_edge, 0, sizeof(seq_slot_edge));
    memset(seq_trigger_pin, 0xFF, sizeof(seq_trigger_pin));
    memset((void *)seq_internal_irq_pending, 0, sizeof(seq_internal_irq_pending));
    memset(seq_irq_every_points, 0, sizeof(seq_irq_every_points));
    memset(seq_chain_target, 0xFF, sizeof(seq_chain_target));
    memset(seq_chain_mask, 0, sizeof(seq_chain_mask));
    memset(seq_chain_irq_every, 0, sizeof(seq_chain_irq_every));
    seq_prev_level[0] = 0xFFu;
    seq_prev_level[1] = 0xFFu;

    // Disable external trigger interrupts for sequence engine.
    EXTI->IMR &= ~((1u << 2) | (1u << 3));
    EXTI->RTSR &= ~((1u << 2) | (1u << 3));
    EXTI->FTSR &= ~((1u << 2) | (1u << 3));
    EXTI->PR = (1u << 2) | (1u << 3);
    NVIC_DisableIRQ(EXTI2_IRQn);
    NVIC_DisableIRQ(EXTI3_IRQn);

    return 1;
}

int stop_sequence_channel(char *args)
{
    STOPSEQcommand_t *cmd = (STOPSEQcommand_t *)args;
    uint8_t dac = cmd->N_DAC;
    uint8_t flags = cmd->FLAGS;

    if (dac >= TOTAL_LOGICAL_CHANNELS) {
        return STOPSEQ_CMD_LEN;
    }

    __disable_irq();
    seqs[dac].active = 0u;
    seqs[dac].idx = 0u;
    seqs[dac].limit = 0u;
    seqs[dac].pending_limit = 0u;
    seqs[dac].pending_delay_ticks = 0u;
    seqs[dac].pending_valid = 0u;
    seqs[dac].timer_counter = 0u;
    seqs[dac].mode = 0u;

    seq_internal_irq_pending[dac] = 0u;
    seq_irq_every_points[dac] = 0u;
    seq_chain_target[dac] = 0xFFu;
    seq_chain_mask[dac] = 0u;
    memset(seq_chain_irq_every[dac], 0, sizeof(seq_chain_irq_every[dac]));
    seq_slot_edge[dac] = 0u;
    seq_trigger_pin[dac] = 0xFFu;
    unregister_dac_from_trigger_slots(dac);
    __enable_irq();

    // Optional behavior: force channel output to 0V after stop.
    if ((flags & 0x01u) != 0u) {
        dac_route_t route;
        if (map_logical_channel(dac, &route) == 0) {
            ltc_write_dac_cs(LTC268X_CMD_CH_CODE_UPDATE(route.local_ch, route.dev_id), 0x8000, route.cs);
            if (route.dev_id == LTC2686 && p_dac) {
                p_dac->dac_code[route.local_ch] = 0x8000;
            }
        }
    }

    return STOPSEQ_CMD_LEN;
}

int set_span(char *args) {
    uint8_t n_dac = args[1];
    uint8_t span  = args[2];
    dac_route_t route;

    if (map_logical_channel(n_dac, &route) < 0) return 4;
    if (span > 4) span = LTC268X_VOLTAGE_RANGE_M10V_10V; // Boundary fallback safeguard

    if (route.dev_id == LTC2686 && p_dac) {
        ltc268x_set_span(p_dac, route.local_ch, (enum ltc268x_voltage_range)span);
    } else {
        uint8_t reg_setting = LTC268X_CMD_CH_SETTING(route.local_ch, route.dev_id);
        uint16_t ch_setting = LTC268X_CH_SPAN(span);
        ltc_write_dac_cs(reg_setting, ch_setting, route.cs);
    }
    dac_ranges[n_dac] = (enum ltc268x_voltage_range)span;

    return 4;
}

int set_voltage(char *args)
{
    Vcommand_t *cmd = (Vcommand_t *)args;
    dac_route_t route;

    if (!p_dac) return sizeof(*cmd);
    if (map_logical_channel(cmd->N_DAC, &route) < 0) return sizeof(*cmd);

    uint8_t dac = route.local_ch;
    uint16_t voltCode = cmd->CODE;

    uint8_t reg_code = LTC268X_CMD_CH_CODE_UPDATE(dac, route.dev_id);

    ltc_write_dac_cs(reg_code, voltCode, route.cs);

    if (route.dev_id == LTC2686) {
        p_dac->dac_code[dac] = voltCode;
    }

    //uart1_print("reached set voltage\r\n");

    return sizeof(*cmd);
}

int set_trigger_level(char *args)
{
    TRIGDACcommand_t *cmd = (TRIGDACcommand_t *)args;

    uint16_t code12 = trig_voltage_to_code(cmd->V_TRIG);

    if (cmd->N_TRIG == 0u) {
        trig_set_level_A(code12);
    } else if (cmd->N_TRIG == 1u) {
        trig_set_level_B(code12);
    } else {
        return TRIGDAC_CMD_LEN;
    }

    return TRIGDAC_CMD_LEN;
}

int write_ip_config(char *args)
{
    WriteIPcommand_t *cmd = (WriteIPcommand_t *)args;
    char prntbuf[96];
    uint8_t ip[4] = {
        cmd->IPaddr[0],
        cmd->IPaddr[1],
        cmd->IPaddr[2],
        cmd->IPaddr[3]
    };
    uint16_t port = cmd->PORT;

    snprintf(prntbuf,
             sizeof(prntbuf),
             "Persist net cfg %u.%u.%u.%u:%u\r\n",
             ip[0], ip[1], ip[2], ip[3], port);
    uart1_print(prntbuf);

    if (netcfg_save(ip, port) != 0) {
        uart1_print("Flash save failed\r\n");
        return WRITE_IP_CMD_LEN;
    }

    if (!netcfg_load(ip, &port)) {
        uart1_print("Flash verify failed\r\n");
        return WRITE_IP_CMD_LEN;
    }

    wiz_NetInfo netinfo;
    wizchip_getnetinfo(&netinfo);
    netinfo.ip[0] = ip[0];
    netinfo.ip[1] = ip[1];
    netinfo.ip[2] = ip[2];
    netinfo.ip[3] = ip[3];
    wizchip_setnetinfo(&netinfo);

    lan_set_tcp_port(port);
    lan_reopen_listener();

    snprintf(prntbuf,
             sizeof(prntbuf),
             "Applied net cfg %u.%u.%u.%u:%u\r\n",
             ip[0], ip[1], ip[2], ip[3], port);
    uart1_print(prntbuf);

    return WRITE_IP_CMD_LEN;
}



#if 1

int start_pwm_hw(char *args)
{
    STARTPWMcommand_t *cmd = (STARTPWMcommand_t *)args;
    dac_route_t route;
    uint8_t duty = cmd->DUTY;

    if (!p_dac) return sizeof(*cmd);
    if (map_logical_channel(cmd->N_DAC, &route) < 0) return sizeof(*cmd);
    if (cmd->N_TGP > 2) return sizeof(*cmd);

    // Backward compatibility: old host packet used this byte as padding (zero).
    if (duty == 0u) {
        duty = 50u;
    }
    if (duty > 100u) {
        duty = 100u;
    }

    uint8_t dac = route.local_ch;
    uint16_t dac_bit = (uint16_t)(1u << LTC268X_CHANNEL_SEL(dac, route.dev_id));

    char buffer[96];

        sprintf(buffer, "PWM START DAC=%d TGP=%d DUTY=%u LOW=%u HIGH=%u\r\n",
            cmd->N_DAC, cmd->N_TGP, duty, cmd->LOW, cmd->HIGH);
    uart1_print(buffer);

        sprintf(buffer, "PWM ROUTE bank=%u line=%u\r\n",
            route.state_idx, cmd->N_TGP);
        uart1_print(buffer);

        uint8_t reg_setting = LTC268X_CMD_CH_SETTING(dac, route.dev_id);
        uint8_t reg_code    = LTC268X_CMD_CH_CODE(dac, route.dev_id);

    sprintf(buffer, "reg_setting=%u reg_code=%u dac_bit=0x%04X\r\n",
            reg_setting, reg_code, dac_bit);
    uart1_print(buffer);

    /* Disable toggle for this DAC while configuring. */
    dither_toggle_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                     dither_toggle_state[route.state_idx], route.cs);

    /* Force span to +/-10 V. */
    if (route.dev_id == LTC2686) {
        ltc268x_set_span(p_dac, dac, LTC268X_VOLTAGE_RANGE_M10V_10V);
    } else {
        ltc_write_dac_cs(reg_setting, LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V), route.cs);
    }
    dac_ranges[cmd->N_DAC] = LTC268X_VOLTAGE_RANGE_M10V_10V;

    /* Select Input Register A and write LOW code. */
    ab_reg_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
    ltc_write_dac_cs(reg_code, cmd->LOW, route.cs);

    /* Select Input Register B and write HIGH code. */
    ab_reg_state[route.state_idx] |= dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
    ltc_write_dac_cs(reg_code, cmd->HIGH, route.cs);

    /*
     * MODE = 0: toggle mode.
     * TD_SEL: 0=software toggle, 1=TGP0, 2=TGP1, 3=TGP2.
     * If cmd->N_TGP is 0/1/2, add 1 to select TGP0/1/2.
     */
    uint16_t ch_setting =
        LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V) |
        LTC268X_CH_TD_SEL((cmd->N_TGP + 1u) & 0x3u);

    ltc_write_dac_cs(reg_setting, ch_setting, route.cs);

    /* Optional: return write access to A after both A/B values are loaded. */
    ab_reg_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);

    /* Start the selected TGP line on the DAC's corresponding bank. */
    Set_TGP_Frequency_Duty_Bank(route.state_idx, cmd->N_TGP, cmd->FREQ, duty);

    /* Enable toggle for this DAC. */
        dither_toggle_state[route.state_idx] |= dac_bit;
        ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                 dither_toggle_state[route.state_idx], route.cs);

    sprintf(buffer, "PWM RUNNING DAC=%d AB=0x%04X EN=0x%04X CH=0x%04X\r\n",
            cmd->N_DAC, ab_reg_state[route.state_idx], dither_toggle_state[route.state_idx], ch_setting);
    uart1_print(buffer);

    return sizeof(*cmd);
}

#else

int start_pwm_hw(char *args)
{
    STARTPWMcommand_t *cmd = (STARTPWMcommand_t *)args;

    if (!p_dac) return sizeof(*cmd);
    if (cmd->N_DAC > 7) return sizeof(*cmd);

    uint8_t dac = cmd->N_DAC;

    char buffer[80];
    sprintf(buffer, "PWM START DAC=%d TGP=%d LOW=%u HIGH=%u\r\n",
            dac, cmd->N_TGP, cmd->LOW, cmd->HIGH);
    uart1_print(buffer);

    // Correct register mapping (NO shadowing)
    uint8_t reg_setting = LTC268X_CMD_CH_SETTING(dac, 0 );
    uint8_t reg_code    = LTC268X_CMD_CH_CODE(dac, 0);

    sprintf(buffer,"reg_setting: %d, reg_code: %d", reg_setting, reg_code);
    uart1_print(buffer);



    // Disable toggle
    p_dac->dither_toggle_en &= ~(1 << dac);
    ltc_write_dac(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                  p_dac->dither_toggle_en);

    // Force span
    ltc268x_set_span(p_dac, dac,
        LTC268X_VOLTAGE_RANGE_M10V_10V);

    // ALWAYS force A first
    ab_reg_state &= ~(1 << dac);
    ltc_write_dac(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state);

    ltc_write_dac(reg_code, cmd->HIGH);

    // switch to B
    ab_reg_state |= (1 << dac);
    ltc_write_dac(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state);

    ltc_write_dac(reg_code, cmd->LOW);

    // FIXED CH_SETTING (IMPORTANT: include MODE=0 explicitly)
    uint16_t ch_setting =
        LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V) |
        LTC268X_CH_TD_SEL(cmd->N_TGP +1 & 0x3);

    ltc_write_dac(reg_setting, ch_setting);

    // return to A
    //ab_reg_state &= ~(1 << dac);
    //ltc_write_dac(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state);

    // enable toggle
    p_dac->dither_toggle_en |= (1 << dac);
    ltc_write_dac(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                  p_dac->dither_toggle_en);

    Set_TGP_Frequency(cmd->N_TGP, cmd->FREQ);

    sprintf(buffer, "PWM RUNNING DAC=%d\r\n", dac);
    uart1_print(buffer);

    return sizeof(*cmd);
}
#endif

int stop_pwm_hw(char *args)
{
    STOPPWMcommand_t *cmd = (STOPPWMcommand_t *)args;
    dac_route_t route;

    if (!p_dac) return sizeof(*cmd);
    if (map_logical_channel(cmd->N_DAC, &route) < 0) return sizeof(*cmd);

    uint8_t reg_setting = LTC268X_CMD_CH_SETTING(route.local_ch, route.dev_id);
    uint8_t reg_code = LTC268X_CMD_CH_CODE(route.local_ch, route.dev_id);
    uint8_t reg_update = LTC268X_CMD_CH_CODE_UPDATE(route.local_ch, route.dev_id);
    uint16_t dac_bit = (uint16_t)(1u << LTC268X_CHANNEL_SEL(route.local_ch, route.dev_id));

    /* Disable toggle only for this DAC channel. */
    dither_toggle_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                     dither_toggle_state[route.state_idx], route.cs);

    /* Program both A/B registers to 0V so future mode transitions are deterministic. */
    ab_reg_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
    ltc_write_dac_cs(reg_code, 0x8000, route.cs);

    ab_reg_state[route.state_idx] |= dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
    ltc_write_dac_cs(reg_code, 0x8000, route.cs);

    /* Return to A and force normal mode (MODE=1), then update output to 0V now. */
    ab_reg_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_A_B_SELECT_REG, ab_reg_state[route.state_idx], route.cs);
    ltc_write_dac_cs(reg_setting,
                     LTC268X_CH_MODE |
                     LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V) |
                     LTC268X_CH_TD_SEL(LTC268X_SOFT_TGL),
                     route.cs);
    ltc_write_dac_cs(reg_update, 0x8000, route.cs);

    // Optional: stop a specific TGP line only when explicitly provided.
    // 0xFF means "channel-only stop" (do not alter shared TGP generators).
    if (cmd->N_TGP < TGP_PER_BANK) {
        Set_TGP_Frequency_Bank(route.state_idx, cmd->N_TGP, 0.0f);
    }

    return sizeof(*cmd);
}


int set_soft_sequence(char *args) {
    // Cast the raw byte argument pointer directly to your packed struct layout
    SOFTSEQcommand_t *cmd = (SOFTSEQcommand_t *)args;
    dac_route_t route;
    
    uint8_t dac = cmd->N_DAC;
    if (map_logical_channel(dac, &route) < 0) return 0; // Guard against out-of-bounds channels

    // Clear out any lingering Toggle/Dither mode flags on this channel
    uint16_t dac_bit = (uint16_t)(1u << LTC268X_CHANNEL_SEL(route.local_ch, route.dev_id));
    dither_toggle_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                     dither_toggle_state[route.state_idx], route.cs);

    uint8_t reg_setting = LTC268X_CMD_CH_SETTING(route.local_ch, route.dev_id);
    ltc_write_dac_cs(reg_setting,
                     LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V),
                     route.cs);
    dac_ranges[dac] = LTC268X_VOLTAGE_RANGE_M10V_10V;

    // Bounds check to avoid memory overflows in the sequencer buffer
    uint16_t actual_count = cmd->COUNT;
    if (actual_count > SEQ_MAX_POINTS) {
        actual_count = SEQ_MAX_POINTS;
    }

    // If this DAC already has an active soft-sequence, queue one next segment
    // instead of overwriting the currently running one.
    if (seqs[dac].active && seqs[dac].mode == 1u && seqs[dac].target_dac == dac) {
        uint16_t append_start = seqs[dac].pending_valid ? seqs[dac].pending_limit : seqs[dac].limit;
        uint16_t room = (append_start <= SEQ_MAX_POINTS) ? (SEQ_MAX_POINTS - append_start) : 0u;
        if (actual_count > room) {
            actual_count = room;
        }

        for (uint16_t i = 0; i < actual_count; i++) {
            seqs[dac].buffer[append_start + i] = cmd->VOLTS[i];
        }

        seqs[dac].pending_limit = append_start + actual_count;
        seqs[dac].pending_delay_ticks = (uint32_t)cmd->DELAY_MS * 10u;
        seqs[dac].pending_valid = (actual_count > 0u) ? 1u : 0u;

        return 6 + (((uint16_t)cmd->COUNT) * 2);
    }

    // Start a fresh soft-sequence for this DAC channel.
    seqs[dac].target_dac    = dac;
    seqs[dac].idx           = 0;
    seqs[dac].limit         = actual_count;
    seqs[dac].pending_limit = 0;
    seqs[dac].pending_delay_ticks = 0;
    seqs[dac].pending_valid = 0;
    seqs[dac].delay_ticks   = (uint32_t)cmd->DELAY_MS * 10u;
    seqs[dac].timer_counter = 0;
    seqs[dac].mode          = 1;  // Software-timed sequence mode
    seq_irq_every_points[dac] = 0u;
    seq_chain_target[dac] = 0xFFu;
    seq_chain_mask[dac] = 0u;
    seq_internal_irq_pending[dac] = 0u;

    // Deep copy the values from the dynamic packet into the sequencer's static buffer.
    // This isolates the data and prevents Channel 6 from overwriting Channel 0.
    for (uint16_t i = 0; i < actual_count; i++) {
        seqs[dac].buffer[i] = cmd->VOLTS[i];
    }

    // Mark the sequencer slot active AFTER the data copy operation completes
    seqs[dac].active = 1;

    // Return total bytes processed: Header fields (6 bytes) + Payload data array
    return 6 + (((uint16_t)cmd->COUNT) * 2);
}

int set_soft_sequence_chained(char *args)
{
    CHSOFTSEQcommand_t *cmd = (CHSOFTSEQcommand_t *)args;
    dac_route_t route;
    uint8_t dac = cmd->N_DAC;

    if (map_logical_channel(dac, &route) < 0) {
        return 0;
    }

    uint16_t actual_count = cmd->COUNT;
    if (actual_count > SEQ_MAX_POINTS) {
        actual_count = SEQ_MAX_POINTS;
    }

    uint16_t irq_every = cmd->IRQ_EVERY_POINTS;
    if (irq_every > actual_count) {
        irq_every = actual_count;
    }

    uint8_t chain_to = cmd->CHAIN_TO_DAC;
    if (chain_to >= TOTAL_LOGICAL_CHANNELS) {
        chain_to = 0xFFu;
    }

    uint8_t start_on_internal_irq = (cmd->START_ON_INTERNAL_IRQ != 0u) ? 1u : 0u;

    uint16_t dac_bit = (uint16_t)(1u << LTC268X_CHANNEL_SEL(route.local_ch, route.dev_id));
    dither_toggle_state[route.state_idx] &= ~dac_bit;
    ltc_write_dac_cs(LTC268X_CMD_TOGGLE_DITHER_EN_REG,
                     dither_toggle_state[route.state_idx], route.cs);

    uint8_t reg_setting = LTC268X_CMD_CH_SETTING(route.local_ch, route.dev_id);
    ltc_write_dac_cs(reg_setting,
                     LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V),
                     route.cs);
    dac_ranges[dac] = LTC268X_VOLTAGE_RANGE_M10V_10V;

    seqs[dac].target_dac = dac;
    seqs[dac].idx = 0u;
    seqs[dac].limit = actual_count;
    seqs[dac].pending_limit = 0u;
    seqs[dac].pending_delay_ticks = 0u;
    seqs[dac].pending_valid = 0u;
    seqs[dac].delay_ticks = (uint32_t)cmd->DELAY_MS * 10u;
    seqs[dac].timer_counter = 0u;
    seqs[dac].mode = start_on_internal_irq ? 3u : 1u;

    for (uint16_t i = 0u; i < actual_count; i++) {
        seqs[dac].buffer[i] = cmd->VOLTS[i];
    }

    seq_irq_every_points[dac] = irq_every;
    seq_chain_target[dac] = chain_to;
    seq_chain_mask[dac] = 0u;
    memset(seq_chain_irq_every[dac], 0, sizeof(seq_chain_irq_every[dac]));
    if (chain_to < TOTAL_LOGICAL_CHANNELS) {
        seq_chain_mask[dac] = (1u << chain_to);
    }
    seq_internal_irq_pending[dac] = 0u;
    seqs[dac].active = (actual_count > 0u) ? 1u : 0u;

    return 10 + (((uint16_t)cmd->COUNT) * 2);
}

int set_chain_followers(char *args)
{
    CHAINMAPcommand_t *cmd = (CHAINMAPcommand_t *)args;
    uint8_t src = cmd->SRC_DAC;
    uint8_t follower_count = cmd->FOLLOWER_COUNT;
    uint8_t first_valid = 0xFFu;
    uint32_t mask = 0u;

    if (src >= TOTAL_LOGICAL_CHANNELS) {
        return (int)(CHAINMAP_MIN_CMD_LEN + follower_count);
    }

    for (uint8_t i = 0u; i < follower_count; i++) {
        uint8_t follower = cmd->FOLLOWERS[i];
        if (follower >= TOTAL_LOGICAL_CHANNELS || follower == src) {
            continue;
        }

        mask |= (1u << follower);
        if (first_valid == 0xFFu) {
            first_valid = follower;
        }
    }

    seq_chain_mask[src] = mask;
    seq_chain_target[src] = first_valid;
    memset(seq_chain_irq_every[src], 0, sizeof(seq_chain_irq_every[src]));

    return (int)(CHAINMAP_MIN_CMD_LEN + follower_count);
}

int set_chain_followers_with_irq(char *args)
{
    CHAINMAPIRQcommand_t *cmd = (CHAINMAPIRQcommand_t *)args;
    uint8_t src = cmd->SRC_DAC;
    uint8_t follower_count = cmd->FOLLOWER_COUNT;
    uint8_t first_valid = 0xFFu;
    uint32_t mask = 0u;
    uint8_t *p = cmd->DATA;

    if (src >= TOTAL_LOGICAL_CHANNELS) {
        return (int)(CHAINMAP_IRQ_MIN_CMD_LEN + (follower_count * 3u));
    }

    memset(seq_chain_irq_every[src], 0, sizeof(seq_chain_irq_every[src]));

    for (uint8_t i = 0u; i < follower_count; i++) {
        uint8_t follower = p[0];
        uint16_t irq_every = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
        p += 3u;

        if (follower >= TOTAL_LOGICAL_CHANNELS || follower == src) {
            continue;
        }
        if (irq_every == 0u) {
            continue;
        }

        seq_chain_irq_every[src][follower] = irq_every;
        mask |= (1u << follower);
        if (first_valid == 0xFFu) {
            first_valid = follower;
        }
    }

    seq_chain_mask[src] = mask;
    seq_chain_target[src] = first_valid;

    return (int)(CHAINMAP_IRQ_MIN_CMD_LEN + (follower_count * 3u));
}

// Hardware Triggered sequences (EXTI Mapping Interface)
int set_sequence(char *args) {
    SEQcommand_t *CMD = (SEQcommand_t *)args;
    char buffer[20];
    uint8_t pin_req = CMD->T_PIN;
    uint8_t pin = pin_req;

    // Dedicated interrupt trigger path: only EXTI3 (PC3).
    // Accept logical trigger 1 or raw EXTI line 3.
    if (pin_req == 1u) {
        pin = 3u;
    }

    sprintf(buffer, "Pin: %d\r\n", pin);
    uart1_print(buffer);
    uint8_t dac = CMD->N_DAC;
    sprintf(buffer, "DAC: %d\r\n", dac);
    uart1_print(buffer);
    uint8_t edge = CMD->EDGE;
    uint16_t count = (uint16_t)CMD->COUNT;
    uint16_t copy_count = 0u;
    if (pin != 3u) return 6 + (count * 2);
    if (dac >= TOTAL_LOGICAL_CHANNELS) return 6 + (count * 2);
    uint8_t edge_sel = (edge != 0u) ? 1u : 0u;

    uint8_t *data_ptr = (uint8_t *)&args[6];
    copy_count = (count > SEQ_MAX_POINTS) ? SEQ_MAX_POINTS : count;

    // One slot per DAC channel for both trigger pins.
    seqs[dac].target_dac = dac;
    seqs[dac].limit = copy_count;
    seqs[dac].idx = 0;
    seqs[dac].active = 1;
    seqs[dac].mode = 2u;
    seq_slot_edge[dac] = edge_sel;

    for (uint16_t i = 0; i < copy_count; i++) {
        seqs[dac].buffer[i] = (uint16_t)data_ptr[0] | ((uint16_t)data_ptr[1] << 8);
        data_ptr += 2;
    }

    unregister_dac_from_trigger_slots(dac);
    seq_trigger_pin[dac] = pin;

    if (!register_dac_for_trigger(pin, dac)) {
        seqs[dac].active = 0;
        seq_trigger_pin[dac] = 0xFFu;
        return 6 + (count * 2);
    }

    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRL &= ~(0xFu << (3u * 4u));
    GPIOC->CRL |=  (0x4u << (3u * 4u));

    {
        uint32_t shift = (3u % 4u) * 4u;
        AFIO->EXTICR[0] &= ~(0xFu << shift);
        AFIO->EXTICR[0] |=  (0x2u << shift); // EXTI3 <- PC3
    }

    EXTI->IMR |= (1u << pin);
    // Enable BOTH edges at the line level; per-channel edge is selected in software.
    EXTI->RTSR |= (1u << pin);
    EXTI->FTSR |= (1u << pin);

    // Prime software edge detector with current pin level so first real edge is captured.
    if (pin == 3u) {
        seq_prev_level[1] = (GPIOC->IDR & (1u << 3)) ? 1u : 0u;
    } else if (pin == 2u) {
        seq_prev_level[0] = (GPIOD->IDR & (1u << 2)) ? 1u : 0u;
    }

    EXTI->PR = (1u << pin);
    IRQn_Type target_irq = EXTI3_IRQn;
    NVIC_SetPriority(target_irq, 1);
    NVIC_EnableIRQ(target_irq);

    return 6 + (count * 2);
}

static void apply_sequence_step_for_slot(uint8_t slot)
{
    uint8_t dac = seqs[slot].target_dac;
    uint16_t code = seqs[slot].buffer[seqs[slot].idx++];
    dac_route_t route;

    if (map_logical_channel(dac, &route) == 0) {
        ltc_write_dac_cs(LTC268X_CMD_CH_CODE_UPDATE(route.local_ch, route.dev_id), code, route.cs);
        if (route.dev_id == LTC2686) {
            p_dac->dac_code[route.local_ch] = code;
        }
    }

    if (seqs[slot].idx >= seqs[slot].limit) {
        seqs[slot].active = 0;
    }
}

void parser_poll_trigger_edges(void)
{
    uint8_t level2 = (GPIOD->IDR & (1u << 2)) ? 1u : 0u;
    uint8_t level3 = (GPIOC->IDR & (1u << 3)) ? 1u : 0u;

    if (seq_prev_level[0] == 0xFFu) {
        seq_prev_level[0] = level2;
    }
    if (seq_prev_level[1] == 0xFFu) {
        seq_prev_level[1] = level3;
    }

    if (seq_prev_level[0] != level2) {
        uint8_t rising = (seq_prev_level[0] == 0u && level2 == 1u) ? 1u : 0u;
        for (uint8_t i = 0u; i < trig2_seq_count; i++) {
            uint8_t dac = trig2_seq_dacs[i];
            if (dac < TOTAL_LOGICAL_CHANNELS && seqs[dac].active && seqs[dac].mode == 2u) {
                uint8_t want_rising = (seq_slot_edge[dac] != 0u) ? 1u : 0u;
                if ((want_rising && rising) || (!want_rising && !rising)) {
                    apply_sequence_step_for_slot(dac);
                }
            }
        }
        compact_trigger_slots(2u);
    }

    if (seq_prev_level[1] != level3) {
        uint8_t rising = (seq_prev_level[1] == 0u && level3 == 1u) ? 1u : 0u;
        for (uint8_t i = 0u; i < trig3_seq_count; i++) {
            uint8_t dac = trig3_seq_dacs[i];
            if (dac < TOTAL_LOGICAL_CHANNELS && seqs[dac].active && seqs[dac].mode == 2u) {
                uint8_t want_rising = (seq_slot_edge[dac] != 0u) ? 1u : 0u;
                if ((want_rising && rising) || (!want_rising && !rising)) {
                    apply_sequence_step_for_slot(dac);
                }
            }
        }
        compact_trigger_slots(3u);
    }

    seq_prev_level[0] = level2;
    seq_prev_level[1] = level3;
}

// Internal Hardware EXTI Engine Execution Routines
void EXTI3_IRQHandler(void) {
    if (EXTI->PR & EXTI_PR_PR3) {
        parser_poll_trigger_edges();
        EXTI->PR = EXTI_PR_PR3;
    }
}

void parser_handle_exti2_trigger(void)
{
    // EXTI2 is shared with W5500 packet interrupt; sequence stepping is EXTI3-only.
}