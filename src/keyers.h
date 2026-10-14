// Keyer implementations ported from the Vail adapter
// (https://github.com/Vail-CW/vail-adapter, MIT License,
// Copyright (c) 2020 Neale Pickett).

#pragma once

#define MAX_KEYER_QUEUE 5

typedef enum {
    PADDLE_DIT = 0,
    PADDLE_DAH = 1,
    PADDLE_STRAIGHT,
} Paddle;

class Transmitter {
public:
    virtual void BeginTx() = 0;
    virtual void EndTx() = 0;
    virtual void BeginTx(int relay) = 0;
    virtual void EndTx(int relay) = 0;
};

class Keyer {
public:
    virtual void SetOutput(Transmitter *output) = 0;
    virtual void Reset() = 0;
    virtual void SetDitDuration(unsigned int d) = 0;
    virtual void Release() = 0;
    virtual bool TxClosed() = 0;
    virtual bool TxClosed(int relay) = 0;
    virtual void Tx(int relay, bool closed) = 0;
    virtual void Key(Paddle key, bool pressed) = 0;
    virtual void Tick(unsigned long millis) = 0;
};

// Keyer numbers match MIDI program change values:
// 0 passthrough (NULL), 1 straight, 2 bug, 3 electric bug, 4 single dot,
// 5 ultimatic, 6 plain iambic, 7 iambic A, 8 iambic B, 9 keyahead.
Keyer *GetKeyerByNumber(int n, Transmitter *output);
int getKeyerNumber(Keyer *k);
