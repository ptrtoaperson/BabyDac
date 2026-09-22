#include <stdlib.h>
#include "ltc268x.h"
#include "stm32f1xx.h"

// Low-level full duplex master transfer function from your spi.c file
extern uint8_t spi2_transfer_byte(uint8_t data);

#define CS_LOW()   (GPIOB->BSRR = GPIO_BSRR_BR12)
#define CS_HIGH()  (GPIOB->BSRR = GPIO_BSRR_BS12)
#define SPI_WAIT() while(SPI2->SR & SPI_SR_BSY)

// Persistent allocation buffer to satisfy the init instantiation handle
static struct ltc268x_dev global_dac_device;

void no_os_mdelay(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms; i++) {
        for (volatile int j = 0; j < 8000; j++);
    }
}

void *no_os_calloc(size_t nmemb, size_t size) {
    return &global_dac_device;
}

void no_os_free(void *ptr) {
    // Persistent memory allocation block - ignore calls to free
}

int32_t no_os_spi_init(struct no_os_spi_desc **desc, const struct no_os_spi_init_param *param) {
    static struct no_os_spi_desc mock_desc;
    *desc = &mock_desc;
    return 0;
}

int32_t no_os_spi_remove(struct no_os_spi_desc *desc) {
    return 0;
}

// Master SPI abstraction routine that links ADI logic directly to your registers
int32_t no_os_spi_write_and_read(struct no_os_spi_desc *desc, uint8_t *data, uint8_t bytes_number) {
    __disable_irq();
    CS_LOW();

    for (uint8_t i = 0; i < bytes_number; i++) {
        data[i] = spi2_transfer_byte(data[i]);
    }

    SPI_WAIT();
    CS_HIGH();
    __enable_irq();

    return 0;
}