#include "usb_debug.h"

#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/time.h"
#include "tusb.h"

#define LOG_SIZE 4096   // power of two

static char log_buf[LOG_SIZE];
static volatile uint32_t log_head = 0;    // next byte to write
static uint32_t log_tail = 0;             // next byte to send

static volatile bool reboot_requested = false;
static absolute_time_t reboot_at;

// Never blocks and never touches USB, so it's safe anywhere, including in
// TinyUSB callbacks. When the buffer is full the oldest output is dropped.
static void usb_debug_out_chars(const char *buf, int len) {
  uint32_t irq = save_and_disable_interrupts();
  uint32_t head = log_head;
  for (int i = 0; i < len; i++) {
    log_buf[head & (LOG_SIZE - 1)] = buf[i];
    head++;
  }
  log_head = head;
  if (head - log_tail > LOG_SIZE) {
    log_tail = head - LOG_SIZE;
  }
  restore_interrupts(irq);
}

static stdio_driver_t usb_debug_driver = {
  .out_chars = usb_debug_out_chars,
  .crlf_enabled = true,
};

void usb_debug_init(void) {
  stdio_set_driver_enabled(&usb_debug_driver, true);
}

void usb_debug_task(void) {
  if (reboot_requested && time_reached(reboot_at)) {
    reset_usb_boot(0, 0);
  }

  if (!tud_cdc_connected()) return;

  uint32_t irq = save_and_disable_interrupts();
  uint32_t head = log_head;
  uint32_t tail = log_tail;
  restore_interrupts(irq);

  bool sent = false;
  while (tail != head) {
    uint32_t space = tud_cdc_write_available();
    if (space == 0) break;
    // Send up to the end of the buffer, then wrap on the next pass
    uint32_t start = tail & (LOG_SIZE - 1);
    uint32_t n = head - tail;
    if (n > LOG_SIZE - start) n = LOG_SIZE - start;
    if (n > space) n = space;
    n = tud_cdc_write(&log_buf[start], n);
    if (n == 0) break;
    tail += n;
    sent = true;
  }

  irq = save_and_disable_interrupts();
  // Output may have been dropped meanwhile; never move the tail backwards
  if ((int32_t) (tail - log_tail) > 0) log_tail = tail;
  restore_interrupts(irq);

  if (sent) tud_cdc_write_flush();
}

// Opening the port at 1200 baud asks for the bootloader. Reboot a little
// later, so the host gets its reply to this request first.
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *line_coding) {
  (void) itf;
  if (line_coding->bit_rate == 1200) {
    reboot_requested = true;
    reboot_at = make_timeout_time_ms(50);
  }
}
