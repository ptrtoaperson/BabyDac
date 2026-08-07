#include "spi.h"

static inline void ltc_select_cs(ltc_dac_cs_t cs)
{
    if (cs == LTC_DAC_CS1) {
        GPIOC->BSRR = GPIO_BSRR_BR9;
    } else {
        GPIOB->BSRR = GPIO_BSRR_BR12;
    }
}

static inline void ltc_deselect_cs(ltc_dac_cs_t cs)
{
    if (cs == LTC_DAC_CS1) {
        GPIOC->BSRR = GPIO_BSRR_BS9;
    } else {
        GPIOB->BSRR = GPIO_BSRR_BS12;
    }
}

// ============================
// SPI INIT
// ============================

void ethernet_spi_init(void)
{
    // 1. Enable clocks
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_AFIOEN |
                    RCC_APB2ENR_SPI1EN;

    // 2. Disable JTAG only (Keeps SWD working so you can still debug/flash)
    // Clear the bits first, then set the JTAG-Disable/SWD-Enable option
    AFIO->MAPR &= ~AFIO_MAPR_SWJ_CFG;
    AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    // 3. Remap SPI1 to PB3, PB4, PB5
    AFIO->MAPR |= AFIO_MAPR_SPI1_REMAP;

    // ================= GPIO =================

    // PB3 → SCK (AF Push-Pull)
    GPIOB->CRL &= ~(GPIO_CRL_MODE3 | GPIO_CRL_CNF3);
    GPIOB->CRL |= GPIO_CRL_MODE3;  // Output 50MHz
    GPIOB->CRL |= GPIO_CRL_CNF3_1; // AF Push-Pull

    // PB5 → MOSI (AF Push-Pull)
    GPIOB->CRL &= ~(GPIO_CRL_MODE5 | GPIO_CRL_CNF5);
    GPIOB->CRL |= GPIO_CRL_MODE5;  // Output 50MHz
    GPIOB->CRL |= GPIO_CRL_CNF5_1; // AF Push-Pull

    // PB4 → MISO (Input Floating)
    GPIOB->CRL &= ~(GPIO_CRL_MODE4 | GPIO_CRL_CNF4);
    GPIOB->CRL |= GPIO_CRL_CNF4_0; // Floating Input

    // PA15 → CS (General Purpose Output Push-Pull)
    GPIOA->CRH &= ~(GPIO_CRH_MODE15 | GPIO_CRH_CNF15);
    GPIOA->CRH |= GPIO_CRH_MODE15; // Output 50MHz
    // CNF15 remains 00 for General Purpose Push-Pull

    GPIOA->BSRR = GPIO_BSRR_BS15; // CS HIGH (Idle)

    // ================= SPI =================

    SPI1->CR1 = 0; // Reset configuration

    // Set Baud Rate Divider
    // PCLK2 is 72MHz. 
    SPI_CR1_BR_2 | SPI_CR1_BR_0 ;//(Divider 64) = 1.125 MHz (Very safe for testing)
   //(Divider 16) = 4.5 MHz (Good for Ethernet)
  //
    //SPI1->CR1 |= ~( SPI_CR1_BR_1 |SPI_CR1_BR_2 | SPI_CR1_BR_0); 
    SPI1->CR1 &= ~SPI_CR1_BR; 

// 2. Set BR[2:0] to 001 (Divider 4)
//SPI1->CR1 |= SPI_CR1_BR_0;
   

    SPI1->CR1 |= SPI_CR1_MSTR;               // Master Mode
    SPI1->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;  // Software Slave Management
    SPI1->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA); // Mode 0 (Standard for Ethernet)

    // Interrupts
    SPI1->CR2 |= SPI_CR2_RXNEIE; 
    // NVIC_EnableIRQ(SPI1_IRQn); // Uncomment if you have an SPI1_IRQHandler

    SPI1->CR1 |= SPI_CR1_SPE; // Enable SPI
}

// ============================
// CS CONTROL
// ============================

void wizchip_select(void)
{
    GPIOA->BSRR = GPIO_BSRR_BR15; // CS LOW
}

void wizchip_deselect(void)
{
    GPIOA->BSRR = GPIO_BSRR_BS15; // CS HIGH
}

// ============================
// SPI CORE TRANSFER
// ============================

static inline uint8_t SPI1_Transfer(uint8_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE));
    *((__IO uint8_t*)&SPI1->DR) = data;

    while (!(SPI1->SR & SPI_SR_RXNE));
    return *((__IO uint8_t*)&SPI1->DR);
}

// ============================
// SINGLE BYTE FUNCTIONS
// ============================

void spi_write(uint8_t data)
{
    SPI1_Transfer(data);
}

uint8_t spi_read(void)
{
    return SPI1_Transfer(0xFF);
}

// ============================
// BURST FUNCTIONS
// ============================

void spi_write_burst(uint8_t* buf, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
    {
        SPI1_Transfer(buf[i]);
    }
}

void spi_read_burst(uint8_t* buf, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
    {
        buf[i] = SPI1_Transfer(0xFF);
    }
}


