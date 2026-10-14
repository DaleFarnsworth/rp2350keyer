#pragma once

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fills `frames` stereo frames (packed as (right << 16) | left).
// Called from the DMA interrupt.
typedef void (*i2s_fill_fn)(uint32_t *buf, uint32_t frames);

// Start continuous I2S output. The clock runs from this point on, which the
// TLV320DAC3100 needs because its PLL is fed from BCLK.
void i2s_out_init(uint data_pin, uint clock_pin_base, uint32_t sample_rate, i2s_fill_fn fill);

#ifdef __cplusplus
}
#endif
