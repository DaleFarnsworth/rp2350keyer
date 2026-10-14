#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hardware/flash.h"
#include "pico/flash.h"

#define SETTINGS_MAGIC    0x4C494156u   // "VAIL"
#define SETTINGS_VERSION  3
#define SETTINGS_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
  uint32_t magic;
  uint32_t version;
  vail_settings_t s;
  uint32_t check;
} stored_t;

static vail_settings_t pending;
static bool save_pending;
static uint32_t last_change_ms;

// Field by field, so struct padding never matters
static uint32_t checksum(const vail_settings_t *s) {
  uint8_t bytes[7] = { s->keyer_type, (uint8_t) s->dit_duration_ms,
                       (uint8_t) (s->dit_duration_ms >> 8), s->tx_note,
                       (uint8_t) s->tx_freq_hz, (uint8_t) (s->tx_freq_hz >> 8),
                       s->sidetone_volume };
  uint32_t h = 2166136261u;     // FNV-1a
  for (size_t i = 0; i < sizeof(bytes); i++) {
    h = (h ^ bytes[i]) * 16777619u;
  }
  return h;
}

static bool same(const vail_settings_t *a, const vail_settings_t *b) {
  return a->keyer_type == b->keyer_type && a->dit_duration_ms == b->dit_duration_ms &&
         a->tx_note == b->tx_note && a->tx_freq_hz == b->tx_freq_hz &&
         a->sidetone_volume == b->sidetone_volume;
}

bool settings_load(vail_settings_t *s) {
  const stored_t *st = (const stored_t *) (XIP_BASE + SETTINGS_OFFSET);
  if (st->magic == SETTINGS_MAGIC && st->version == SETTINGS_VERSION &&
      st->check == checksum(&st->s) && st->s.keyer_type <= 9 && st->s.tx_note <= 127 &&
      st->s.tx_freq_hz <= 1270 && st->s.sidetone_volume <= 127) {
    *s = st->s;
    return true;
  }
  s->keyer_type = DEFAULT_KEYER_TYPE;
  s->dit_duration_ms = DEFAULT_ADAPTER_DIT_DURATION_MS;
  s->tx_note = DEFAULT_TONE_NOTE;
  s->tx_freq_hz = 0;
  s->sidetone_volume = DEFAULT_SIDETONE_VOLUME;
  return false;
}

void settings_save_later(const vail_settings_t *s) {
  pending = *s;
  save_pending = true;
  last_change_ms = 0;   // stamped by settings_task
}

static void do_write(void *param) {
  const uint8_t *page = (const uint8_t *) param;
  flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
}

void settings_task(uint32_t now_ms) {
  if (!save_pending) return;
  if (last_change_ms == 0) {
    last_change_ms = now_ms ? now_ms : 1;
    return;
  }
  if (now_ms - last_change_ms < SETTINGS_SAVE_DELAY_MS) return;
  save_pending = false;

  vail_settings_t current;
  if (settings_load(&current) && same(&current, &pending)) {
    return;   // nothing changed, spare the flash
  }

  static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
  memset(page, 0xff, sizeof(page));
  stored_t st;
  memset(&st, 0, sizeof(st));
  st.magic = SETTINGS_MAGIC;
  st.version = SETTINGS_VERSION;
  st.s = pending;
  st.check = checksum(&st.s);
  memcpy(page, &st, sizeof(st));

  // Interrupts are held off for the erase (~50 ms), so audio glitches once.
  int rc = flash_safe_execute(do_write, page, 500);
  printf("settings: saved keyer=%u dit=%u note=%u freq=%u volume=%u (rc=%d)\n",
         pending.keyer_type, pending.dit_duration_ms, pending.tx_note, pending.tx_freq_hz,
         pending.sidetone_volume, rc);
}
