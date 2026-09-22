#ifndef NO_OS_SPI_H_
#define NO_OS_SPI_H_

#include <stdint.h>
#include <stdbool.h>

// Mock structures to satisfy Analog Devices driver signatures
struct no_os_spi_init_param {
    uint8_t chip_select; 
};

struct no_os_spi_desc {
    uint8_t chip_select; 
};

// FIX: Macro names must be alphanumeric identifiers (no minus signs!)
#define ENODEV 19
#define ENOENT 2
#define ENOMEM 12

// Standard bit-mask utilities used inside the ADI driver
#define NO_OS_BIT(x) (1ULL << (x))

#endif /* NO_OS_SPI_H_ */