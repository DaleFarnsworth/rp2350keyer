// USB Audio Class 2 speaker -> I2S -> TLV320DAC3100, with sidetone mixed in.
//
// Data path:
//   USB ISO OUT -> TinyUSB FIFO (elastic, ~16 ms) -> staging ring (~5 ms)
//   -> I2S DMA buffers, where host volume is applied and sidetone is added.
//
// The staging ring is only topped up to a fixed level, so the TinyUSB FIFO
// drains at exactly the I2S rate and its fill level drives the asynchronous
// feedback endpoint (AUDIO_FEEDBACK_METHOD_FIFO_COUNT).

#include "audio.h"

#include <math.h>
#include <stdio.h>

#include "hang_trace.h"
#include "hardware/structs/usb.h"
#include "hardware/sync.h"
#include <string.h>

#include "config.h"
#include "i2s_out.h"
#include "sidetone.h"
#include "tlv320dac3100.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define N_CHANNELS          CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX
#define BYTES_PER_FRAME     (N_CHANNELS * CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX)

#define RING_FRAMES         512u            // power of two
#define RING_MASK           (RING_FRAMES - 1)
#define RING_TARGET_FRAMES  256u

// Volume range reported to the host, in 1/256 dB
#define VOLUME_MIN          (-60 * 256)
#define VOLUME_MAX          0
#define VOLUME_RES          256

static uint32_t ring[RING_FRAMES];
static volatile uint32_t ring_head;     // written by audio_task
static volatile uint32_t ring_tail;     // written by the I2S interrupt
static volatile bool underrun;

static volatile bool streaming;
static bool priming;

static int8_t mute[N_CHANNELS + 1];     // [0] is the master channel
static int16_t volume[N_CHANNELS + 1];
static volatile int32_t gain_q15[N_CHANNELS] = { 32768, 32768 };
// The host's volume without its mute, for the sidetone
static volatile int32_t sidetone_gain_q15[N_CHANNELS] = { 32768, 32768 };

// Gain on the computer's audio while the sidetone sounds, in Q15, ramped
// so muting and unmuting don't click. Interrupt-only state.
#define RX_MUTE_RAMP_SAMPLES (AUDIO_SAMPLE_RATE / 1000 * SIDETONE_RAMP_MS)
#define RX_MUTE_STEP         (32768 / RX_MUTE_RAMP_SAMPLES)
#define RX_MUTE_HANG_SAMPLES (AUDIO_SAMPLE_RATE / 1000 * RX_MUTE_HANG_MS)
static int32_t rx_gain = 32768;
static uint32_t rx_mute_hang;

// Gain on the whole output, in Q15, faded to silence for a flash write
#define OUT_FADE_STEP (32768 / (AUDIO_SAMPLE_RATE / 1000 * SIDETONE_RAMP_MS))
static volatile bool out_fade;
static volatile int32_t out_gain = 32768;

static inline int16_t clip16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t) v;
}

// I2S DMA interrupt: drain the staging ring, apply volume, add sidetone.
static void fill_i2s(uint32_t *buf, uint32_t frames) {
  uint32_t tail = ring_tail;
  uint32_t head = ring_head;
  int32_t gl = gain_q15[0];
  int32_t gr = gain_q15[1];
  int32_t og = out_gain;
#if SIDETONE_FOLLOWS_HOST_VOLUME
  int32_t sgl = sidetone_gain_q15[0];
  int32_t sgr = sidetone_gain_q15[1];
#else
  int32_t sgl = 32768, sgr = 32768;
#endif

  for (uint32_t i = 0; i < frames; i++) {
    uint32_t f = 0;
    if ((int32_t) (head - tail) > 0) {
      f = ring[tail & RING_MASK];
      tail++;
    } else if (streaming && !priming) {
      underrun = true;
    }
    int32_t st = sidetone_next_sample();
#if RX_MUTE_DURING_SIDETONE
    if (sidetone_sounding()) {
      rx_mute_hang = RX_MUTE_HANG_SAMPLES + 1;
    } else if (rx_mute_hang) {
      rx_mute_hang--;
    }
    if (rx_mute_hang) {
      rx_gain = (rx_gain > RX_MUTE_STEP) ? rx_gain - RX_MUTE_STEP : 0;
    } else {
      rx_gain = (rx_gain < 32768 - RX_MUTE_STEP) ? rx_gain + RX_MUTE_STEP : 32768;
    }
    int32_t l = (((int32_t) (int16_t) (f & 0xffff) * gl) >> 15) * rx_gain >> 15;
    int32_t r = (((int32_t) (int16_t) (f >> 16) * gr) >> 15) * rx_gain >> 15;
#else
    int32_t l = ((int32_t) (int16_t) (f & 0xffff) * gl) >> 15;
    int32_t r = ((int32_t) (int16_t) (f >> 16) * gr) >> 15;
#endif
    int32_t st_l = (st * sgl) >> 15;
    int32_t st_r = (st * sgr) >> 15;
    if (out_fade) {
      og = (og > OUT_FADE_STEP) ? og - OUT_FADE_STEP : 0;
    } else {
      og = (og < 32768 - OUT_FADE_STEP) ? og + OUT_FADE_STEP : 32768;
    }
    int32_t out_l = (clip16(l + st_l) * og) >> 15;
    int32_t out_r = (clip16(r + st_r) * og) >> 15;
    buf[i] = ((uint32_t) (uint16_t) out_r << 16) | (uint16_t) out_l;
  }
  ring_tail = tail;
  out_gain = og;
}

