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

// Hard fault: find the stacked exception frame (MSP or PSP, from bit 2 of
// EXC_RETURN in LR), then record where the fault happened.
void __attribute__((naked)) isr_hardfault(void) {
  __asm volatile(
    "movs r0, #4\n"
    "mov r1, lr\n"
    "tst r0, r1\n"
    "beq 1f\n"
    "mrs r0, psp\n"
    "b hang_trace_fault\n"
    "1:\n"
    "mrs r0, msp\n"
    "b hang_trace_fault\n");
}

void __attribute__((used, noreturn)) hang_trace_fault(uint32_t *frame) {
  // frame: r0 r1 r2 r3 r12 lr pc xpsr
  watchdog_hw->scratch[0] |= HANG_FAULT;
  watchdog_hw->scratch[1] = frame[6];
  watchdog_hw->scratch[2] = frame[5];
  watchdog_hw->scratch[3] = *(volatile uint32_t *) 0xe000ed28;   // CFSR
  while (true) {
    // Wait for the watchdog, which is no longer being fed
  }
}

void hang_trace_start(void) {
  uint32_t s0 = watchdog_hw->scratch[0];
  if (watchdog_enable_caused_reboot() && (s0 & HANG_FAULT)) {
    uint32_t s = s0 & ~HANG_FAULT;
    printf("*** Last run crashed (CPU fault) in %s (stage %lu): PC 0x%08lx, "
           "LR 0x%08lx, CFSR 0x%08lx\n",
           s < sizeof(stage_names) / sizeof(stage_names[0]) && stage_names[s]
               ? stage_names[s] : "?",
           (unsigned long) s, (unsigned long) watchdog_hw->scratch[1],
           (unsigned long) watchdog_hw->scratch[2], (unsigned long) watchdog_hw->scratch[3]);
  } else if (watchdog_enable_caused_reboot()) {
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
  watchdog_hw->scratch[3] = 0;
  watchdog_enable(HANG_TRACE_TIMEOUT_MS, true);
}

void hang_trace_feed(void) {
  watchdog_update();
}