/*DAC SPI INterface*/
void ltc_spi_init(void)
{
    // 1. Enable clocks
    // SPI2 is on APB1, GPIOs and AFIO are on APB2
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    // ================= GPIO CONFIG (PB12-PB15) =================
    // Clear CRH bits for pins 12, 13, 14, 15
    GPIOB->CRH &= ~(GPIO_CRH_MODE12 | GPIO_CRH_CNF12 |
                    GPIO_CRH_MODE13 | GPIO_CRH_CNF13 |
                    GPIO_CRH_MODE14 | GPIO_CRH_CNF14 |
                    GPIO_CRH_MODE15 | GPIO_CRH_CNF15);

    // PB13: SCK (AF Push-Pull, 50MHz)
    GPIOB->CRH |= (GPIO_CRH_MODE13 | GPIO_CRH_CNF13_1);

    // PB15: MOSI (AF Push-Pull, 50MHz)
    GPIOB->CRH |= (GPIO_CRH_MODE15 | GPIO_CRH_CNF15_1);

    // PB14: MISO (Input Floating)
   // Replace your old PB14 configuration with this:
GPIOB->CRH &= ~(GPIO_CRH_MODE14 | GPIO_CRH_CNF14); // Clear PB14 bits
GPIOB->CRH |= (0x8 << (6 * 4));                    // Set Pin 14 as Input Pull-up/down (6th nibble in CRH)
GPIOB->ODR |= (1 << 14);                           // Select Pull-up resistor

    // PB12: CS (General Purpose Output Push-Pull, 50MHz)
    GPIOB->CRH |= GPIO_CRH_MODE12; 

    // PC9: CS for second DAC (General Purpose Output Push-Pull, 50MHz)
    GPIOC->CRH &= ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
    GPIOC->CRH |= GPIO_CRH_MODE9;
    
    // CS High (Idle)
    GPIOB->BSRR = GPIO_BSRR_BS12;
    GPIOC->BSRR = GPIO_BSRR_BS9;

    // ================= SPI2 CONFIG =================
    SPI2->CR1 = 0; // Clear reset state

    // Baud Rate: PCLK1 is 36MHz. 
    // Div 16 (SPI_CR1_BR_1 | SPI_CR1_BR_0) = 2.25 MHz
   // SPI2->CR1 |= (SPI_CR1_BR_1 | SPI_CR1_BR_0); 
    SPI2 -> CR1 &= ~( SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0);
    SPI2->CR1 |= SPI_CR1_MSTR;               // Master Mode
    SPI2->CR1 |= SPI_CR1_SSM | SPI_CR1_SSI;  // Software Slave Management
    SPI2->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA); // Mode 0

    // Interrupts (Only enable if you have a handler ready)
    // SPI2->CR2 |= SPI_CR2_RXNEIE; 
    // NVIC_EnableIRQ(SPI2_IRQn);

    SPI2->CR1 |= SPI_CR1_SPE; // Enable SPI
}
uint8_t spi2_transfer_byte(uint8_t data)
{
    while (!(SPI2->SR & SPI_SR_TXE));

    *((volatile uint8_t *)&SPI2->DR) = data;

    while (!(SPI2->SR & SPI_SR_RXNE));

    return *((volatile uint8_t *)&SPI2->DR);
}

void ltc_write_dac(uint8_t cmd, uint16_t value) {
    ltc_write_dac_cs(cmd, value, LTC_DAC_CS0);
}

void ltc_write_dac_cs(uint8_t cmd, uint16_t value, ltc_dac_cs_t cs) {
    // 1. Pull CS Low to start transaction
    ltc_select_cs(cs);

    // 2. Send Command/Address byte
    spi2_transfer_byte(cmd);

    // 3. Send 16-bit value (MSB first)
    spi2_transfer_byte((value >> 8) & 0xFF); // High Byte
    spi2_transfer_byte(value & 0xFF);        // Low Byte

    // 4. Wait for SPI to not be busy before raising CS
    while (SPI2->SR & SPI_SR_BSY);

    // 5. Pull CS High to latch data
    ltc_deselect_cs(cs);
}
uint16_t ltc_read_data(uint8_t cmd) {
    return ltc_read_data_cs(cmd, LTC_DAC_CS0);
}

uint16_t ltc_read_data_cs(uint8_t cmd, ltc_dac_cs_t cs) {
    uint16_t result = 0;

    ltc_select_cs(cs); // CS Low

    // Send the command to initiate read
    spi2_transfer_byte(cmd);

    // Clock in 2 bytes of data (sending 0x00 as dummy)
    uint8_t msb = spi2_transfer_byte(0x00);
    uint8_t lsb = spi2_transfer_byte(0x00);

    while (SPI2->SR & SPI_SR_BSY);
    ltc_deselect_cs(cs); // CS High

    result = (msb << 8) | lsb;
    return result;
}

/*
// Safe read routine for LTC2686 16-bit registers
uint16_t LTC2686_Read16(uint8_t cmd_addr)
{
    uint16_t received_val = 0;
    
    __disable_irq();
    CS_LOW();
    spi2_transfer_byte(cmd_addr | 0x80); // Send command with Read Bit (0x80) set
    spi2_transfer_byte(0x00);
    spi2_transfer_byte(0x00);
    SPI_WAIT();
    CS_HIGH();
    __enable_irq();

    for(volatile int i = 0; i < 20; i++); // Wait for internal DAC latching

    __disable_irq();
    CS_LOW();
    spi2_transfer_byte(0xFF); // Send NOOP to shift out the read contents
    received_val |= ((uint16_t)spi2_transfer_byte(0x00) << 8);
    received_val |= spi2_transfer_byte(0x00);
    SPI_WAIT();
    CS_HIGH();
    __enable_irq();

    return received_val;
}
    */