bool audio_hold_output(void) {
  if (!out_fade) {
    if (sidetone_sounding()) return false;
    out_fade = true;
  }
  if (out_gain == 0) i2s_out_hold(true);
  return i2s_out_held();
}

void audio_release_output(void) {
  i2s_out_hold(false);
  out_fade = false;
}

static bool dac_ready;
static uint32_t dac_tries;

void audio_init(void) {
  sidetone_init();
  i2s_out_init(I2S_DATA_PIN, I2S_BCLK_PIN, AUDIO_SAMPLE_RATE, fill_i2s);
  dac_tries = 1;
  dac_ready = dac3100_init(true);
}

void audio_dac_task(uint32_t now) {
  static uint32_t last_try;
  if (dac_ready || now - last_try < 1000) return;
  last_try = now;
  dac_tries++;
  // Log the first try, then once a minute
  bool report = dac_tries % 60 == 0;
  if (report) {
    printf("dac3100: still no response after %lu tries\n",
           (unsigned long) dac_tries);
  }
  dac_ready = dac3100_init(report);
  if (dac_ready) {
    printf("dac3100: set up after %lu tries\n", (unsigned long) dac_tries);
  }
}

bool audio_streaming(void) {
  return streaming;
}

void audio_task(void) {
  static uint8_t tmp[RING_TARGET_FRAMES * BYTES_PER_FRAME];

  if (!streaming) return;

  if (underrun) {
    underrun = false;
    if (tud_audio_available() < CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 4) {
      priming = true;
    }
  }

  if (priming) {
    // Let the USB FIFO reach its half-full set point before playing
    if (tud_audio_available() < CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 2) return;
    priming = false;
  }

  uint32_t head = ring_head;
  int32_t fill = (int32_t) (head - ring_tail);
  if (fill < 0) {
    // The ring was reset while the interrupt was mid-buffer
    head = ring_tail;
    fill = 0;
  }
  if (fill >= (int32_t) RING_TARGET_FRAMES) return;

  uint32_t want = (RING_TARGET_FRAMES - (uint32_t) fill) * BYTES_PER_FRAME;
  uint32_t avail = tud_audio_available();
  if (want > avail) want = avail;
  want -= want % BYTES_PER_FRAME;
  if (want == 0) return;

  uint32_t got = tud_audio_read(tmp, (uint16_t) want);
  uint32_t frames = got / BYTES_PER_FRAME;
  for (uint32_t i = 0; i < frames; i++) {
    uint32_t f;
    memcpy(&f, &tmp[i * BYTES_PER_FRAME], sizeof(f));
    ring[head & RING_MASK] = f;
    head++;
  }
  ring_head = head;
}

static void update_gains(void) {
  for (int ch = 0; ch < N_CHANNELS; ch++) {
    float db = (float) (volume[0] + volume[ch + 1]) / 256.0f;
    int32_t g = (int32_t) lrintf(32768.0f * powf(10.0f, db / 20.0f));
    sidetone_gain_q15[ch] = g;
    gain_q15[ch] = (mute[0] || mute[ch + 1]) ? 0 : g;
  }
}

//--------------------------------------------------------------------+
// TinyUSB audio callbacks
//--------------------------------------------------------------------+

