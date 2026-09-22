#ifndef UART_H_
#define UART_H_

//#include "stdio.h"
#include <stddef.h>  // Defines NULL, size_t, etc.

int uart1_init(void);
void uart1_write(int ch);
char uart1_read(void);
void uart1_set_rx_callback(void (*callback)(char));

// Additional helper functions
void uart1_print(const char *str);
void uart1_print_hex(uint8_t value);

#endif /* UART_H_ */
