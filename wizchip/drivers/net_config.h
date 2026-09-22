#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define NETCFG_FLASH_ADDR      0x0807F800u
#define NETCFG_MAGIC           0x4E434647u
#define NETCFG_DEFAULT_PORT    5000u

bool netcfg_load(uint8_t ip[4], uint16_t *port);
int netcfg_save(const uint8_t ip[4], uint16_t port);
void netcfg_clear(void);

#endif