static bool clock_get_request(uint8_t rhport, audio_control_request_t const *request) {
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    if (request->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_4_t curf = { (int32_t) tu_htole32(AUDIO_SAMPLE_RATE) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &curf, sizeof(curf));
    } else if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(1) rangef = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { (int32_t) AUDIO_SAMPLE_RATE, (int32_t) AUDIO_SAMPLE_RATE, 0 },
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &rangef, sizeof(rangef));
    }
  } else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t cur_valid = { .bCur = 1 };
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_valid, sizeof(cur_valid));
  }
  return false;
}

static bool feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request) {
  if (request->bChannelNumber > N_CHANNELS) return false;

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t mute1 = { .bCur = mute[request->bChannelNumber] };
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &mute1, sizeof(mute1));
  } else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_2_n_t(1) range_vol = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { tu_htole16(VOLUME_MIN), tu_htole16(VOLUME_MAX), tu_htole16(VOLUME_RES) },
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &range_vol, sizeof(range_vol));
    } else if (request->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(volume[request->bChannelNumber]) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_vol, sizeof(cur_vol));
    }
  }
  return false;
}

static bool feature_unit_set_request(audio_control_request_t const *request, uint8_t const *buf) {
  if (request->bRequest != AUDIO_CS_REQ_CUR) return false;
  if (request->bChannelNumber > N_CHANNELS) return false;

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));
    mute[request->bChannelNumber] = ((audio_control_cur_1_t const *) buf)->bCur;
    printf("audio: channel %u mute %u\n", request->bChannelNumber, mute[request->bChannelNumber]);
    update_gains();
    return true;
  } else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
    int16_t v = ((audio_control_cur_2_t const *) buf)->bCur;
    if (v < VOLUME_MIN) v = VOLUME_MIN;
    if (v > VOLUME_MAX) v = VOLUME_MAX;
    volume[request->bChannelNumber] = v;
    printf("audio: channel %u volume %d/256 dB\n", request->bChannelNumber, v);
    update_gains();
    return true;
  }
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  hang_stage(STAGE_CB_GET_REQ);
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK) return clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return feature_unit_get_request(rhport, request);
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  hang_stage(STAGE_CB_SET_REQ);
  (void) rhport;
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return feature_unit_set_request(request, buf);
  return false;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  hang_stage(STAGE_CB_SET_ITF);
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  printf("audio: interface %u alt %u\n", itf, alt);

  if (itf == ITF_NUM_AUDIO_STREAMING && alt != 0) {
    ring_head = ring_tail;    // discard anything left from a previous stream
    priming = true;
    underrun = false;
    streaming = true;
  }
  return true;
}

// Workaround for TinyUSB 0.18 on the RP2040/RP2350. With iso endpoint
// allocation (TUP_DCD_EDPT_ISO_ALLOC), selecting alternate setting 0 leaves
// the audio OUT and feedback IN endpoints armed in hardware. When the host
// next selects alternate setting 1 and TinyUSB arms them again, it finds
// them already available and panics ("ep 01 was already available"). The
// host always does that: alt 1, alt 0 while probing, then alt 1 to play.
// So abort both in hardware and clear their buffer control when the
// stream closes.
static void reset_iso_endpoint(uint8_t ep_addr) {
  uint8_t num = ep_addr & 0x0f;
  bool in = ep_addr & 0x80;
  uint32_t bit = 1u << (2 * num + (in ? 0 : 1));   // abort and buf_status layout

  uint32_t irq = save_and_disable_interrupts();
  hw_set_bits(&usb_hw->abort, bit);
  while (!(usb_hw->abort_done & bit)) {}
  if (in) {
    usb_dpram->ep_buf_ctrl[num].in = 0;
  } else {
    usb_dpram->ep_buf_ctrl[num].out = 0;
  }
  usb_hw->buf_status = bit;                         // write 1 to clear
  hw_clear_bits(&usb_hw->abort, bit);
  usb_hw->abort_done = bit;                         // write 1 to clear
  restore_interrupts(irq);
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  hang_stage(STAGE_CB_SET_ITF_CLOSE);
  printf("audio: closing interface %u endpoints\n", tu_u16_low(tu_le16toh(p_request->wIndex)));
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) {
    streaming = false;
    reset_iso_endpoint(EPNUM_AUDIO_OUT);
    reset_iso_endpoint(EPNUM_AUDIO_FB);
  }
  return true;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
  (void) func_id;
  (void) alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
  feedback_param->sample_freq = AUDIO_SAMPLE_RATE;
}

void tud_umount_cb(void) {
  streaming = false;
}
