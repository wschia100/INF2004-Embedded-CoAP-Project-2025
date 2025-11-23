#ifndef CS04_HARDWARE_H
#define CS04_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "ff.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "ws2812.h"

// Structure representing a hardware button input.
typedef struct {
    uint pin;         // GPIO pin number
    bool last_state;  // Previous state for edge detection
} button_t;

// Sets the RGB LED to the specified color and brightness.
void hw_led_set_color(uint8_t r, uint8_t g, uint8_t b, float brightness);

// Turns off the RGB LED.
void hw_led_off(void);

// Blinks the RGB LED in the specified color for a duration in ms.
void hw_led_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms);

// Sounds the buzzer on the specified pin at a given frequency and duration.
void hw_buzz(uint pin, uint frequency, uint duration_ms);

// Visual and audio feedback for operation success (e.g., file written).
void hw_signal_success(void);

// Visual and audio feedback for error condition.
void hw_signal_error(void);

// Visual and audio feedback for progress (e.g., block transferred).
void hw_signal_progress(void);

// Initializes a button state structure and hardware pin.
void hw_button_init(button_t *btn, uint pin);

// Checks if button is pressed (with debouncing).
bool hw_button_pressed(button_t *btn);

// Mounts the SD card and initializes the FATFS.
bool hw_sd_init(FATFS *fs);

// Checks if a file exists on the SD card.
bool hw_file_exists(const char *filename);

// Constructs a 32-bit WS2812 RGB color value.
uint32_t hw_urgb_u32(uint8_t r, uint8_t g, uint8_t b, float brightness);

// Shows a triple-beep/LED flash for file transfer complete.
void hw_play_file_complete_signal(PIO pio, int sm, int buzzer_pin);

// Double-beep/flash signal for receiving string notifications.
void hw_play_string_signal(PIO pio, int sm, int buzzer_pin);

// Beep/cyan LED pattern for a FETCH transfer event.
void hw_play_fetch_signal(PIO pio, int sm, int buzzer_pin);

// Double-beep/green LED pattern for iPATCH append event.
void hw_play_append_success_signal(PIO pio, int sm, int buzzer_pin);

// Triple-beep/LED flash for FETCH completion.
void hw_play_fetch_success_signal(PIO pio, int sm, int buzzer_pin);

#endif  // CS04_HARDWARE_H
