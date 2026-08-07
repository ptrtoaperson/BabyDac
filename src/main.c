#include "stm32f1xx.h"
#include "spi.h"
#include "lantask.h"
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include <stdio.h>
#include <stdbool.h>
#include "ltc268x.h"
#include "triggers.h"
#include "pin_intr.h"
#include "togglepins.h"
#include "HSE_CLK.h"
#include "parser.h"
#include "uart.h"
#include "I2C.h"
#include "fact_reset.h"
#include "net_config.h"

volatile bool thereISpkt = false;

static volatile bool uart_cmd_ready = false;
static volatile uint16_t uart_rx_len = 0;
static volatile uint16_t uart_complete_len = 0;
static volatile uint8_t uart_rx_buf[DATA_BUF_SIZE];
static uint8_t uart_cmd_buf[DATA_BUF_SIZE];

static uint16_t g_boot_rst_streak = 0u;
static uint16_t g_boot_rst_total = 0u;
static uint32_t g_boot_rst_elapsed_sec = 0u;
static uint32_t g_boot_rst_window_sec = 0u;
static uint8_t g_boot_rst_pin = 0u;

static uint16_t uart_expected_cmd_len(const volatile uint8_t *buf, uint16_t len)
{
    uint8_t inst;

    if (!buf || len == 0u) {
        return 0u;
    }

    inst = buf[0];
    if (inst > 13u) {
        return 0u;
    }

    if (inst == 4u || inst == 5u) {
        uint16_t count;
        if (len < 6u) {
            return 0u;
        }
        count = (uint16_t)buf[4u] | ((uint16_t)buf[5u] << 8);
        return (uint16_t)(6u + (count * 2u));
    }

    if (inst == 10u) {
        uint16_t count;
        if (len < 10u) {
            return 0u;
        }
        count = (uint16_t)buf[4u] | ((uint16_t)buf[5u] << 8);
        return (uint16_t)(10u + (count * 2u));
    }

    if (inst == 12u) {
        uint16_t follower_count;
        if (len < CHAINMAP_MIN_CMD_LEN) {
            return 0u;
        }
        follower_count = (uint16_t)buf[2u];
        return (uint16_t)(CHAINMAP_MIN_CMD_LEN + follower_count);
    }

    if (inst == 13u) {
        uint16_t follower_count;
        if (len < CHAINMAP_IRQ_MIN_CMD_LEN) {
            return 0u;
        }
        follower_count = (uint16_t)buf[2u];
        return (uint16_t)(CHAINMAP_IRQ_MIN_CMD_LEN + (follower_count * 3u));
    }

    if (inst == 6u) {
        return 4u;
    }
    if (inst == 7u) {
        return TRIGDAC_CMD_LEN;
    }
    if (inst == 8u) {
        return WRITE_IP_CMD_LEN;
    }
    if (inst == 9u) {
        return 1u;
    }

    // IDs 0..3 fixed lengths.
    {
        static const uint16_t fixed_len[4] = {4u, 4u, 12u, 3u};
        return fixed_len[inst];
    }
}

static void uart_print_network_identity(const wiz_NetInfo *netinfo, uint16_t port)
{
    char msg[96];

    if (!netinfo) {
        return;
    }

    snprintf(msg,
             sizeof(msg),
             "Using IP: %u.%u.%u.%u, MAC: %02X:%02X:%02X:%02X:%02X:%02X, Port: %u\r\n",
             netinfo->ip[0],
             netinfo->ip[1],
             netinfo->ip[2],
             netinfo->ip[3],
             netinfo->mac[0],
             netinfo->mac[1],
             netinfo->mac[2],
             netinfo->mac[3],
             netinfo->mac[4],
             netinfo->mac[5],
             port);
    uart1_print(msg);
}

