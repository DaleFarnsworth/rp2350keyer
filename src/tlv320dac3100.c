#include "tlv320dac3100.h"

#include <stdio.h>

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

// Register numbers from the TLV320DAC3100 datasheet (SLAS659).
// Page 0
#define P0_SW_RESET        0x01
#define P0_CLOCK_MUX       0x04
#define P0_PLL_P_R         0x05
#define P0_PLL_J           0x06
#define P0_PLL_D_MSB       0x07
#define P0_PLL_D_LSB       0x08
#define P0_NDAC            0x0B
#define P0_MDAC            0x0C
#define P0_DOSR_MSB        0x0D
#define P0_DOSR_LSB        0x0E
#define P0_CODEC_IF        0x1B
#define P0_DAC_PRB         0x3C
#define P0_DAC_DATAPATH    0x3F
#define P0_DAC_VOL_CTRL    0x40
#define P0_DAC_LVOL        0x41
#define P0_DAC_RVOL        0x42
#define P0_VOL_MICDET_ADC  0x74
// Page 1
#define P1_HP_DRIVERS      0x1F
#define P1_SPK_AMP         0x20
#define P1_HP_POP          0x21
#define P1_DAC_MIXER       0x23
#define P1_HPL_ANALOG_VOL  0x24
#define P1_HPR_ANALOG_VOL  0x25
#define P1_SPK_ANALOG_VOL  0x26
#define P1_HPL_DRIVER      0x28
#define P1_HPR_DRIVER      0x29
#define P1_SPK_DRIVER      0x2A

#define PAGE_SELECT        0x00

static bool write_reg(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = { reg, val };
  int n = i2c_write_blocking(DAC_I2C_INST, DAC_I2C_ADDR, buf, 2, false);
  if (n != 2) {
    printf("dac3100: write reg 0x%02x failed\n", reg);
    return false;
  }
  return true;
}

static bool select_page(uint8_t page) {
  return write_reg(PAGE_SELECT, page);
}

bool dac3100_init(void) {
  i2c_init(DAC_I2C_INST, DAC_I2C_BAUD);
  gpio_set_function(DAC_I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(DAC_I2C_SCL_PIN, GPIO_FUNC_I2C);
  // The Adafruit breakout has its own pull-ups; these are just a backstop.
  gpio_pull_up(DAC_I2C_SDA_PIN);
  gpio_pull_up(DAC_I2C_SCL_PIN);

  gpio_init(DAC_RESET_PIN);
  gpio_set_dir(DAC_RESET_PIN, GPIO_OUT);
  gpio_put(DAC_RESET_PIN, 0);
  sleep_ms(2);
  gpio_put(DAC_RESET_PIN, 1);
  sleep_ms(2);

  bool ok = true;
  ok &= select_page(0);
  ok &= write_reg(P0_SW_RESET, 0x01);
  sleep_ms(2);

  // Clocking, for fs = 48 kHz and BCLK = 32 * fs = 1.536 MHz:
  //   PLL_CLK   = BCLK * R * J.D / P = 1.536 MHz * 2 * 32 / 1 = 98.304 MHz
  //   DAC_fs    = PLL_CLK / (NDAC * MDAC * DOSR) = 98.304 MHz / (8 * 2 * 128)
  ok &= write_reg(P0_CLOCK_MUX, 0x07);       // PLL_CLKIN = BCLK, CODEC_CLKIN = PLL
  ok &= write_reg(P0_PLL_J, 32);
  ok &= write_reg(P0_PLL_D_MSB, 0);
  ok &= write_reg(P0_PLL_D_LSB, 0);
  ok &= write_reg(P0_PLL_P_R, 0x80 | (1 << 4) | 2);  // power up, P = 1, R = 2
  sleep_ms(10);                              // PLL lock
  ok &= write_reg(P0_NDAC, 0x80 | 8);        // power up, NDAC = 8
  ok &= write_reg(P0_MDAC, 0x80 | 2);        // power up, MDAC = 2
  ok &= write_reg(P0_DOSR_MSB, 0);
  ok &= write_reg(P0_DOSR_LSB, 128);         // DOSR = 128
  ok &= write_reg(P0_CODEC_IF, 0x00);        // I2S, 16 bit, BCLK and WCLK inputs
  ok &= write_reg(P0_DAC_PRB, 0x01);         // processing block PRB_P1
  ok &= write_reg(P0_VOL_MICDET_ADC, 0x00);  // volume pin not used

  // Analog output stage
  ok &= select_page(1);
  ok &= write_reg(P1_HP_POP, 0x4E);          // de-pop: 1.22 s power-on, 3.9 ms ramp
  ok &= write_reg(P1_DAC_MIXER, 0x44);       // left DAC -> HPL, right DAC -> HPR
  ok &= write_reg(P1_HPL_DRIVER, 0x06);      // 0 dB, unmuted
  ok &= write_reg(P1_HPR_DRIVER, 0x06);
  ok &= write_reg(P1_HPL_ANALOG_VOL, 0x80 | DAC_HP_ANALOG_ATTEN);
  ok &= write_reg(P1_HPR_ANALOG_VOL, 0x80 | DAC_HP_ANALOG_ATTEN);
  ok &= write_reg(P1_HP_DRIVERS, 0xC4);      // HPL and HPR powered, 1.35 V common mode
#if DAC_ENABLE_SPEAKER
  ok &= write_reg(P1_SPK_DRIVER, 0x04);      // 6 dB, unmuted
  ok &= write_reg(P1_SPK_ANALOG_VOL, 0x80 | DAC_SPK_ANALOG_ATTEN);
  ok &= write_reg(P1_SPK_AMP, 0x86);         // class-D powered
#endif

  // Power up the DACs and unmute
  ok &= select_page(0);
  ok &= write_reg(P0_DAC_DATAPATH, 0xD4);    // both DACs on, L->L, R->R, soft step
  ok &= write_reg(P0_DAC_LVOL, 0);
  ok &= write_reg(P0_DAC_RVOL, 0);
  ok &= write_reg(P0_DAC_VOL_CTRL, 0x00);    // unmute, independent volumes

  printf("dac3100: init %s\n", ok ? "ok" : "FAILED");
  return ok;
}

bool dac3100_set_digital_volume(int8_t half_db) {
  if (half_db > 48) half_db = 48;
  if (half_db < -127) half_db = -127;
  bool ok = select_page(0);
  ok &= write_reg(P0_DAC_LVOL, (uint8_t) half_db);
  ok &= write_reg(P0_DAC_RVOL, (uint8_t) half_db);
  return ok;
}
