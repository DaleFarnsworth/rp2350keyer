# Pico 2 Vail Audio

Firmware for a Raspberry Pi Pico 2 with an Adafruit TLV320DAC3100 I2S DAC.
It shows up over USB as a single composite device with two functions:

- **USB sound card**: a UAC2 stereo speaker, 48 kHz, 16-bit, with an
  asynchronous feedback endpoint and host volume/mute. No driver is
  needed on Linux, macOS or Windows 10+.
- **USB MIDI**: a class-compliant MIDI port that speaks the
  [Vail adapter](https://github.com/Vail-CW/vail-adapter) protocol, so
  [Vail](https://vail.woozle.org/) can drive it like a Vail adapter. The
  Vail keyers are included, and the sidetone is mixed into the DAC output.

## Wiring

| Pico 2 pin | TLV320DAC3100 breakout | Notes |
|---|---|---|
| 3V3 (pin 36) | VIN | |
| GND | GND | |
| GP4 | SDA | I2C0 |
| GP5 | SCL | I2C0 |
| GP7 | RST | active low |
| GP9 | DIN | I2S data |
| GP10 | BCK | I2S bit clock |
| GP11 | WSEL | I2S word select (must be BCK + 1) |
| — | MCLK | leave unconnected; the DAC PLL runs from BCK |

| Pico 2 pin | Key |
|---|---|
| GP2 | dit paddle (tip) |
| GP3 | dah paddle (ring) |
| GP6 | straight key |
| GND | common (sleeve) |

The key inputs are active low with internal pull-ups. Headphones go in the
breakout's jack. The class-D speaker output is off; set
`DAC_ENABLE_SPEAKER` to 1 in `src/config.h` to turn it on.
All pins and levels are in [src/config.h](src/config.h).

UART0 (GP0 TX, 115200 baud) carries debug messages.

## Building

You need the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.x
(with its TinyUSB submodule) and an Arm GCC toolchain.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -G Ninja
ninja -C build
```

Hold BOOTSEL while plugging in the Pico 2, then copy
`build/pico2_vail_audio.uf2` to the `RP2350` drive.

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
| `src/adapter.*` | Vail MIDI handling and keying output (port of `adapter.cpp`) |
| `src/keyers.*` | Vail keyers (port of `keyers.cpp`) |
| `src/sidetone.*` | Sine sidetone with priority slots (replaces PolyBuzzer) |
| `src/audio.*` | UAC2 callbacks, volume, USB→I2S buffering, mixing |
| `src/i2s_out.*` | PIO + DMA I2S transmitter |
| `src/tlv320dac3100.*` | DAC register setup (PLL from BCLK) |
| `src/settings.*` | Flash-backed settings |
| `src/usb_descriptors.*` | Composite UAC2 + MIDI descriptors |
| `src/quirk_os_guessing.*` | From TinyUSB: picks the feedback format per host OS |

The clock runs at 153.6 MHz, so the I2S PIO divider is exactly 50 and the
output is exactly 48 kHz.

## License

MIT License; see [LICENSE](LICENSE).

The keyer code and MIDI protocol come from the Vail adapter (MIT License,
Copyright (c) 2020 Neale Pickett). The I2S PIO program is adapted from
pico-extras (BSD-3-Clause), and the UAC2 descriptors and OS-guessing quirk
are adapted from TinyUSB (MIT). Their notices are in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
