#include "status_led.h"

#include <stdio.h>

#include "config.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

#define WS2812_BIT_RATE     800000
#define WS2812_CYCLES_PER_BIT (ws2812_T1 + ws2812_T2 + ws2812_T3)

static PIO led_pio;
static uint led_sm;
static bool led_ready = false;
static int led_state = -1;      // last value sent, -1 for none

void status_led_init(void) {
  uint offset;
  if (!pio_claim_free_sm_and_add_program(&ws2812_program, &led_pio, &led_sm, &offset)) {
    printf("status LED: no free PIO state machine\n");
    return;
  }
  pio_gpio_init(led_pio, STATUS_LED_PIN);
  pio_sm_set_consecutive_pindirs(led_pio, led_sm, STATUS_LED_PIN, 1, true);

  pio_sm_config c = ws2812_program_get_default_config(offset);
  sm_config_set_sideset_pins(&c, STATUS_LED_PIN);
  sm_config_set_out_shift(&c, false, true, 24);   // MSB first, 24 bits per LED
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&c, (float) clock_get_hz(clk_sys) /
                           (WS2812_BIT_RATE * WS2812_CYCLES_PER_BIT));
  pio_sm_init(led_pio, led_sm, offset, &c);
  pio_sm_set_enabled(led_pio, led_sm, true);
  led_ready = true;

  status_led_set(false);
}

void status_led_set(bool on) {
  if (!led_ready || led_state == (int) on) return;
  led_state = on;
  pio_sm_put_blocking(led_pio, led_sm, (on ? STATUS_LED_ON_GRB : 0u) << 8);
}
