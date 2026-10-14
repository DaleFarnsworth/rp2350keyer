#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Persistent Vail settings, kept in the last flash sector
// (the equivalent of the Vail adapter's EEPROM settings).
typedef struct {
  uint8_t keyer_type;
  uint16_t dit_duration_ms;
  uint8_t tx_note;
} vail_settings_t;

// Returns false (and fills in defaults) if nothing valid is stored.
bool settings_load(vail_settings_t *s);

// Schedule a write; it happens SETTINGS_SAVE_DELAY_MS after the last call.
void settings_save_later(const vail_settings_t *s);

// Call from the main loop.
void settings_task(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
