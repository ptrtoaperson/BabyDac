#ifndef I2C_H
#define I2C_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void i2c_init(void);
uint8_t i2c_start(uint8_t address);
void i2c_stop(void);
uint8_t i2c_write(uint8_t data);
uint8_t i2c_read_ack(void);
uint8_t i2c_read_nack(void);
bool eui48_read_mac(uint8_t mac[6]);
bool eui48_get_last_source(uint8_t *bus_index, uint8_t *addr_7bit);
bool eui48_get_probe_status(uint8_t *ack_count, uint8_t *first_addr_7bit, uint8_t *ack_mask);
bool eui48_get_last_raw_read(uint8_t mac[6], uint8_t *addr_7bit, bool *is_valid);
void eui48_get_diag_counters(uint8_t *addr_ack, uint8_t *ptr_ok, uint8_t *read_start_ok, uint8_t *raw_reads);

#ifdef __cplusplus
}
#endif

#endif // I2C_H
