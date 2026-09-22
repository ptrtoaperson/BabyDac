#ifndef LANTASK_H
#define LANTASK_H

#include "stm32f1xx.h"
#include "spi.h"
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include "stdbool.h"


void hardware_reset(void);
void w5500_port_init(void);
void run_tcp_server(volatile bool *flg);
void lan_set_tcp_port(uint16_t port);
uint16_t lan_get_tcp_port(void);
void lan_reopen_listener(void);
static void delay_ms(uint32_t ms);

// --- Configuration ---
#define W5500_RST_PORT  GPIOA
#define W5500_RST_PIN   0    
#define TCP_PORT        5000  
#define DATA_BUF_SIZE   2048



#endif