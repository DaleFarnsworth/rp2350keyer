cmake_minimum_required(VERSION 3.13)

# Waveshare RP2350-Zero (RP2350, Arm cores)
set(PICO_BOARD waveshare_rp2350_zero CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(rp2350_zero_vail_audio C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(rp2350_zero_vail_audio
    src/main.cpp
    src/adapter.cpp
    src/keyers.cpp
    src/sidetone.c
    src/audio.c
    src/i2s_out.c
    src/tlv320dac3100.c
    src/status_led.c
    src/settings.c
    src/usb_descriptors.c
    src/usb_debug.c
    src/hang_trace.c
    src/quirk_os_guessing.c
)

pico_generate_pio_header(rp2350_zero_vail_audio ${CMAKE_CURRENT_LIST_DIR}/src/i2s_out.pio)
pico_generate_pio_header(rp2350_zero_vail_audio ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(rp2350_zero_vail_audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(rp2350_zero_vail_audio PRIVATE
    pico_stdlib
    pico_unique_id
    pico_flash
    hardware_pio
    hardware_dma
    hardware_i2c
    hardware_clocks
    hardware_flash
    hardware_uart
    tinyusb_device
)

# USB is owned by our TinyUSB composite device, which has its own serial
# port for debug output (src/usb_debug.c). Debug output also goes to UART0,
# moved from the board's default GP0/GP1, which are the key inputs, to
# GP12 (TX) and GP13 (RX). src/usb_debug.c drives the UART itself, without
# blocking, rather than through the SDK's stdio_uart, which waits for each
# character and so stalled the audio.
pico_enable_stdio_usb(rp2350_zero_vail_audio 0)
pico_enable_stdio_uart(rp2350_zero_vail_audio 0)
# Wait 64 ms rather than the SDK's 6 ms for the crystal to start, as
# Arduino-Pico does for every board. With the shorter wait the Zero's
# crystal sometimes wasn't running yet at power-on, and the clock setup
# hung before USB or the LED started.
target_compile_definitions(rp2350_zero_vail_audio PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_definitions(rp2350_zero_vail_audio PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=12
    PICO_DEFAULT_UART_RX_PIN=13
)

pico_set_program_name(rp2350_zero_vail_audio "RP2350-Zero Vail Audio")
pico_add_extra_outputs(rp2350_zero_vail_audio)
