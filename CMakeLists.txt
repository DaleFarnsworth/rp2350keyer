cmake_minimum_required(VERSION 3.13)

# Raspberry Pi Pico 2 (RP2350, Arm cores)
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(pico2_vail_audio C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico2_vail_audio
    src/main.cpp
    src/adapter.cpp
    src/keyers.cpp
    src/sidetone.c
    src/audio.c
    src/i2s_out.c
    src/tlv320dac3100.c
    src/settings.c
    src/usb_descriptors.c
    src/quirk_os_guessing.c
)

pico_generate_pio_header(pico2_vail_audio ${CMAKE_CURRENT_LIST_DIR}/src/i2s_out.pio)

target_include_directories(pico2_vail_audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(pico2_vail_audio PRIVATE
    pico_stdlib
    pico_unique_id
    pico_flash
    hardware_pio
    hardware_dma
    hardware_i2c
    hardware_clocks
    hardware_flash
    tinyusb_device
)

# USB is owned by our TinyUSB composite device; debug output goes to UART0.
pico_enable_stdio_usb(pico2_vail_audio 0)
pico_enable_stdio_uart(pico2_vail_audio 1)

pico_set_program_name(pico2_vail_audio "Pico 2 Vail Audio")
pico_add_extra_outputs(pico2_vail_audio)