static void uart_rx_command_accumulator(char c)
{
    uint8_t byte = (uint8_t)c;
    uint16_t expected_len = 0u;
    uint16_t remaining = 0u;

    /* If one full UART frame is waiting, drop incoming bytes until main loop consumes it. */
    if (uart_cmd_ready) {
        return;
    }

    if (uart_rx_len >= (DATA_BUF_SIZE - 1u)) {
        if (uart_complete_len > 0u) {
            uart_cmd_ready = true;
            return;
        }

        // Drop malformed/oversized stream that has no complete command boundary.
        uart_rx_len = 0;
        uart_complete_len = 0;
        return;
    }

    uart_rx_buf[uart_rx_len++] = byte;

    // Track how many bytes form complete command records from the beginning of buffer.
    while (uart_complete_len < uart_rx_len) {
        remaining = (uint16_t)(uart_rx_len - uart_complete_len);
        expected_len = uart_expected_cmd_len(&uart_rx_buf[uart_complete_len], remaining);

        if (expected_len == 0u || remaining < expected_len) {
            break;
        }

        uart_complete_len = (uint16_t)(uart_complete_len + expected_len);
    }

    // Finalize only when delimiter arrives exactly after complete command boundaries.
    if (byte == 'e' && uart_complete_len == (uint16_t)(uart_rx_len - 1u)) {
        uart_cmd_ready = true;
    }
}

void SystemInit(void) {
    // Count reset gesture as early as possible so quick presses are not missed.
    g_boot_rst_streak = Increment_Reset_Counter(0);
    g_boot_rst_total = Get_Total_Reset_Presses();
    g_boot_rst_elapsed_sec = Get_Reset_Last_Delta_Sec();
    g_boot_rst_window_sec = Get_Reset_Window_Sec();
    g_boot_rst_pin = Was_Last_Reset_Pin();

    // Force critical startup control pin high as early as possible.
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->BSRR = GPIO_BSRR_BS0;
    GPIOC->CRL &= ~(GPIO_CRL_CNF0 | GPIO_CRL_MODE0);
    GPIOC->CRL |= GPIO_CRL_MODE0_0; // PC0 output push-pull, 10MHz

    // Earliest possible DAC safety state on reset: initialize SPI2 + CS and
    // preload both DACs to +/-10V span with 0V code.
    ltc_spi_init();

    // Preload startup span while outputs are still powered down.
    for (uint8_t ch = 0; ch < 8; ch++) {
        uint8_t reg_setting = LTC268X_CMD_CH_SETTING(ch, LTC2686);
        uint8_t reg_update = LTC268X_CMD_CH_CODE_UPDATE(ch, LTC2686);
        ltc_write_dac_cs(reg_setting,
                         LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V),
                         LTC_DAC_CS0);
        ltc_write_dac_cs(reg_update, 0x8000, LTC_DAC_CS0);
    }

    for (uint8_t ch = 0; ch < 16; ch++) {
        uint8_t reg_setting = LTC268X_CMD_CH_SETTING(ch, LTC2688);
        uint8_t reg_update = LTC268X_CMD_CH_CODE_UPDATE(ch, LTC2688);
        ltc_write_dac_cs(reg_setting,
                         LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V),
                         LTC_DAC_CS1);
        ltc_write_dac_cs(reg_update, 0x8000, LTC_DAC_CS1);
    }

    // Keep both DACs powered so external stages do not fall to rail in high-Z.
    ltc_write_dac_cs(LTC268X_CMD_POWERDOWN_REG, 0x0000, LTC_DAC_CS0);
    ltc_write_dac_cs(LTC268X_CMD_POWERDOWN_REG, 0x0000, LTC_DAC_CS1);
}

