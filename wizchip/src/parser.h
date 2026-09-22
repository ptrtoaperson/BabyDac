#ifndef  PARSER_H
#define PARSER_H

#include "stm32f1xx.h"
#include <stdint.h>
#include "ltc268x.h"
#include "togglepins.h"
#include "spi.h"
#include "uart.h"

#define TOTAL_LOGICAL_CHANNELS 24u
#define SEQ_MAX_POINTS 1024u
#define TRIGDAC_CMD_LEN 6u
#define WRITE_IP_CMD_LEN 7u
#define STOPSEQ_CMD_LEN 3u
#define CHAINMAP_MIN_CMD_LEN 3u
#define CHAINMAP_IRQ_MIN_CMD_LEN 3u


typedef struct {
    uint16_t buffer[SEQ_MAX_POINTS]; 
    uint16_t idx;
    uint16_t limit;
    uint16_t pending_limit;   // Absolute end index for queued next segment
    uint32_t delay_ticks;   // New: How many 100us ticks to wait
    uint32_t pending_delay_ticks;
    uint32_t timer_counter; // New: Current countdown
    uint8_t  target_dac;
    uint8_t  active;
    uint8_t  pending_valid;
    uint8_t  mode;          // 1=software delay, 2=external trigger, 3=internal IRQ trigger
} Sequencer_t;
extern Sequencer_t seqs[TOTAL_LOGICAL_CHANNELS];
extern volatile uint16_t seq_internal_irq_pending[TOTAL_LOGICAL_CHANNELS];
extern uint16_t seq_irq_every_points[TOTAL_LOGICAL_CHANNELS];
extern uint8_t seq_chain_target[TOTAL_LOGICAL_CHANNELS];
extern uint32_t seq_chain_mask[TOTAL_LOGICAL_CHANNELS];
extern uint16_t seq_chain_irq_every[TOTAL_LOGICAL_CHANNELS][TOTAL_LOGICAL_CHANNELS];

/* Use packed struct to match on-disk layout */
#if defined(__GNUC__)
typedef struct __attribute__((packed))
{
    char INST;

}RESETcommand_t;


typedef struct __attribute__((packed))
{
    char INST;
    uint8_t N_DAC;
    uint16_t CODE;
   } Vcommand_t;

typedef struct __attribute__((packed))
{
    char INST;
    uint8_t N_DAC;
    uint16_t SPAN; 
    
} SPANcommand_t;

typedef struct __attribute__((packed))
{
    char INST;
    uint8_t N_DAC;
    uint8_t N_TGP;
    uint8_t DUTY;
    uint16_t LOW;
    uint16_t HIGH;
    float FREQ;

} STARTPWMcommand_t;

typedef struct __attribute__((packed))
{
    char INST;
    uint8_t N_DAC;
    uint8_t N_TGP;
 

} STOPPWMcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t N_DAC;
    uint8_t T_PIN;
    uint8_t EDGE;
    uint16_t COUNT;
    uint16_t VOLTS[]; // Flexible array member
} SEQcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t N_DAC;
    uint16_t DELAY_MS; // Delay between points in milliseconds
    uint16_t COUNT;
    uint16_t VOLTS[]; 
} SOFTSEQcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t N_DAC;
    uint16_t DELAY_MS;
    uint16_t COUNT;
    uint8_t CHAIN_TO_DAC;
    uint8_t START_ON_INTERNAL_IRQ;
    uint16_t IRQ_EVERY_POINTS;
    uint16_t VOLTS[];
} CHSOFTSEQcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t SRC_DAC;
    uint8_t FOLLOWER_COUNT;
    uint8_t FOLLOWERS[];
} CHAINMAPcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t SRC_DAC;
    uint8_t FOLLOWER_COUNT;
    uint8_t DATA[]; // Repeated entries: FOLLOWER(uint8), IRQ_EVERY_POINTS(uint16 LE)
} CHAINMAPIRQcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t N_TRIG;
    float V_TRIG;
} TRIGDACcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t IPaddr[4];
    uint16_t PORT;
} WriteIPcommand_t;

typedef struct __attribute__((packed)) {
    uint8_t INST;
    uint8_t N_DAC;
    uint8_t FLAGS; // bit0: zero output after stop
} STOPSEQcommand_t;

#else
typedef struct
{
    char INST;
    uint8_t N_DAC;
    float VOLT;
    uint16_t DELAY;
} command_t;
#endif

uint8_t parse_command(uint8_t *dptr, uint16_t len);
int reset(char* args);
int set_span(char *args);
int set_voltage(char* args);
int start_pwm_hw(char* args);
int stop_pwm_hw(char *args);
int set_soft_sequence(char *args);
int set_soft_sequence_chained(char *args);
int set_chain_followers(char *args);
int set_chain_followers_with_irq(char *args);
int set_trigger_level(char *args);
int write_ip_config(char *args);
int clear_state(char *args);
int stop_sequence_channel(char *args);

int stop_pwm_hw(char* args);

void Sequencer_Init(void);
int set_sequence(char *args);
void parser_handle_exti2_trigger(void);
void parser_poll_trigger_edges(void);
uint16_t trig_voltage_to_code(float v_trig);

#endif