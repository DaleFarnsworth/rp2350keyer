#include "sidetone.h"

#include <math.h>
#include <stdbool.h>

#include "config.h"

#define SINE_BITS 8
#define SINE_LEN  (1u << SINE_BITS)

static int16_t sine_table[SINE_LEN + 1];  // +1 guard entry for interpolation

static uint32_t tones[SIDETONE_SLOTS];

// Shared with the I2S interrupt
static volatile uint32_t phase_inc;   // 0 when silent
static volatile bool gate;

// Interrupt-only state
static uint32_t phase;
static uint32_t cur_inc;
static uint32_t amp;                  // Q16 envelope level, 0..SIDETONE_AMPLITUDE<<16

#define AMP_MAX  ((uint32_t) SIDETONE_AMPLITUDE << 16)
#define AMP_STEP (AMP_MAX / (AUDIO_SAMPLE_RATE / 1000 * SIDETONE_RAMP_MS))

void sidetone_init(void) {
  for (uint32_t i = 0; i <= SINE_LEN; i++) {
    sine_table[i] = (int16_t) lrintf(32767.0f * sinf(2.0f * (float) M_PI * (float) i / SINE_LEN));
  }
}

static void update(void) {
  for (int i = 0; i < SIDETONE_SLOTS; i++) {
    if (tones[i]) {
      phase_inc = (uint32_t) (((uint64_t) tones[i] << 32) / AUDIO_SAMPLE_RATE);
      gate = true;
      return;
    }
  }
  gate = false;
}

void sidetone_tone(int slot, uint32_t frequency_hz) {
  if (slot < 0 || slot >= SIDETONE_SLOTS) return;
  if (frequency_hz > AUDIO_SAMPLE_RATE / 2) frequency_hz = 0;
  tones[slot] = frequency_hz;
  update();
}

void sidetone_note(int slot, uint8_t note) {
  if (note > 127) note = 127;
  float hz = 440.0f * powf(2.0f, ((float) note - 69.0f) / 12.0f);
  sidetone_tone(slot, (uint32_t) lrintf(hz));
}

void sidetone_off(int slot) {
  sidetone_tone(slot, 0);
}

int32_t sidetone_next_sample(void) {
  if (gate) {
    // Keep the old pitch through the release so the fade-out is clean,
    // but follow pitch changes while keyed.
    cur_inc = phase_inc;
    amp = (amp + AMP_STEP > AMP_MAX) ? AMP_MAX : amp + AMP_STEP;
  } else {
    if (amp == 0) {
      phase = 0;
      return 0;
    }
    amp = (amp > AMP_STEP) ? amp - AMP_STEP : 0;
  }

  uint32_t idx = phase >> (32 - SINE_BITS);
  int32_t frac = (int32_t) ((phase >> (32 - SINE_BITS - 15)) & 0x7fff);
  int32_t a = sine_table[idx];
  int32_t b = sine_table[idx + 1];
  int32_t s = a + (((b - a) * frac) >> 15);
  phase += cur_inc;

  return (s * (int32_t) (amp >> 16)) >> 15;
}
