#include "i2s_out.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "i2s_out.pio.h"

// 128 frames = 2.67 ms per buffer at 48 kHz
#define I2S_BUF_FRAMES 128

static uint32_t buffers[2][I2S_BUF_FRAMES];
static int dma_chan[2];
static i2s_fill_fn fill_cb;

static void __isr dma_handler(void) {
  for (int i = 0; i < 2; i++) {
    int ch = dma_chan[i];
    if (dma_irqn_get_channel_status(0, ch)) {
      dma_irqn_acknowledge_channel(0, ch);
      // The other channel is already playing (chained). Rearm this one
      // without triggering, then refill its buffer.
      dma_channel_set_read_addr(ch, buffers[i], false);
      fill_cb(buffers[i], I2S_BUF_FRAMES);
    }
  }
}

void i2s_out_init(uint data_pin, uint clock_pin_base, uint32_t sample_rate, i2s_fill_fn fill) {
  fill_cb = fill;

  PIO pio = pio0;
  uint sm = pio_claim_unused_sm(pio, true);
  uint offset = pio_add_program(pio, &i2s_out_program);
  i2s_out_program_init(pio, sm, offset, data_pin, clock_pin_base);

  // Two PIO cycles per bit, 32 bits per frame.
  uint32_t sys_hz = clock_get_hz(clk_sys);
  uint32_t pio_hz = sample_rate * 64;
  uint32_t div_int = sys_hz / pio_hz;
  uint32_t div_frac = (uint32_t) ((((uint64_t) (sys_hz % pio_hz)) << 8) / pio_hz);
  pio_sm_set_clkdiv_int_frac(pio, sm, (uint16_t) div_int, (uint8_t) div_frac);

  for (int i = 0; i < 2; i++) {
    fill_cb(buffers[i], I2S_BUF_FRAMES);
    dma_chan[i] = dma_claim_unused_channel(true);
  }

  for (int i = 0; i < 2; i++) {
    dma_channel_config c = dma_channel_get_default_config(dma_chan[i]);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c, dma_chan[i ^ 1]);
    dma_channel_configure(dma_chan[i], &c, &pio->txf[sm], buffers[i], I2S_BUF_FRAMES, false);
    dma_irqn_set_channel_enabled(0, dma_chan[i], true);
  }

  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
  irq_set_enabled(DMA_IRQ_0, true);

  dma_channel_start(dma_chan[0]);
  pio_sm_set_enabled(pio, sm, true);
}
