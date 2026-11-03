#ifndef _HANG_TRACE_H_
#define _HANG_TRACE_H_

// Finding where the firmware stops. The main loop records which step it's
// in, in watchdog scratch registers, which survive a watchdog reset. If the
// loop stops for HANG_TRACE_TIMEOUT_MS the watchdog restarts the RP2350,
// and the next start reports where the last run was stuck.

#include "hardware/structs/watchdog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HANG_TRACE_TIMEOUT_MS 3000

enum {
  STAGE_BOOT = 1,
  STAGE_TUD_TASK,
  STAGE_USB_DEBUG,
  STAGE_AUDIO_TASK,
  STAGE_MIDI_TASK,
  STAGE_ADAPTER_TICK,
  STAGE_KEYS,
  STAGE_SETTINGS,
  STAGE_LED,
  STAGE_CB_SET_ITF,         // inside TinyUSB callbacks, called from tud_task
  STAGE_CB_SET_ITF_CLOSE,
  STAGE_CB_GET_REQ,
  STAGE_CB_SET_REQ,
};

// scratch[0]: stage, scratch[1]: main loop count, scratch[2]: DMA
// interrupts. scratch[4..7] belong to the bootrom.
static inline void hang_stage(uint32_t s) { watchdog_hw->scratch[0] = s; }
static inline void hang_loop(void) { watchdog_hw->scratch[1]++; }
static inline void hang_dma_irq(void) { watchdog_hw->scratch[2]++; }

void hang_trace_start(void);   // report the last run if it hung, then arm
void hang_trace_feed(void);

#ifdef __cplusplus
}
#endif

#endif
