# RP2350-Zero Vail Audio

Firmware for a Waveshare RP2350-Zero with an Adafruit TLV320DAC3100 I2S DAC.
It shows up over USB as a single composite device with three functions:

- **USB sound card**: a UAC2 stereo speaker, 48 kHz, 16-bit, with an
  asynchronous feedback endpoint and host volume/mute. No driver is
  needed on Linux, macOS or Windows 10+.
- **USB MIDI**: a class-compliant MIDI port that speaks the
  [Vail adapter](https://github.com/Vail-CW/vail-adapter) protocol, so
  [Vail](https://vail.woozle.org/) can drive it like a Vail adapter. The
  Vail keyers are included, and the sidetone is mixed into the DAC output.
- **USB serial port**: debug messages (`/dev/ttyACM0` on Linux). Opening it
  at 1200 baud restarts the Zero in its USB bootloader.

## Wiring

Pad numbers are from the RP2350-Zero's pinout.

| RP2350-Zero | Pad | TLV320DAC3100 breakout | Notes |
|---|---|---|---|
| 3V3 | 3 | VIN | |
| GND | 2 | GND | |
| GP4 | 19 | SDA | I2C0 |
| GP5 | 18 | SCL | I2C0 |
| GP7 | 16 | RST | active low |
| GP9 | 14 | DIN | I2S data |
| GP10 | 13 | BCK | I2S bit clock |
| GP11 | 12 | WSEL | I2S word select (must be BCK + 1) |
| — | | MCLK | leave unconnected; the DAC PLL runs from BCK |

| RP2350-Zero | Pad | Key |
|---|---|---|
| GP2 | 21 | dit paddle (tip) |
| GP1 | 22 | dah paddle (ring) |
| GP0 | 23 | straight key |
| GND | 2 | common (sleeve) |

The Zero has only one GND pad, so join the DAC's and the keys' grounds
before they reach it. The key inputs are active low with internal pull-ups. Headphones go in the
breakout's jack. The class-D speaker output is off; set
`DAC_ENABLE_SPEAKER` to 1 in `src/config.h` to turn it on.
All pins and levels are in [src/config.h](src/config.h).

These are the Vail adapter's RP2350-Zero pins, the same as the wiring in
the keyer case's `rp2350-zero` branch.

Debug messages go to the USB serial port, and also to UART0 (GP12 TX, pad
11, 115200 baud). The UART is moved from the board's usual GP0 and GP1,
which are key inputs here. The USB port keeps the last 4 KB of messages, so
the ones from startup are still there when you open it, for example with:

```bash
picocom /dev/ttyACM0
```

The Zero's RGB LED (a WS2812 on GP16) is the status light: it blinks green
until the computer has set up the USB device, then stays dim green, going
dark while the key is down.

## Building

You need the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.x
(with its TinyUSB submodule) and an Arm GCC toolchain.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -G Ninja
ninja -C build
```

Hold the Zero's BOOT button while plugging it in, then copy
`build/rp2350_zero_vail_audio.uf2` to the `RP2350` drive. Once this
firmware is running, you can skip the button: open its serial port at
1200 baud (`stty -F /dev/ttyACM0 1200`, or an Arduino-style upload) and
the drive appears.

## MIDI protocol

The device uses the Vail adapter's MIDI protocol on channel 1, plus two
extra controllers (CC3 and CC4) for the sidetone. Values are hex; `vv`, `nn`
and `kk` are 00–7F.

| Message | Meaning | Default | Saved | Source |
|---|---|---|---|---|
| `B0 00 vv` | Mode: `vv` > 3F is keyboard mode, otherwise MIDI mode. Echoed back. There is no HID keyboard, so the device sends MIDI either way. | keyboard | no | Vail |
| `B0 01 vv` | Dit duration = `vv` × 2 ms (`00` gives 2 ms) | `32` (100 ms) | yes | Vail |
| `B0 02 nn` | Sidetone note number (`45` = A4 = 440 Hz). Clears a CC3 pitch. | `45` | yes | Vail |
| `B0 03 vv` | Sidetone pitch = `vv` × 10 Hz (10–1270 Hz). `00` goes back to the CC2 note. | `00` (use CC2) | yes | added |
| `B0 04 vv` | Sidetone volume: `7F` is 0 dBFS, each step below is −0.5 dB, `00` is off. Applies to keyed and MIDI-note sidetone. | `67` (−12 dBFS) | yes | added |
| `C0 kk` | Keyer (see below) | `01` | yes | Vail |
| `90 nn vv` | Play note `nn` on the sidetone. Velocity `00` is note off. | | | Vail |
| `80 nn vv` | Stop the MIDI sidetone note | | | Vail |

Keyers selected by `C0 kk`; anything above `09` selects passthrough:

| `kk` | Keyer |
|---|---|
| `00` | Passthrough (paddles sent as notes 1 and 2) |
| `01` | Straight key |
| `02` | Bug |
| `03` | Electric bug |
| `04` | Single dot |
| `05` | Ultimatic |
| `06` | Plain iambic |
| `07` | Iambic A |
| `08` | Iambic B |
| `09` | Keyahead |

Whichever of CC2 and CC3 arrived last sets the keying sidetone pitch.
Saved settings are written to flash 2 s after the last change.

The device sends:

| Event | MIDI |
|---|---|
| Straight key down / up | `90 00 7F` / `80 00 00` |
| Keyed dit on / off | `90 01 7F` / `80 01 00` |
| Keyed dah on / off | `90 02 7F` / `80 02 00` |

In passthrough mode the dit and dah paddles are sent directly as notes 1 and
2, so the host can do the keying.

## Sidetone

As in the Vail adapter's PolyBuzzer, there are two priority slots. Local
keying (slot 0) overrides MIDI notes from the host (slot 1). The tone is a
sine with a 5 ms rise and fall (`SIDETONE_RAMP_MS`) to avoid key clicks.
Its level is set with CC4 and defaults to -12 dBFS
(`DEFAULT_SIDETONE_VOLUME`). It is added to the USB audio after the host
volume is applied, so the computer's volume control does not change the
sidetone level.

While the sidetone sounds, from local keying or a MIDI note, the audio
from the computer is muted. It fades out and back in over the same 5 ms,
so it doesn't click, and comes back as soon as each tone has faded out.
`RX_MUTE_HANG_MS` keeps it muted longer after each tone, for example to
stop it returning in the gaps between elements, and
`RX_MUTE_DURING_SIDETONE` set to 0 turns the muting off.

## How it differs from the Vail adapter

- No HID keyboard, piezo buzzer, radio output, CW memories, button menu,
  capacitive touch or TRS auto-detection.
- Passthrough mode sends only notes 1 and 2. The original also sent a
  note-0 key-down for every paddle press.
- Passthrough (keyer 0) is saved to flash. The original reported it as
  keyer 1, so a CC1/CC2 change in passthrough could save it as straight key.
- `B0 01 00` gives a 2 ms dit instead of 0 ms.
- `B0 03 vv` is an extra command for setting the sidetone pitch in Hz.
- `B0 04 vv` is an extra command for setting the sidetone volume.
- Settings are saved in the last 4 KB flash sector, 2 s after the last
  change. The erase holds off interrupts for about 50 ms, so audio glitches
  once each time a setting is saved.

## Source layout

| File | Purpose |
|---|---|
| `src/main.cpp` | Main loop, debounced key inputs, LED |
| `src/status_led.*`, `src/ws2812.pio` | PIO driver for the WS2812 status LED |
| `src/adapter.*` | Vail MIDI handling and keying output (port of `adapter.cpp`) |
| `src/keyers.*` | Vail keyers (port of `keyers.cpp`) |
| `src/sidetone.*` | Sine sidetone with priority slots (replaces PolyBuzzer) |
| `src/audio.*` | UAC2 callbacks, volume, USB→I2S buffering, mixing |
| `src/i2s_out.*` | PIO + DMA I2S transmitter |
| `src/tlv320dac3100.*` | DAC register setup (PLL from BCLK) |
| `src/settings.*` | Flash-backed settings |
| `src/usb_descriptors.*` | Composite UAC2 + MIDI + CDC descriptors |
| `src/usb_debug.*` | Debug output over the USB serial port, 1200-baud bootloader entry |
| `src/quirk_os_guessing.*` | From TinyUSB: picks the feedback format per host OS |

The clock runs at 153.6 MHz, so the I2S PIO divider is exactly 50 and the
output is exactly 48 kHz.

## License

MIT License; see [LICENSE](LICENSE).

The keyer code and MIDI protocol come from the Vail adapter (MIT License,
Copyright (c) 2020 Neale Pickett). The I2S PIO program is adapted from
pico-extras and the WS2812 program comes from pico-examples (both
BSD-3-Clause), and the UAC2 descriptors and OS-guessing quirk are adapted
from TinyUSB (MIT). Their notices are in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
