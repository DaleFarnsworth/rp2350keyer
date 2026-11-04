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

// Hold the output on silence that needs no interrupt to keep it going, for
// a flash write. The clock keeps running. Each channel switches as it
// finishes its buffer, so what's already queued still plays; call
// i2s_out_held() until it says both have. Releasing goes back to the fill
// callback the same way.
void i2s_out_hold(bool on);
bool i2s_out_held(void);

#ifdef __cplusplus
}
#endif
