#include "stm32f1xx.h"
#include <stdbool.h>
#include <string.h>

#define EUI_EEPROM_ADDR_MIN_7BIT 0x50u
#define EUI_EEPROM_ADDR_MAX_7BIT 0x57u
#define EUI48_START_ADDR      0xFAu
#define I2C_TIMEOUT_CYCLES    100000u
#define EUI48_READ_RETRIES    3u

static I2C_TypeDef *i2c_bus = I2C2;
static uint8_t eui_last_bus_index = 0xFFu;
static uint8_t eui_last_addr_7bit = 0xFFu;
static uint8_t eui_probe_ack_count = 0u;
static uint8_t eui_probe_first_addr = 0xFFu;
static uint8_t eui_probe_ack_mask = 0u;
static uint8_t eui_diag_addr_ack = 0u;
static uint8_t eui_diag_ptr_ok = 0u;
static uint8_t eui_diag_read_start_ok = 0u;
static uint8_t eui_diag_raw_reads = 0u;
static uint8_t eui_last_raw_mac[6] = {0u, 0u, 0u, 0u, 0u, 0u};
static uint8_t eui_last_raw_addr_7bit = 0xFFu;
static bool eui_last_raw_valid = false;
static bool eui_last_raw_available = false;

static void i2c_select_bus(I2C_TypeDef *bus)
{
    if (bus != 0) {
        i2c_bus = bus;
    }
}

static void i2c_configure_gpio_for_bus2(void)
{
    // PB10/PB11 (I2C2): AF open-drain, 50MHz.
    GPIOB->CRH &= ~(GPIO_CRH_MODE10 | GPIO_CRH_CNF10 | GPIO_CRH_MODE11 | GPIO_CRH_CNF11);
    GPIOB->CRH |= (GPIO_CRH_MODE10 | GPIO_CRH_CNF10 | GPIO_CRH_MODE11 | GPIO_CRH_CNF11);

    GPIOB->BSRR = GPIO_BSRR_BS10 | GPIO_BSRR_BS11;
}

static void i2c_init_bus(I2C_TypeDef *bus)
{
    if (!bus) {
        return;
    }

    bus->CR1 |= I2C_CR1_SWRST;
    bus->CR1 &= ~I2C_CR1_SWRST;

    // Timing for APB1 = 36MHz, Standard mode 100kHz.
    bus->CR2 = 36u;
    bus->CCR = 180u;
    bus->TRISE = 37u;
    bus->CR1 |= I2C_CR1_PE;
    bus->CR1 |= I2C_CR1_ACK;
}

static void i2c_recover_bus(I2C_TypeDef *bus)
{
    if (!bus) {
        return;
    }

    // Force a STOP to release bus ownership, then reinitialize peripheral state.
    bus->CR1 |= I2C_CR1_STOP;
    for (volatile uint32_t i = 0u; i < 8000u; i++) {
        __asm("nop");
    }
    i2c_init_bus(bus);
}

static bool eui48_is_valid(const uint8_t mac[6])
{
    bool all_zero = true;
    bool all_ff = true;

    for (uint8_t i = 0u; i < 6u; i++) {
        if (mac[i] != 0x00u) {
            all_zero = false;
        }
        if (mac[i] != 0xFFu) {
            all_ff = false;
        }
    }

    if (all_zero || all_ff) {
        return false;
    }

    // IEEE 802: first-octet bit0 must be 0 for unicast address.
    if ((mac[0] & 0x01u) != 0u) {
        return false;
    }

    return true;
}

static uint8_t i2c_wait_flag_set(volatile uint32_t *reg, uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT_CYCLES;
    while (((*reg) & flag) == 0u) {
        if (timeout-- == 0u) {
            return 1u;
        }
    }
    return 0u;
}

void i2c_init(void) {
    // 1. Enable Clocks
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    // 2. Configure GPIO and initialize I2C2 controller.
    i2c_configure_gpio_for_bus2();
    i2c_init_bus(I2C2);
    i2c_select_bus(I2C2);
}

