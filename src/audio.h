#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts I2S, then configures the TLV320DAC3100.
void audio_init(void);

// Moves received USB audio towards the I2S output. Call from the main loop.
void audio_task(void);

bool audio_streaming(void);

#ifdef __cplusplus
}
#endif
