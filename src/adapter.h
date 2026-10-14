// Vail adapter MIDI behaviour, ported from vail-adapter's adapter.h/.cpp
// (https://github.com/Vail-CW/vail-adapter, MIT License).
//
// Kept: the MIDI protocol (CC0 mode, CC1 dit duration, CC2 sidetone note,
// program change keyer select, note on/off sidetone), the keyers, and the
// key-down MIDI notes (0 straight key, 1 dit, 2 dah).
// Added: CC3 sets the sidetone pitch in 10 Hz steps, CC4 the sidetone volume.
// Dropped: HID keyboard output, the piezo buzzer, radio output, CW memories
// and the button menu. Sidetone goes to the TLV320DAC3100 instead.

#pragma once

#include <stdint.h>
#include "keyers.h"

struct MidiPacket {
    uint8_t header;   // USB-MIDI cable number / code index
    uint8_t byte1;
    uint8_t byte2;
    uint8_t byte3;
};

class VailAdapter: public Transmitter {
private:
    unsigned int txNote;
    unsigned int txFreqHz = 0;  // CC3 pitch; 0 means use txNote
    unsigned int sidetoneVolume;  // CC4
    unsigned int ditDuration;
    bool keyboardMode = true;   // tracked and echoed; this device always sends MIDI
    Keyer *keyer = nullptr;
    bool keyIsPressed = false;

    // Paddle state for passthrough mode
    bool passthroughPressed[2] = {false, false};

    void midiKey(uint8_t key, bool down);
    void sendMIDI(const MidiPacket &event);
    void settingsChanged(uint8_t keyerType);
    void startTxTone();

public:
    VailAdapter();

    // Restore saved settings (as if received by MIDI).
    void Begin(uint8_t keyerType, uint16_t ditDurationMs, uint8_t note, uint16_t freqHz,
               uint8_t volume);

    void ProcessPaddleInput(Paddle paddle, bool pressed);
    void HandleMIDI(const MidiPacket &event);

    void BeginTx() override;
    void EndTx() override;
    void BeginTx(int relay) override;
    void EndTx(int relay) override;

    void Tick(unsigned long millis);

    bool KeyboardMode() const { return keyboardMode; }
    bool TxActive() const { return keyIsPressed; }
    uint8_t getCurrentKeyerType() const;
    uint16_t getDitDuration() const { return ditDuration; }
    uint8_t getTxNote() const { return txNote; }
    uint16_t getTxFreqHz() const { return txFreqHz; }
    uint8_t getSidetoneVolume() const { return sidetoneVolume; }

    void ReleaseAllKeys();
};
