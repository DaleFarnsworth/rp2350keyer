// Keyer implementations ported from the Vail adapter
// (https://github.com/Vail-CW/vail-adapter, MIT License,
// Copyright (c) 2020 Neale Pickett).
//
// The logic is unchanged; Arduino and Serial dependencies are removed.

#include <stddef.h>
#include "keyers.h"

#define len(t) (sizeof(t)/sizeof(*t))

// Queue Set: A Set you can shift.
class QSet {
    int arr[MAX_KEYER_QUEUE];
    unsigned int arrlen = 0;

public:
    int shift() {
        if (arrlen == 0) {
            return -1;
        }
        int ret = arr[0];
        arrlen--;
        for (unsigned int i = 0; i < arrlen; i++) {
            arr[i] = arr[i+1];
        }
        return ret;
    }

    void add(int val) {
        if (arrlen == MAX_KEYER_QUEUE) {
            return;
        }
        for (unsigned int i = 0; i < arrlen; i++) {
            if (arr[i] == val) {
                return;
            }
        }
        arr[arrlen] = val;
        arrlen++;
    }
};


class StraightKeyer: public Keyer {
public:
    Transmitter *output = NULL;
    unsigned int ditDuration = 100;
    bool txRelays[2] = {false, false};
    int currentTransmittingRelay = -1; // Track what we're currently transmitting

    StraightKeyer() {
        this->Reset();
    }

    void SetOutput(Transmitter *output) override {
        this->output = output;
    }

    void Reset() override {
        if (this->output) {
            this->output->EndTx();
        }
        this->ditDuration = 100;
    }

    void SetDitDuration(unsigned int duration) override {
        this->ditDuration = duration;
    }

    void Release() override {
        this->Reset();
    }

    bool TxClosed() override {
        for (unsigned int i = 0; i < len(this->txRelays); i++) {
            if (this->TxClosed(i)) {
                return true;
            }
        }
        return false;
    }

    bool TxClosed(int relay) override {
        return this->txRelays[relay];
    }

    void Tx(int relay, bool closed) override {
        bool wasClosed = this->TxClosed();
        this->txRelays[relay] = closed;
        bool nowClosed = this->TxClosed();

        if (wasClosed != nowClosed) {
            if (nowClosed) {
                this->currentTransmittingRelay = relay;
                this->output->BeginTx(relay);
            } else {
                this->currentTransmittingRelay = -1;
                this->output->EndTx(relay);
            }
        }
    }

    void Key(Paddle key, bool pressed) override {
        this->Tx(key, pressed);
    }

    void Tick(unsigned long millis) override { (void)millis; }
};

class BugKeyer: public StraightKeyer {
public:
    unsigned long nextPulse = 0;
    bool keyPressed[2] = {false, false};

    using StraightKeyer::StraightKeyer;

    void Reset() override {
        StraightKeyer::Reset();
        this->nextPulse = 0;
        this->keyPressed[0] = false;
        this->keyPressed[1] = false;
    }

    void Key(Paddle key, bool pressed) override {
        this->keyPressed[key] = pressed;
        if (key == 0) {
            this->beginPulsing();
        } else {
            StraightKeyer::Key(key, pressed);
        }
    }

    void Tick(unsigned long millis) override {
        if (this->nextPulse && (millis >= this->nextPulse)) {
            this->pulse(millis);
        }
    }

    void beginPulsing() {
        if (!this->nextPulse) {
            this->nextPulse = 1;
        }
    }

    virtual void pulse(unsigned long millis) {
        if (this->TxClosed(0)) {
            this->Tx(0, false);
        } else if (this->keyPressed[0]) {
            this->Tx(0, true);
        } else {
            this->nextPulse = 0;
            return;
        }
        this->nextPulse = millis + this->ditDuration;
    }
};

class ElBugKeyer: public BugKeyer {
public:
    int nextRepeat = -1;
    int currentTransmittingElement = -1; // Track what element we're transmitting

    using BugKeyer::BugKeyer;

    void Reset() override {
        BugKeyer::Reset();
        this->nextRepeat = -1;
        this->currentTransmittingElement = -1;
    }

    // Return which key is pressed. If none, return -1.
    int whichKeyPressed() {
        for (unsigned int i = 0; i < len(this->keyPressed); i++) {
            if (this->keyPressed[i]) {
                return i;
            }
        }
        return -1;
    }

    void Key(Paddle key, bool pressed) override {
        this->keyPressed[key] = pressed;
        if (pressed) {
            this->nextRepeat = key;
            this->beginPulsing();
        } else {
            this->nextRepeat = this->whichKeyPressed();
        }
    }

    unsigned int keyDuration(int key) {
        switch (key) {
        case PADDLE_DIT:
            return this->ditDuration;
        case PADDLE_DAH:
            return 3 * (this->ditDuration);
        }
        return this->ditDuration; // XXX
    }

    virtual int nextTx() {
        if (this->whichKeyPressed() == -1) {
            return -1;
        }
        return this->nextRepeat;
    }

