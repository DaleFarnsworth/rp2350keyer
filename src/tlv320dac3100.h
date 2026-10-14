#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configure the TLV320DAC3100 for 48 kHz, 16-bit I2S slave operation with
// the PLL clocked from BCLK. I2S must already be running.
bool dac3100_init(void);

// DAC digital volume, both channels, in 0.5 dB steps (-127 = -63.5 dB,
// 48 = +24 dB).
bool dac3100_set_digital_volume(int8_t half_db);

#ifdef __cplusplus
}
#endif
