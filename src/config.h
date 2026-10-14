#pragma once

// Board configuration for a Raspberry Pi Pico 2 wired to an
// Adafruit TLV320DAC3100 I2S DAC breakout.

// ---- TLV320DAC3100 wiring -------------------------------------------------
// I2S: BCLK and WSEL must be consecutive pins (PIO side-set).
#define I2S_DATA_PIN        9   // -> DAC "DIN"
#define I2S_BCLK_PIN        10  // -> DAC "BCK"
#define I2S_WSEL_PIN        11  // -> DAC "WSEL" (must be I2S_BCLK_PIN + 1)
// DAC "MCLK" is left unconnected: the DAC PLL runs from BCLK.

#define DAC_I2C_INST        i2c0
#define DAC_I2C_SDA_PIN     4   // -> DAC "SDA"
#define DAC_I2C_SCL_PIN     5   // -> DAC "SCL"
#define DAC_I2C_ADDR        0x18
#define DAC_I2C_BAUD        400000
#define DAC_RESET_PIN       7   // -> DAC "RST" (active low)

// Enable the DAC3100's mono class-D speaker output in addition to the
// headphone output.
#define DAC_ENABLE_SPEAKER  1

// Headphone and speaker analog attenuation, in register units
// (0 = 0 dB, roughly -0.5 dB per step, see datasheet table 6-24).
#define DAC_HP_ANALOG_ATTEN   12
#define DAC_SPK_ANALOG_ATTEN  12

// ---- Morse key inputs (active low, internal pull-ups) ---------------------
#define DIT_PIN             2
#define DAH_PIN             3
#define KEY_PIN             6   // straight key
#define DEBOUNCE_MS         10  // same as the Vail adapter's Bounce default

#define LED_PIN             PICO_DEFAULT_LED_PIN

// ---- Audio ----------------------------------------------------------------
#define AUDIO_SAMPLE_RATE   48000
// sys clock chosen so the I2S PIO divider is an integer:
// 153.6 MHz / (2 * 32 * 48 kHz) = 50.
#define SYS_CLOCK_KHZ       153600

// Sidetone peak level relative to full scale (0..32767).
#define SIDETONE_AMPLITUDE  8192    // about -12 dBFS
// Rise/fall time of the sidetone envelope, to avoid key clicks.
#define SIDETONE_RAMP_MS    5

// ---- Vail adapter defaults (from vail-adapter config.h) -------------------
#define DEFAULT_TONE_NOTE                69   // A4, 440 Hz
#define DEFAULT_ADAPTER_DIT_DURATION_MS  100
#define DEFAULT_KEYER_TYPE               1    // straight key

// Settings are written to flash this long after the last MIDI change,
// so a burst of CC messages costs a single erase.
#define SETTINGS_SAVE_DELAY_MS 2000
