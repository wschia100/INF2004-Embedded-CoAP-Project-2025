#include "cs04_hardware.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "ws2812.h"
#include "sd_card.h"
#include <stdio.h>

// External WS2812 state (from main)
extern PIO pio_ws2812;
extern int sm_ws2812;

uint32_t hw_urgb_u32(uint8_t r, uint8_t g, uint8_t b, float brightness) {
    r = (uint8_t)(r * brightness);
    g = (uint8_t)(g * brightness);
    b = (uint8_t)(b * brightness);
    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

void hw_led_set_color(uint8_t r, uint8_t g, uint8_t b, float brightness) {
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(r, g, b, brightness));
}

void hw_led_off(void) {
    hw_led_set_color(0, 0, 0, 0.0f);
}

void hw_led_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms) {
    hw_led_set_color(r, g, b, 0.5f);
    sleep_ms(duration_ms);
    hw_led_set_color(0, 10, 10, 0.1f);
}

void hw_buzz(uint pin, uint frequency, uint duration_ms) {
    if (frequency == 0) return;
    uint delay_us = 500000 / frequency;
    uint cycles = frequency * duration_ms / 1000;
    for (uint i = 0; i < cycles; i++) {
        gpio_put(pin, 1);
        sleep_us(delay_us);
        gpio_put(pin, 0);
        sleep_us(delay_us);
    }
}

void hw_signal_success(void) {
    hw_led_set_color(0, 50, 0, 0.5f);
    hw_buzz(18, 1800, 60);
    sleep_ms(70);
    hw_led_off();
    sleep_ms(30);
    hw_led_set_color(0, 50, 0, 0.5f);
    hw_buzz(18, 1800, 60);
    sleep_ms(80);
    hw_led_set_color(0, 10, 10, 0.1f);
}

void hw_signal_error(void) {
    hw_led_set_color(50, 0, 0, 0.5f);
    hw_buzz(18, 400, 100);
    sleep_ms(100);
    hw_led_set_color(0, 10, 10, 0.1f);
}

void hw_signal_progress(void) {
    hw_buzz(18, 1500, 30);
    hw_led_set_color(0, 50, 0, 0.5f);
}

void hw_button_init(button_t *btn, uint pin) {
    btn->pin = pin;
    btn->last_state = true;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

bool hw_button_pressed(button_t *btn) {
    bool current = !gpio_get(btn->pin);
    bool pressed = current && btn->last_state;
    btn->last_state = !current;
    return pressed;
}

bool hw_sd_init(FATFS *fs) {
    if (!sd_init_driver()) {
        printf("ERROR: Could not initialize SD card\n");
        return false;
    }
    
    FRESULT fr = f_mount(fs, "0:", 1);
    if (fr != FR_OK) {
        printf("ERROR: Failed to mount SD card: %d\n", fr);
        return false;
    }
    
    printf("SD card mounted successfully.\n");
    return true;
}

bool hw_file_exists(const char *filename) {
    FIL test_file;
    FRESULT fr = f_open(&test_file, filename, FA_READ);
    if (fr == FR_OK) {
        f_close(&test_file);
        return true;
    }
    return false;
}
