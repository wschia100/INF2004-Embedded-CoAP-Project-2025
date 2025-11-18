#ifndef CS04_HARDWARE_H
#define CS04_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"  // ✅ ADD THIS - defines 'uint' type
#include "ff.h"

// Button management
typedef struct {
    uint pin;
    bool last_state;
} button_t;

// LED control
void hw_led_set_color(uint8_t r, uint8_t g, uint8_t b, float brightness);
void hw_led_off(void);
void hw_led_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms);

// Buzzer
void hw_buzz(uint pin, uint frequency, uint duration_ms);
void hw_signal_success(void);
void hw_signal_error(void);
void hw_signal_progress(void);

// Button
void hw_button_init(button_t *btn, uint pin);
bool hw_button_pressed(button_t *btn);

// SD card
bool hw_sd_init(FATFS *fs);
bool hw_file_exists(const char *filename);

// WS2812 utility
uint32_t hw_urgb_u32(uint8_t r, uint8_t g, uint8_t b, float brightness);

#endif // CS04_HARDWARE_H
