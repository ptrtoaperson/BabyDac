#pragma once

#include <stdint.h>

#include "usbd_cdc.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);
//void CDC_Process(void);
/* Minimal CDC interface for echo demo */
