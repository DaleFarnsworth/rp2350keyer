// Pico 2 + TLV320DAC3100: USB sound card and Vail-compatible USB MIDI keyer.

#include <stdio.h>

#include "adapter.h"
#include "audio.h"
#include "config.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "settings.h"
#include "tusb.h"

// Debounced, active-low input. Same behaviour as the Vail adapter's Bounce2
// default mode: a change is accepted once the pin has been stable for
// DEBOUNCE_MS.
class Bounce {
    uint pin = 0;
    bool state = true;          // debounced level (true = released)
    bool unstable = true;       // last raw level
    uint32_t changed_ms = 0;

public:
    void attach(uint p) {
        pin = p;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        sleep_us(10);
        state = unstable = gpio_get(pin);
    }

    // Returns true when the debounced state changes.
    bool update(uint32_t now_ms) {
        bool raw = gpio_get(pin);
        if (raw != unstable) {
            unstable = raw;
            changed_ms = now_ms;
        }
        if (unstable != state && now_ms - changed_ms >= DEBOUNCE_MS) {
            state = unstable;
            return true;
        }
        return false;
    }

    bool pressed() const { return !state; }
};

static VailAdapter adapter;
static Bounce dit, dah, key;

static uint32_t millis() {
    return to_ms_since_boot(get_absolute_time());
}

static void midi_task() {
    uint8_t packet[4];
    while (tud_midi_available()) {
        if (!tud_midi_packet_read(packet)) break;
        MidiPacket event = { packet[0], packet[1], packet[2], packet[3] };
        if (event.header) {
            adapter.HandleMIDI(event);
        }
    }
}

static void led_task(uint32_t now) {
    bool on;
    if (!tud_mounted()) {
        on = (now % 500) < 250;                  // fast blink: not enumerated
    } else if (adapter.TxActive()) {
        on = false;                              // dark while key is down
    } else {
        on = true;
    }
    gpio_put(LED_PIN, on);
}

int main() {
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();
    printf("\nPico 2 Vail Audio starting\n");

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    dit.attach(DIT_PIN);
    dah.attach(DAH_PIN);
    key.attach(KEY_PIN);

    vail_settings_t s;
    bool stored = settings_load(&s);
    adapter.Begin(s.keyer_type, s.dit_duration_ms, s.tx_note, s.tx_freq_hz);
    printf("Settings %s - keyer: %u, dit duration: %u ms, TX note: %u, TX freq: %u Hz\n",
           stored ? "loaded" : "defaulted", s.keyer_type, s.dit_duration_ms, s.tx_note,
           s.tx_freq_hz);

    audio_init();

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    while (true) {
        uint32_t now = millis();

        tud_task();
        audio_task();
        midi_task();

        adapter.Tick(now);

        if (key.update(now)) {
            adapter.ProcessPaddleInput(PADDLE_STRAIGHT, key.pressed());
        }
        if (dit.update(now)) {
            adapter.ProcessPaddleInput(PADDLE_DIT, dit.pressed());
        }
        if (dah.update(now)) {
            adapter.ProcessPaddleInput(PADDLE_DAH, dah.pressed());
        }

        settings_task(now);
        led_task(now);
    }
}
