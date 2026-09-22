#ifndef FLASH_H
#define FLASH_H

#include "stdint.h"
#include "stm32f1xx.h"

void flash_erase_page(uint32_t pageAddress);
void flash_read32(uint32_t addr, uint32_t *rd32, uint32_t len);
int flash_write32(uint32_t addrs, const uint32_t *wr32, uint32_t len);





#endif 

/*
    flash_write32(0x08007000, (uint32_t *)cmd->IPaddr, 2); // Write IP and PORT flash
//test flash write and read
    uint32_t flash_data[2] = {0};
    char dispbuf[50] = {0};
    uart1_print("Reading back flash data...\r\n");
    flash_read32(0x08007000,flash_data,2); //read flash

    uint8_t *flshPtr = (uint8_t *)flash_data; 
   
        sprintf(prntbuf,"Writing IP address %d.%d.%d.%d, port %u to flash\n",*flshPtr, *(flshPtr+1) ,*(flshPtr+2),
           *(flshPtr+3),  *(uint32_t *)flshPtr);
    uart1_print(dispbuf);
    
*/  