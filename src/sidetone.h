#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Prioritized monophonic sidetone, the DAC counterpart of the Vail adapter's
// PolyBuzzer: a tone in a lower-numbered slot plays in preference to one in
// a higher-numbered slot. Slot 0 is local keying, slot 1 is MIDI notes.
#define SIDETONE_SLOTS 2

void sidetone_init(void);
void sidetone_tone(int slot, uint32_t frequency_hz);
void sidetone_note(int slot, uint8_t note);
void sidetone_off(int slot);

// Next mono sample, called from the I2S fill interrupt.
int32_t sidetone_next_sample(void);

#ifdef __cplusplus
}
#endif