uint8_t i2c_start(uint8_t address) {
    // Wait until bus is not busy
    uint32_t timeout = I2C_TIMEOUT_CYCLES;
    while (i2c_bus->SR2 & I2C_SR2_BUSY) {
        if (timeout-- == 0u) {
            return 1u;
        }
    }

    i2c_bus->CR1 |= I2C_CR1_START;
    if (i2c_wait_flag_set(&i2c_bus->SR1, I2C_SR1_SB) != 0u) {
        return 1u;
    }

    i2c_bus->DR = address;
    // Wait for ADDR flag (Success) or AF flag (NACK)
    timeout = I2C_TIMEOUT_CYCLES;
    while ((i2c_bus->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)) == 0u) {
        if (timeout-- == 0u) {
            return 1u;
        }
    }

    if (i2c_bus->SR1 & I2C_SR1_AF) {
        i2c_bus->SR1 &= ~I2C_SR1_AF; // Clear NACK flag
        i2c_bus->CR1 |= I2C_CR1_STOP;
        return 1; // Failed
    }

    // Clear ADDR flag: Read SR1 (done above) then SR2
    (void)i2c_bus->SR2;
    return 0; // Success
}

uint8_t i2c_write(uint8_t data) {
    if (i2c_wait_flag_set(&i2c_bus->SR1, I2C_SR1_TXE) != 0u) {
        return 1u;
    }

    i2c_bus->DR = data;
    // Wait for BTF (Byte Transfer Finished) to ensure data is on the bus
    if (i2c_wait_flag_set(&i2c_bus->SR1, I2C_SR1_BTF) != 0u) {
        return 1u;
    }

    return 0;
}

uint8_t i2c_read_ack(void)
{
    i2c_bus->CR1 |= I2C_CR1_ACK;
    if (i2c_wait_flag_set(&i2c_bus->SR1, I2C_SR1_RXNE) != 0u) {
        return 0u;
    }

    return (uint8_t)i2c_bus->DR;
}

uint8_t i2c_read_nack(void) {
    // For the LAST byte: 
    // 1. Clear ACK bit
    // 2. Set STOP bit
    // 3. Then read data
    i2c_bus->CR1 &= ~I2C_CR1_ACK;
    i2c_bus->CR1 |= I2C_CR1_STOP;
    
    if (i2c_wait_flag_set(&i2c_bus->SR1, I2C_SR1_RXNE) != 0u) {
        return 0u;
    }

    return (uint8_t)i2c_bus->DR;
}

void i2c_stop(void) {
    i2c_bus->CR1 |= I2C_CR1_STOP;
    i2c_bus->CR1 |= I2C_CR1_ACK;
}

