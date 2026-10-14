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
static volatile uint32_t amp_max;     // Q16 peak level
static volatile uint32_t amp_step;    // Q16 change per sample

// Interrupt-only state
static uint32_t phase;
static uint32_t cur_inc;
static uint32_t amp;                  // Q16 envelope level, 0..amp_max

#define RAMP_SAMPLES (AUDIO_SAMPLE_RATE / 1000 * SIDETONE_RAMP_MS)
// Rate for moving down to a lowered volume: full scale in one ramp time
#define GLIDE_STEP   ((32767u << 16) / RAMP_SAMPLES)

void sidetone_init(void) {
  for (uint32_t i = 0; i <= SINE_LEN; i++) {
    sine_table[i] = (int16_t) lrintf(32767.0f * sinf(2.0f * (float) M_PI * (float) i / SINE_LEN));
  }
  sidetone_set_volume(DEFAULT_SIDETONE_VOLUME);
}

void sidetone_set_volume(uint8_t volume) {
  if (volume > 127) volume = 127;
  uint32_t max = 0;
  if (volume > 0) {
    float level = 32767.0f * powf(10.0f, ((float) volume - 127.0f) / 40.0f);
    max = (uint32_t) lrintf(level) << 16;
  }
  // The ramp takes SIDETONE_RAMP_MS at any volume
  uint32_t step = max / RAMP_SAMPLES;
  amp_step = step ? step : 1;
  amp_max = max;
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
    // A volume change while keyed glides to the new level.
    cur_inc = phase_inc;
    uint32_t max = amp_max;
    uint32_t step = amp_step;
    if (amp < max) {
      amp = (max - amp > step) ? amp + step : max;
    } else if (amp > max) {
      amp = (amp - max > GLIDE_STEP) ? amp - GLIDE_STEP : max;
    }
  } else {
    // Above the current volume (it was just lowered), use the faster rate
    uint32_t step = (amp > amp_max) ? GLIDE_STEP : amp_step;
    amp = (amp > step) ? amp - step : 0;
  }
  if (amp == 0) {
    phase = 0;
    return 0;
  }

  uint32_t idx = phase >> (32 - SINE_BITS);
  int32_t frac = (int32_t) ((phase >> (32 - SINE_BITS - 15)) & 0x7fff);
  int32_t a = sine_table[idx];
  int32_t b = sine_table[idx + 1];
  int32_t s = a + (((b - a) * frac) >> 15);
  phase += cur_inc;

  return (s * (int32_t) (amp >> 16)) >> 15;
}
