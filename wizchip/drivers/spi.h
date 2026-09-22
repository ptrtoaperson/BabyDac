#ifndef SPI_W5500_H
#define SPI_W5500_H

#include "stm32f1xx.h"
#include <stdint.h>

/*W5500 spi function prototypes*/
void ethernet_spi_init(void);

// W5500 required callbacks
void wizchip_select(void);
void wizchip_deselect(void);

uint8_t spi_read(void);
void spi_write(uint8_t data);

void spi_read_burst(uint8_t* buf, uint16_t len);
void spi_write_burst(uint8_t* buf, uint16_t len);

/*LTC2686 SPI function prototypes*/

typedef enum {
	LTC_DAC_CS0 = 0, /* Existing DAC on PB12 */
	LTC_DAC_CS1 = 1  /* New DAC on PC9 */
} ltc_dac_cs_t;

void ltc_spi_init(void);
uint8_t spi2_transfer_byte(uint8_t data);
void ltc_write_dac(uint8_t cmd, uint16_t value);
void ltc_write_dac_cs(uint8_t cmd, uint16_t value, ltc_dac_cs_t cs);
uint16_t ltc_read_data(uint8_t cmd);
uint16_t ltc_read_data_cs(uint8_t cmd, ltc_dac_cs_t cs);
uint16_t LTC2686_Read16(uint8_t cmd_addr);


#endif