    void pulse(unsigned long millis) override {
        int nextPulse = 0;
        if (this->currentTransmittingElement >= 0) {
            // Pause if we're currently transmitting - end current element
            nextPulse = this->keyDuration(PADDLE_DIT);
            this->Tx(this->currentTransmittingElement, false);
            this->currentTransmittingElement = -1;
        } else {
            int next = this->nextTx();
            if (next >= 0) {
                nextPulse = this->keyDuration(next);
                // Use the correct relay (next) and track it
                this->currentTransmittingElement = next;
                this->Tx(next, true);
            }
        }

        if (nextPulse) {
            this->nextPulse = millis + nextPulse;
        } else {
            this->nextPulse = 0;
        }
    }
};

class UltimaticKeyer: public ElBugKeyer {
public:
    QSet queue;

    using ElBugKeyer::ElBugKeyer;

    void Key(Paddle key, bool pressed) override {
        if (pressed) {
            this->queue.add(key);
        }
        ElBugKeyer::Key(key, pressed);
    }

    int nextTx() override {
        int key = this->queue.shift();
        if (key != -1) {
            return key;
        }
        return ElBugKeyer::nextTx();
    }
};

class SingleDotKeyer: public ElBugKeyer {
public:
    QSet queue;

    using ElBugKeyer::ElBugKeyer;

    void Key(Paddle key, bool pressed) override {
        if (pressed && (key == PADDLE_DIT)) {
            this->queue.add(key);
        }
        ElBugKeyer::Key(key, pressed);
    }

    int nextTx() override {
        int key = this->queue.shift();
        if (key != -1) {
            return key;
        }
        if (this->keyPressed[1]) return 1;
        if (this->keyPressed[0]) return 0;
        return -1;
    }
};

class IambicKeyer: public ElBugKeyer {
public:

    using ElBugKeyer::ElBugKeyer;

    int nextTx() override {
        int next = ElBugKeyer::nextTx();
        if (this->keyPressed[PADDLE_DIT] && this->keyPressed[PADDLE_DAH]) {
            this->nextRepeat = 1 - this->nextRepeat;
        }
        return next;
    }
};

class IambicAKeyer: public IambicKeyer {
public:
    QSet queue;

    using IambicKeyer::IambicKeyer;

    void Key(Paddle key, bool pressed) override {
        if (pressed && (key == PADDLE_DIT)) {
            this->queue.add(key);
        }
        IambicKeyer::Key(key, pressed);
    }

    int nextTx() override {
        int next = IambicKeyer::nextTx();
        int key = this->queue.shift();
        if (key != -1) {
            return key;
        }
        return next;
    }
};

class IambicBKeyer: public IambicKeyer {
public:
    QSet queue;

    using IambicKeyer::IambicKeyer;

    void Key(Paddle key, bool pressed) override {
        if (pressed) {
            this->queue.add(key);
        }
        IambicKeyer::Key(key, pressed);
    }

    int nextTx() override {
        for (unsigned int key = 0; key < len(this->keyPressed); key++) {
            if (this->keyPressed[key]) {
                this->queue.add(key);
            }
        }

        return this->queue.shift();
    }
};

class KeyaheadKeyer: public ElBugKeyer {
public:
    int queue[MAX_KEYER_QUEUE];
    unsigned int qlen = 0;

    using ElBugKeyer::ElBugKeyer;

    void Reset() override {
        ElBugKeyer::Reset();
        this->qlen = 0;
    }

    void Key(Paddle key, bool pressed) override {
        if (pressed) {
            if (this->qlen < MAX_KEYER_QUEUE) {
                this->queue[this->qlen++] = key;
            }
        }
        ElBugKeyer::Key(key, pressed);
    }

    int nextTx() override {
        if (this->qlen > 0) {
            int next = this->queue[0];
            this->qlen--;
            for (unsigned int i = 0; i < this->qlen; i++) {
                this->queue[i] = this->queue[i+1];
            }
            return next;
        }
        return ElBugKeyer::nextTx();
    }
};

static StraightKeyer straightKeyer;
static BugKeyer bugKeyer;
static ElBugKeyer elBugKeyer;
static SingleDotKeyer singleDotKeyer;
static UltimaticKeyer ultimaticKeyer;
static IambicKeyer iambicKeyer;
static IambicAKeyer iambicAKeyer;
static IambicBKeyer iambicBKeyer;
static KeyaheadKeyer keyaheadKeyer;

static Keyer *keyers[] = {
    NULL,
    &straightKeyer,
    &bugKeyer,
    &elBugKeyer,
    &singleDotKeyer,
    &ultimaticKeyer,
    &iambicKeyer,
    &iambicAKeyer,
    &iambicBKeyer,
    &keyaheadKeyer,
};

Keyer *GetKeyerByNumber(int n, Transmitter *output) {
    if (n < 0 || (unsigned int)n >= len(keyers)) {
        return NULL;
    }

    Keyer *k = keyers[n];
    if (k) {
        k->SetOutput(output);
    }
    return k;
}

int getKeyerNumber(Keyer *k) {
    if (k == NULL) {
        return 0; // passthrough
    }

    for (unsigned int i = 1; i < len(keyers); i++) {
        if (keyers[i] == k) {
            return i;
        }
    }
    return 1; // Default to straight key if not found
}
