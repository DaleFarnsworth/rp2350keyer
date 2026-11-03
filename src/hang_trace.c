#include "hang_trace.h"

#include <stdio.h>

#include "hardware/watchdog.h"

static const char *const stage_names[] = {
  [0] = "none",
  [STAGE_BOOT] = "startup",
  [STAGE_TUD_TASK] = "tud_task",
  [STAGE_USB_DEBUG] = "usb_debug_task",
  [STAGE_AUDIO_TASK] = "audio_task",
  [STAGE_MIDI_TASK] = "midi_task",
  [STAGE_ADAPTER_TICK] = "adapter.Tick",
  [STAGE_KEYS] = "key inputs",
  [STAGE_SETTINGS] = "settings_task",
  [STAGE_LED] = "led_task",
  [STAGE_CB_SET_ITF] = "tud_audio_set_itf_cb",
  [STAGE_CB_SET_ITF_CLOSE] = "tud_audio_set_itf_close_EP_cb",
  [STAGE_CB_GET_REQ] = "tud_audio_get_req_entity_cb",
  [STAGE_CB_SET_REQ] = "tud_audio_set_req_entity_cb",
};

void hang_trace_start(void) {
  if (watchdog_enable_caused_reboot()) {
    uint32_t s = watchdog_hw->scratch[0];
    printf("*** Last run stopped and the watchdog restarted it: stuck in %s "
           "(stage %lu), after %lu main loops and %lu DMA interrupts\n",
           s < sizeof(stage_names) / sizeof(stage_names[0]) && stage_names[s]
               ? stage_names[s] : "?",
           (unsigned long) s, (unsigned long) watchdog_hw->scratch[1],
           (unsigned long) watchdog_hw->scratch[2]);
  }
  watchdog_hw->scratch[0] = STAGE_BOOT;
  watchdog_hw->scratch[1] = 0;
  watchdog_hw->scratch[2] = 0;
  watchdog_enable(HANG_TRACE_TIMEOUT_MS, true);
}

void hang_trace_feed(void) {
  watchdog_update();
}