bool eui48_read_mac(uint8_t mac[6])
{
    static const I2C_TypeDef *probe_buses[] = {I2C2};
    static const uint8_t probe_bus_ids[] = {2u};

    if (mac == 0) {
        return false;
    }

    eui_last_bus_index = 0xFFu;
    eui_last_addr_7bit = 0xFFu;
    eui_probe_ack_count = 0u;
    eui_probe_first_addr = 0xFFu;
    eui_probe_ack_mask = 0u;
    eui_diag_addr_ack = 0u;
    eui_diag_ptr_ok = 0u;
    eui_diag_read_start_ok = 0u;
    eui_diag_raw_reads = 0u;
    eui_last_raw_addr_7bit = 0xFFu;
    eui_last_raw_valid = false;
    eui_last_raw_available = false;
    memset(eui_last_raw_mac, 0, sizeof(eui_last_raw_mac));

    for (uint8_t attempt = 0u; attempt < EUI48_READ_RETRIES; attempt++) {
        for (uint8_t b = 0u; b < (uint8_t)(sizeof(probe_buses) / sizeof(probe_buses[0])); b++) {
            i2c_select_bus((I2C_TypeDef *)probe_buses[b]);

            for (uint8_t dev7 = EUI_EEPROM_ADDR_MIN_7BIT; dev7 <= EUI_EEPROM_ADDR_MAX_7BIT; dev7++) {
                if (i2c_start((uint8_t)((dev7 << 1) | 0u)) != 0u) {
                    i2c_stop();
                    continue;
                }

                eui_probe_ack_count++;
                eui_diag_addr_ack++;
                if (eui_probe_first_addr == 0xFFu) {
                    eui_probe_first_addr = dev7;
                }
                eui_probe_ack_mask |= (uint8_t)(1u << (dev7 - EUI_EEPROM_ADDR_MIN_7BIT));

                if (i2c_write(EUI48_START_ADDR) != 0u) {
                    i2c_stop();
                    continue;
                }
                eui_diag_ptr_ok++;

                if (i2c_start((uint8_t)((dev7 << 1) | 1u)) != 0u) {
                    // Some parts/board conditions are more reliable with STOP then fresh START.
                    i2c_stop();
                    if (i2c_start((uint8_t)((dev7 << 1) | 1u)) != 0u) {
                        i2c_stop();
                        continue;
                    }
                }

                // Count successful entry into read phase regardless of restart style.
                eui_diag_read_start_ok++;

                for (uint8_t i = 0; i < 5u; i++) {
                    mac[i] = i2c_read_ack();
                }
                mac[5] = i2c_read_nack();

                memcpy(eui_last_raw_mac, mac, sizeof(eui_last_raw_mac));
                eui_last_raw_addr_7bit = dev7;
                eui_last_raw_valid = eui48_is_valid(mac);
                eui_last_raw_available = true;
                eui_diag_raw_reads++;

                i2c_stop();
                i2c_bus->CR1 |= I2C_CR1_ACK;
                if (eui_last_raw_valid) {
                    eui_last_bus_index = probe_bus_ids[b];
                    eui_last_addr_7bit = dev7;
                    return true;
                }
            }
        }

        // Retry after peripheral recovery when first pass hits startup/timing edge cases.
        i2c_recover_bus(I2C2);
    }

    i2c_select_bus(I2C2);
    i2c_bus->CR1 |= I2C_CR1_ACK;
    return false;
}

bool eui48_get_last_source(uint8_t *bus_index, uint8_t *addr_7bit)
{
    if ((eui_last_bus_index == 0xFFu) || (eui_last_addr_7bit == 0xFFu)) {
        return false;
    }

    if (bus_index) {
        *bus_index = eui_last_bus_index;
    }
    if (addr_7bit) {
        *addr_7bit = eui_last_addr_7bit;
    }
    return true;
}

bool eui48_get_probe_status(uint8_t *ack_count, uint8_t *first_addr_7bit, uint8_t *ack_mask)
{
    if (ack_count) {
        *ack_count = eui_probe_ack_count;
    }
    if (first_addr_7bit) {
        *first_addr_7bit = eui_probe_first_addr;
    }
    if (ack_mask) {
        *ack_mask = eui_probe_ack_mask;
    }

    return (eui_probe_ack_count > 0u);
}

bool eui48_get_last_raw_read(uint8_t mac[6], uint8_t *addr_7bit, bool *is_valid)
{
    if (!eui_last_raw_available) {
        return false;
    }

    if (mac) {
        memcpy(mac, eui_last_raw_mac, sizeof(eui_last_raw_mac));
    }
    if (addr_7bit) {
        *addr_7bit = eui_last_raw_addr_7bit;
    }
    if (is_valid) {
        *is_valid = eui_last_raw_valid;
    }

    return true;
}

void eui48_get_diag_counters(uint8_t *addr_ack, uint8_t *ptr_ok, uint8_t *read_start_ok, uint8_t *raw_reads)
{
    if (addr_ack) {
        *addr_ack = eui_diag_addr_ack;
    }
    if (ptr_ok) {
        *ptr_ok = eui_diag_ptr_ok;
    }
    if (read_start_ok) {
        *read_start_ok = eui_diag_read_start_ok;
    }
    if (raw_reads) {
        *raw_reads = eui_diag_raw_reads;
    }
}
