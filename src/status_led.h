#pragma once

// The RP2350-Zero's status light: a WS2812 RGB LED, driven from a PIO
// state machine.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call after the system clock is set, since the LED's bit rate comes from it.
void status_led_init(void);
void status_led_set(bool on);

#ifdef __cplusplus
}
#endif
