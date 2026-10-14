// Vail adapter MIDI behaviour, ported from vail-adapter's adapter.cpp
// (https://github.com/Vail-CW/vail-adapter, MIT License).

#include "adapter.h"

#include <stdio.h>

#include "config.h"
#include "settings.h"
#include "sidetone.h"
#include "tusb.h"

// Sidetone slots, as used by the Vail adapter's PolyBuzzer
#define SLOT_TX   0   // local keying (higher priority)
#define SLOT_MIDI 1   // MIDI note on/off from the host

VailAdapter::VailAdapter() {
    this->txNote = DEFAULT_TONE_NOTE;
    this->ditDuration = DEFAULT_ADAPTER_DIT_DURATION_MS;
}

void VailAdapter::Begin(uint8_t keyerType, uint16_t ditDurationMs, uint8_t note) {
    this->ditDuration = ditDurationMs;
    this->txNote = note;
    this->keyer = GetKeyerByNumber(keyerType, this);
    if (this->keyer) {
        this->keyer->SetDitDuration(this->ditDuration);
    }
}

uint8_t VailAdapter::getCurrentKeyerType() const {
    return getKeyerNumber(this->keyer);
}

void VailAdapter::sendMIDI(const MidiPacket &event) {
    if (!tud_mounted()) return;
    uint8_t packet[4] = { event.header, event.byte1, event.byte2, event.byte3 };
    tud_midi_packet_write(packet);
}

void VailAdapter::midiKey(uint8_t key, bool down) {
    MidiPacket event;
    if (down) {
        event = { 0x09, 0x90, key, 0x7F };  // Note On, channel 1
    } else {
        event = { 0x08, 0x80, key, 0x00 };  // Note Off, channel 1
    }
    this->sendMIDI(event);
}

void VailAdapter::ReleaseAllKeys() {
    this->midiKey(0, false);
    this->midiKey(1, false);
    this->midiKey(2, false);
    sidetone_off(SLOT_TX);
    this->keyIsPressed = false;
    this->passthroughPressed[0] = false;
    this->passthroughPressed[1] = false;
}

void VailAdapter::settingsChanged(uint8_t keyerType) {
    vail_settings_t s = {
        .keyer_type = keyerType,
        .dit_duration_ms = (uint16_t) this->ditDuration,
        .tx_note = (uint8_t) this->txNote,
    };
    settings_save_later(&s);
}

// Straight key (no relay information): MIDI note 0
void VailAdapter::BeginTx() {
    this->keyIsPressed = true;
    sidetone_note(SLOT_TX, this->txNote);
    this->midiKey(0, true);
}

void VailAdapter::EndTx() {
    this->keyIsPressed = false;
    sidetone_off(SLOT_TX);
    this->midiKey(0, false);
}

static uint8_t relayNote(int relay) {
    if (relay == PADDLE_DIT) return 1;
    if (relay == PADDLE_DAH) return 2;
    return 0;  // fallback for straight key
}

// Keyer output: MIDI note 1 for dit, 2 for dah
void VailAdapter::BeginTx(int relay) {
    this->keyIsPressed = true;
    sidetone_note(SLOT_TX, this->txNote);
    this->midiKey(relayNote(relay), true);
}

void VailAdapter::EndTx(int relay) {
    this->keyIsPressed = false;
    sidetone_off(SLOT_TX);
    this->midiKey(relayNote(relay), false);
}

void VailAdapter::ProcessPaddleInput(Paddle paddle, bool pressed) {
    if (paddle == PADDLE_STRAIGHT) {
        if (pressed) BeginTx(); else EndTx();
    } else if (this->keyer) {
        this->keyer->Key(paddle, pressed);
    } else {
        // Passthrough: report the paddles as notes 1 and 2 and let the host
        // do the keying. Sidetone sounds while either paddle is closed.
        this->passthroughPressed[paddle] = pressed;
        this->midiKey(relayNote(paddle), pressed);

        bool any = this->passthroughPressed[PADDLE_DIT] || this->passthroughPressed[PADDLE_DAH];
        if (any && !this->keyIsPressed) {
            this->keyIsPressed = true;
            sidetone_note(SLOT_TX, this->txNote);
        } else if (!any && this->keyIsPressed) {
            this->keyIsPressed = false;
            sidetone_off(SLOT_TX);
        }
    }
}

void VailAdapter::HandleMIDI(const MidiPacket &event) {
    switch (event.byte1) {
    case 0xB0:
        switch (event.byte2) {
        case 0:
            this->keyboardMode = (event.byte3 > 0x3f);
            printf("Keyboard mode: %s (this device always sends MIDI)\n",
                   this->keyboardMode ? "ON" : "OFF");
            this->sendMIDI(event);  // echo, as the Vail adapter does
            break;
        case 1: {
            unsigned int d = event.byte3 * 2;
            if (d == 0) d = 2;  // spec range is 2..254 ms
            this->ditDuration = d;
            if (this->keyer) {
                this->keyer->SetDitDuration(this->ditDuration);
            }
            printf("Dit duration set to: %u ms\n", this->ditDuration);
            this->settingsChanged(getCurrentKeyerType());
            break;
        }
        case 2:
            this->txNote = event.byte3;
            printf("TX note set to: %u\n", this->txNote);
            this->settingsChanged(getCurrentKeyerType());
            break;
        }
        break;
    case 0xC0:
        if (this->keyer) {
            this->keyer->Reset();  // Clear any held key state before switching
            this->keyer->Release();
            this->keyer = nullptr;
        }
        ReleaseAllKeys();
        this->keyer = GetKeyerByNumber(event.byte2, this);
        if (this->keyer) {
            this->keyer->SetDitDuration(this->ditDuration);
            printf("Keyer mode set to: %u\n", event.byte2);
        } else {
            printf("Keyer mode set to passthrough (or invalid): %u\n", event.byte2);
        }
        this->settingsChanged(getCurrentKeyerType());
        break;
    case 0x80:
        sidetone_off(SLOT_MIDI);
        break;
    case 0x90:
        if (event.byte3 == 0) {
            sidetone_off(SLOT_MIDI);  // note on with velocity 0 is note off
        } else {
            sidetone_note(SLOT_MIDI, event.byte2);
        }
        break;
    }
}

void VailAdapter::Tick(unsigned long millis) {
    if (this->keyer) {
        this->keyer->Tick(millis);
    }
}
