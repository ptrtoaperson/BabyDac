#ifndef LTC2686_HW_H
#define LTC2686_HW_H

#include <stdint.h>

// TGP Pin Mapping
// Bank 0 (LTC2686 logical channels 0-7):
//   TGP0 -> PC6
//   TGP1 -> PC7
//   TGP2 -> PC8
// Bank 1 (LTC2688 logical channels 8-23):
//   TGP0 -> PA0
//   TGP1 -> PA1
//   TGP2 -> PA2

#define TGP_BANK_COUNT 2u
#define TGP_PER_BANK   3u

void LTC2686_TGP_Init(void);

// Set pin to toggle at a frequency indefinitely
void LTC2686_SetPinFrequency(uint8_t tgp_num, float frequency_hz);
void Set_TGP_Frequency_Duty_Bank(uint8_t bank, uint8_t tgp_num, float frequency_hz, uint8_t duty_cycle_percent);
void Set_TGP_Frequency_Bank(uint8_t bank, uint8_t tgp_num, float frequency_hz);
void Set_TGP_Frequency(uint8_t tgp_num, float frequency_hz);
void LTC2686_StopHardwarePWM(uint8_t channel, uint8_t tgp_num);



// Set pin to toggle at a frequency for a specific duration (period)
void LTC2686_SetPinPulse(uint8_t tgp_num, float frequency_hz, uint32_t duration_ms);
#endif