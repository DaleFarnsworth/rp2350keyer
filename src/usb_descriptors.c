#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "quirk_os_guessing.h"

// pid.codes test PID; replace with your own VID/PID before distributing.
#define USB_VID   0x1209
#define USB_PID   0x0001

//--------------------------------------------------------------------+
// Device descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0201,   // 2.01 so the host asks for the BOS descriptor

  // Interface Association Descriptors are used for both functions
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  quirk_os_guessing_desc_device_cb();
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration descriptor
//--------------------------------------------------------------------+
#define EPNUM_AUDIO_OUT   0x01
#define EPNUM_AUDIO_FB    0x81
#define EPNUM_MIDI_OUT    0x02
#define EPNUM_MIDI_IN     0x82

#define TUD_MIDI_IAD_LEN  8
// Groups the MIDI AudioControl and MIDIStreaming interfaces into a single
// function, so hosts don't split them in this IAD-class composite device.
#define TUD_MIDI_IAD(_firstitf) \
  TUD_MIDI_IAD_LEN, TUSB_DESC_INTERFACE_ASSOCIATION, _firstitf, 2, \
  TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_CONTROL, AUDIO_FUNC_PROTOCOL_CODE_UNDEF, 0

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN \
                           + TUD_MIDI_IAD_LEN + TUD_MIDI_DESC_LEN)

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_AUDIO,
  STRID_MIDI,
};

#define CONFIG_DESCRIPTOR(_fbsize) \
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100), \
  TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STRID_AUDIO, \
      CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX, \
      EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX, EPNUM_AUDIO_FB, _fbsize), \
  TUD_MIDI_IAD(ITF_NUM_MIDI), \
  TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STRID_MIDI, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)

// Windows (the default): 4-byte 16.16 feedback on full speed
static uint8_t const desc_configuration_default[] = { CONFIG_DESCRIPTOR(4) };
// macOS: 3-byte 10.14 feedback on full speed, as the spec says
static uint8_t const desc_configuration_osx_fs[] = { CONFIG_DESCRIPTOR(3) };

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  quirk_os_guessing_desc_configuration_cb();
  if (quirk_os_guessing_get() == QUIRK_OS_GUESSING_OSX) {
    return desc_configuration_osx_fs;
  }
  return desc_configuration_default;
}

bool tud_audio_feedback_format_correction_cb(uint8_t func_id) {
  (void) func_id;
  return quirk_os_guessing_get() == QUIRK_OS_GUESSING_OSX;
}

//--------------------------------------------------------------------+
// BOS descriptor (needed by the OS guessing quirk)
//--------------------------------------------------------------------+
#define TUD_BOS_USB20_EXT_DESC_LEN 7
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_USB20_EXT_DESC_LEN)

static uint8_t const desc_bos[] = {
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
  // USB 2.0 Extension Descriptor
  0x07, TUSB_DESC_DEVICE_CAPABILITY, DEVICE_CAPABILITY_USB20_EXTENSION, 0x00, 0x00, 0x00, 0x00
};

uint8_t const *tud_descriptor_bos_cb(void) {
  quirk_os_guessing_desc_bos_cb();
  return desc_bos;
}

//--------------------------------------------------------------------+
// String descriptors
//--------------------------------------------------------------------+
static char const *string_desc_arr[] = {
  [STRID_LANGID]       = (const char[]) { 0x09, 0x04 },  // English
  [STRID_MANUFACTURER] = "Open Source Hardware",
  [STRID_PRODUCT]      = "Pico 2 Vail Audio",
  [STRID_SERIAL]       = NULL,                            // from flash unique ID
  [STRID_AUDIO]        = "Vail Audio",
  [STRID_MIDI]         = "Vail Adapter",
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  quirk_os_guessing_desc_string_cb();

  switch (index) {
    case STRID_LANGID:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL: {
      char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
      pico_get_unique_board_id_string(serial, sizeof(serial));
      chr_count = strlen(serial);
      for (size_t i = 0; i < chr_count; i++) {
        _desc_str[1 + i] = serial[i];
      }
      break;
    }

    default: {
      if (index >= TU_ARRAY_SIZE(string_desc_arr)) return NULL;
      const char *str = string_desc_arr[index];
      chr_count = strlen(str);
      size_t const max_count = TU_ARRAY_SIZE(_desc_str) - 1;
      if (chr_count > max_count) chr_count = max_count;
      for (size_t i = 0; i < chr_count; i++) {
        _desc_str[1 + i] = str[i];
      }
      break;
    }
  }

  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