int main(void)
{
    X_Clock_Init();

    // Bring DAC SPI/CS up immediately.
    ltc_spi_init();

    uart1_init();
    uart1_set_rx_callback(uart_rx_command_accumulator);

    {
        uint16_t rstCnt = g_boot_rst_streak;
        uint16_t totalPresses = g_boot_rst_total;
        uint32_t elapsedSec = g_boot_rst_elapsed_sec;
        uint32_t windowSec = g_boot_rst_window_sec;
        uint8_t isPinReset = g_boot_rst_pin;
        char rst_msg[128];

        snprintf(rst_msg,
                 sizeof(rst_msg),
                 "Reset streak: %u, Total reset presses: %u, elapsed=%lu/%lus, pin=%u\r\n",
                 (unsigned int)rstCnt,
                 (unsigned int)totalPresses,
                 (unsigned long)elapsedSec,
                 (unsigned long)windowSec,
                 (unsigned int)isPinReset);
        uart1_print(rst_msg);

        if (rstCnt >= 5u) {
            netcfg_clear();
            lan_set_tcp_port(TCP_PORT);
            Increment_Reset_Counter(1);
            uart1_print("Factory reset threshold reached. Net config cleared.\r\n");
        }
    }

    uart1_print("Clock test\r\n");



    trig_dac_init();
    ExternIntInit();
    ethernet_spi_init();
    LTC2686_TGP_Init();

    __enable_irq(); // Global interrupt enable

    hardware_reset();

    w5500_port_init();
    Sequencer_Init();




    
    // Set STM32 trigger DAC outputs to midscale (~1.65V), not full-scale 3.3V.
    trig_set_level_A(trig_voltage_to_code(3.0f));
    trig_set_level_B(trig_voltage_to_code(3.0f));

    uart1_print("SWD test\r\n");

    // ========================================================================
    // 1. ADI LTC268X OFFICIAL DRIVER INITIALIZATION
    // ========================================================================
    struct ltc268x_init_param dac_init_values;
    dac_init_values.dev_id = LTC2686;           // Target your 8-channel footprint
    // Keep DAC0 channels powered so there is no startup power-down rail transient.
    dac_init_values.pwd_dac_setting = 0x0000;
    dac_init_values.dither_toggle_en = 0x0000;  // Disable default dither updates

    // Configure all active channels to start natively in ±10V Mode
    // Struct requires size 16 arrays even for the 8-channel LTC2686 variant
    for(int i = 0; i < 16; i++) {
        dac_init_values.crt_range[i]     = LTC268X_VOLTAGE_RANGE_M10V_10V;
        dac_init_values.dither_mode[i]   = false;
        dac_init_values.dither_phase[i]  = LTC268X_DITH_PHASE_0;  
        dac_init_values.dither_period[i] = LTC268X_DITH_PERIOD_4; 
        dac_init_values.clk_input[i]     = LTC268X_SOFT_TGL; // MATCHED: Using Soft Toggle from header
        dac_init_values.reg_select[i]    = LTC268X_SELECT_A_REG;
    }

    // Access the shared device handle pointer instantiation from parser.c
    extern struct ltc268x_dev *p_dac; 
    int dac_status = ltc268x_init(&p_dac, dac_init_values);

    if (dac_status == 0) {
        uart1_print("LTC2686 Initialized via official driver API!\r\n");
    } else {
        uart1_print("LTC2686 Initialization Failed!\r\n");
    }

    // ========================================================================
    // 2. PUBLIC API ACTIONS (No hidden underscore helper functions)
    // ========================================================================
    if (dac_status == 0) {
        for(volatile int i=0; i<10000; i++);

        // Set every channel output explicitly to 0.0 Volts.
        for (uint8_t ch = 0; ch < 8; ch++) {
            ltc268x_set_voltage(p_dac, ch, 0.0f);
        }

        // Initialize second DAC (LTC2688 on PC9 CS) to +/-10V span and 0V code.
        for (uint8_t ch = 0; ch < 16; ch++) {
            uint8_t reg_setting = LTC268X_CMD_CH_SETTING(ch, LTC2688);
            uint8_t reg_update = LTC268X_CMD_CH_CODE_UPDATE(ch, LTC2688);
            ltc_write_dac_cs(reg_setting, LTC268X_CH_SPAN(LTC268X_VOLTAGE_RANGE_M10V_10V), LTC_DAC_CS1);
            ltc_write_dac_cs(reg_update, 0x8000, LTC_DAC_CS1);
        }
        
        //ltc268x_set_voltage(p_dac, 0, 2.5f);   // Output a steady 2.5V 
        //for(volatile int i=0; i<10000; i++);

        // Change Span on channel 0 to Unipolar 0V - 10V
       // ltc268x_set_span(p_dac, 0, LTC268X_VOLTAGE_RANGE_0V_10V);
        //for(volatile int i=0; i<10000; i++);
        
        uart1_print("DAC Channel 0 Configured and active.\r\n");
    }

    // ========================================================================
    // 3. PERIPHERAL GPIO & W5500 ETHERNET SERVER SETUP
    // ========================================================================
    // Enable the clock for GPIOC
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    // Configure PC13 as General Purpose Output Push-Pull (2MHz)
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13); 
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    // Memory Allocation: 2KB for all 8 sockets
    uint8_t memsize[2][8] = {{2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 2, 2, 2, 2, 2}};
    (void)memsize; // FIX: Silences unused variable warning cleanly

    // Enable specific interrupt types (RECV for data, CON for connection) grouped safely
    setSn_IMR(0, (Sn_IR_RECV | Sn_IR_CON));

    wiz_NetInfo netinfo = {
        .mac = {0x19, 0x21, 0x68, 0x00, 0x00, 0x29},
        .ip = {192, 168, 1, 50},
        .sn = {255, 255, 255, 0},
        .gw = {192, 168, 1, 1},
        .dns = {8, 8, 8, 8},
        .dhcp = NETINFO_STATIC};
    uint8_t cfg_ip[4] = {0u, 0u, 0u, 0u};
    uint16_t cfg_port = NETCFG_DEFAULT_PORT;

    i2c_init();
    if (eui48_read_mac(netinfo.mac)) {
        uint8_t eui_bus = 0u;
        uint8_t eui_addr = 0u;
        char mac_msg[64];
        snprintf(mac_msg,
                 sizeof(mac_msg),
                 "MAC from EEPROM: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 netinfo.mac[0],
                 netinfo.mac[1],
                 netinfo.mac[2],
                 netinfo.mac[3],
                 netinfo.mac[4],
                 netinfo.mac[5]);
        uart1_print(mac_msg);

        if (eui48_get_last_source(&eui_bus, &eui_addr)) {
            char src_msg[64];
            snprintf(src_msg,
                     sizeof(src_msg),
                     "EEPROM source: I2C%u addr 0x%02X, EUI48 @0xFA\r\n",
                     eui_bus,
                     eui_addr);
            uart1_print(src_msg);
        }
    } else {
        uint8_t ack_count = 0u;
        uint8_t first_addr = 0xFFu;
        uint8_t ack_mask = 0u;
        uint8_t raw_mac[6] = {0u, 0u, 0u, 0u, 0u, 0u};
        uint8_t raw_addr = 0xFFu;
        bool raw_valid = false;
        uint8_t diag_addr_ack = 0u;
        uint8_t diag_ptr_ok = 0u;
        uint8_t diag_read_start_ok = 0u;
        uint8_t diag_raw_reads = 0u;

        eui48_get_diag_counters(&diag_addr_ack,
                    &diag_ptr_ok,
                    &diag_read_start_ok,
                    &diag_raw_reads);

        if (eui48_get_probe_status(&ack_count, &first_addr, &ack_mask)) {
            char probe_msg[96];
            snprintf(probe_msg,
                     sizeof(probe_msg),
                     "EEPROM probe: ACK on I2C2 (count=%u, first=0x%02X, mask=0x%02X)\r\n",
                     ack_count,
                     first_addr,
                     ack_mask);
            uart1_print(probe_msg);

            {
                char diag_msg[96];
                snprintf(diag_msg,
                         sizeof(diag_msg),
                         "EEPROM diag: addr_ack=%u ptr_ok=%u rd_start_ok=%u raw_reads=%u\r\n",
                         diag_addr_ack,
                         diag_ptr_ok,
                         diag_read_start_ok,
                         diag_raw_reads);
                uart1_print(diag_msg);
            }

            if (eui48_get_last_raw_read(raw_mac, &raw_addr, &raw_valid)) {
                char raw_msg[128];
                snprintf(raw_msg,
                         sizeof(raw_msg),
                         "EEPROM raw @0x%02X [0xFA..0xFF]: %02X:%02X:%02X:%02X:%02X:%02X (valid=%u)\r\n",
                         raw_addr,
                         raw_mac[0],
                         raw_mac[1],
                         raw_mac[2],
                         raw_mac[3],
                         raw_mac[4],
                         raw_mac[5],
                         raw_valid ? 1u : 0u);
                uart1_print(raw_msg);
            }
        } else {
            uart1_print("EEPROM probe: no ACK on I2C2 addresses 0x50-0x57\r\n");
        }

        uart1_print("EEPROM MAC read failed, using fallback MAC.\r\n");
    }

    if (netcfg_load(cfg_ip, &cfg_port)) {
        netinfo.ip[0] = cfg_ip[0];
        netinfo.ip[1] = cfg_ip[1];
        netinfo.ip[2] = cfg_ip[2];
        netinfo.ip[3] = cfg_ip[3];
        lan_set_tcp_port(cfg_port);
        uart1_print("Using saved network config from flash.\r\n");
    } else {
        lan_set_tcp_port(TCP_PORT);
        uart1_print("Saved network config invalid/missing. Using defaults.\r\n");
    }

    wizchip_setnetinfo(&netinfo);
    uart_print_network_identity(&netinfo, lan_get_tcp_port());

    socket(0, Sn_MR_TCP, lan_get_tcp_port(), 0);
    listen(0);

    setSIMR(0x01);

    // Initial Hardware Check
    uint8_t ver = getVERSIONR();
    if (ver != 0x04)
    {
        uart1_print("FATAL ERROR: W5500 not found via SPI!\r\n");
        while (1) {
            // Rapidly flash the LED if there is a hardware error
            GPIOC->BRR = (1 << 13);  // LED ON
            for (volatile int i = 0; i < 300000; i++);
            GPIOC->BSRR = (1 << 13); // LED OFF
            for (volatile int i = 0; i < 300000; i++);
        }
    }
    uart1_print("W5500 Ready. Starting Server...\r\n");

    // ========================================================================
    // 4. MAIN BACKGROUND SERVER LOOP
    // ========================================================================
    while (1) {
        if (uart_cmd_ready) {
            uint16_t frame_len = 0;

            __disable_irq();
            frame_len = uart_rx_len;
            if (frame_len > DATA_BUF_SIZE) {
                frame_len = DATA_BUF_SIZE;
            }
            for (uint16_t i = 0; i < frame_len; i++) {
                uart_cmd_buf[i] = uart_rx_buf[i];
            }
            uart_rx_len = 0;
            uart_complete_len = 0;
            uart_cmd_ready = false;
            __enable_irq();

            if (frame_len > 0) {
                //uart1_print("UART packet processing...\r\n");
                uint16_t parse_len = frame_len;
                if (parse_len > 0 && uart_cmd_buf[parse_len - 1] == 'e') {
                    parse_len--;
                }
                parse_command(uart_cmd_buf, parse_len);
                uart1_print("OK");
            }
        }

        // Check the flag OR check if the pin is physically LOW (PD2)
        if (thereISpkt || !(GPIOD->IDR & (1 << 2))) {
            thereISpkt = true; // Force flag true if pin is held low by W5500
            //uart1_print("Packet processing...\r\n");
            
            run_tcp_server(&thereISpkt);
            
            // Clear the pending register for line 2 
            // to catch any edge that happened during processing
            EXTI->PR = EXTI_PR_PR2; 
        }
    }
}