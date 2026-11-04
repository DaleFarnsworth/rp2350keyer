#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts I2S, then configures the TLV320DAC3100.
void audio_init(void);
// Retries the DAC's setup once a second until it responds
void audio_dac_task(uint32_t now);

// Moves received USB audio towards the I2S output. Call from the main loop.
void audio_task(void);

bool audio_streaming(void);

// For a flash write, which holds off interrupts: fade the output out and
// hold it on silence. Call until it returns true, then write, then call
// audio_release_output(). It doesn't start while the sidetone sounds.
bool audio_hold_output(void);
void audio_release_output(void);

#ifdef __cplusplus
}
#endif
