#ifndef _USB_DEBUG_H_
#define _USB_DEBUG_H_

// Debug output over a USB serial (CDC) port, alongside the UART.
//
// Everything printed goes into a ring buffer, which is sent to the port
// whenever a terminal has it open, so the messages from boot are still
// there when one opens it later. Opening the port at 1200 baud restarts the
// RP2350 in its USB bootloader, as the Arduino IDE expects.

#ifdef __cplusplus
extern "C" {
#endif

void usb_debug_init(void);
// Call from the main loop, after tud_task()
void usb_debug_task(void);

#ifdef __cplusplus
}
#endif

#endif
