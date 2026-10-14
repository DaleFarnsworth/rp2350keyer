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

  for (uint32_t i = 0; i < frames; i++) {
    uint32_t f = 0;
    if ((int32_t) (head - tail) > 0) {
      f = ring[tail & RING_MASK];
      tail++;
    } else if (streaming && !priming) {
      underrun = true;
    }
    int32_t l = ((int32_t) (int16_t) (f & 0xffff) * gl) >> 15;
    int32_t r = ((int32_t) (int16_t) (f >> 16) * gr) >> 15;
    int32_t st = sidetone_next_sample();
    buf[i] = ((uint32_t) (uint16_t) clip16(r + st) << 16) | (uint16_t) clip16(l + st);
  }
  ring_tail = tail;
}

void audio_init(void) {
  sidetone_init();
  i2s_out_init(I2S_DATA_PIN, I2S_BCLK_PIN, AUDIO_SAMPLE_RATE, fill_i2s);
  dac3100_init();
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
    if (mute[0] || mute[ch + 1]) {
      gain_q15[ch] = 0;
    } else {
      float db = (float) (volume[0] + volume[ch + 1]) / 256.0f;
      gain_q15[ch] = (int32_t) lrintf(32768.0f * powf(10.0f, db / 20.0f));
    }
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
    update_gains();
    return true;
  } else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
    int16_t v = ((audio_control_cur_2_t const *) buf)->bCur;
    if (v < VOLUME_MIN) v = VOLUME_MIN;
    if (v > VOLUME_MAX) v = VOLUME_MAX;
    volume[request->bChannelNumber] = v;
    update_gains();
    return true;
  }
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK) return clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return feature_unit_get_request(rhport, request);
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void) rhport;
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return feature_unit_set_request(request, buf);
  return false;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  if (itf == ITF_NUM_AUDIO_STREAMING && alt != 0) {
    ring_head = ring_tail;    // discard anything left from a previous stream
    priming = true;
    underrun = false;
    streaming = true;
  }
  return true;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) {
    streaming = false;
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
