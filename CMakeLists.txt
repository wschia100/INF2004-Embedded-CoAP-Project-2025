cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)

project(coap_pico_project C CXX ASM)
pico_sdk_init()

set(PICO_STDIO_USB 1)
set(PICO_STDIO_UART 0)
add_subdirectory(external_libraries/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)


# Define library paths
set(MICROCOAP_SRC ${CMAKE_CURRENT_LIST_DIR}/external_libraries/microcoap)
set(FATFS_SRC ${CMAKE_CURRENT_LIST_DIR}/external_libraries/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI)
set(CS04_SRC ${CMAKE_CURRENT_LIST_DIR}/src/cs04_coap)
set(SRC ${CMAKE_CURRENT_LIST_DIR}/src)

# === Server target ===
add_executable(coap_server
    ${SRC}/coap_server.c
    ${CS04_SRC}/cs04_coap_packet.c
    ${CS04_SRC}/cs04_coap_reliability.c 
    ${CS04_SRC}/cs04_hardware.c 
    ${MICROCOAP_SRC}/coap.c
    ${FATFS_SRC}/ff15/source/ff.c
    ${FATFS_SRC}/sd_driver/sd_card.c
    ws2812.c
)

# Generate PIO header specifically FOR the server target
pico_generate_pio_header(coap_server ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio) # Correct usage

target_include_directories(coap_server PRIVATE
    ${SRC}                          # For main
    ${CMAKE_CURRENT_LIST_DIR}       # For lwipopts.h, ws2812.h etc.
    ${MICROCOAP_SRC}                # For coap.h
    ${FATFS_SRC}/ff15/source        # For ff.h
    ${FATFS_SRC}/sd_driver          # For sd_card.h (if it exists)
    ${FATFS_SRC}                    # For hw_config.h (if needed by sd_card.c/ff.c)
    ${CS04_SRC}
)

target_link_libraries(coap_server PRIVATE
    pico_stdlib
    pico_cyw43_arch_lwip_poll
    hardware_spi
    hardware_pio
    FatFs_SPI
)

pico_add_extra_outputs(coap_server)
pico_enable_stdio_usb(coap_server 1)
pico_enable_stdio_uart(coap_server 0)


# === Client target ===
add_executable(coap_client
    ${SRC}/coap_client.c
    ${CS04_SRC}/cs04_coap_packet.c
    ${CS04_SRC}/cs04_coap_reliability.c 
    ${CS04_SRC}/cs04_hardware.c 
    ${MICROCOAP_SRC}/coap.c
    ${FATFS_SRC}/ff15/source/ff.c
    ${FATFS_SRC}/sd_driver/sd_card.c
    ws2812.c                    # Assuming ws2812.c is in the root
)

# Generate PIO header specifically FOR the client target
pico_generate_pio_header(coap_client ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio) # Correct usage

target_include_directories(coap_client PRIVATE
    ${SRC}                          # For main
    ${CMAKE_CURRENT_LIST_DIR}       # For lwipopts.h, ws2812.h etc.
    ${MICROCOAP_SRC}                # For coap.h
    ${FATFS_SRC}/ff15/source        # For ff.h
    ${FATFS_SRC}/sd_driver          # For sd_card.h (if it exists)
    ${FATFS_SRC}                    # For hw_config.h (if needed)
    ${CS04_SRC}
)

target_link_libraries(coap_client PRIVATE
    pico_stdlib
    pico_cyw43_arch_lwip_poll
    hardware_spi
    hardware_pio
    FatFs_SPI
)

pico_add_extra_outputs(coap_client)
pico_enable_stdio_usb(coap_client 1)
pico_enable_stdio_uart(coap_client 0)
