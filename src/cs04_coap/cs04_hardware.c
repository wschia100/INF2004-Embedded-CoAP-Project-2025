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

/**
 * @brief Construct a 32-bit WS2812 RGB value with brightness scaling.
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @param brightness Scales all channels (0.0-1.0)
 * @return Packed uint32_t WS2812 RGB value
 */
uint32_t hw_urgb_u32(uint8_t r, uint8_t g, uint8_t b, float brightness)
{
    r = (uint8_t) (r * brightness);
    g = (uint8_t) (g * brightness);
    b = (uint8_t) (b * brightness);
    return ((uint32_t) g << 16) | ((uint32_t) r << 8) | b;
}

/**
 * @brief Set the WS2812 LED to given RGB color/brightness.
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @param brightness Brightness (0.0-1.0)
 */
void hw_led_set_color(uint8_t r, uint8_t g, uint8_t b, float brightness)
{
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(r, g, b, brightness));
}

/**
 * @brief Turn WS2812 LED off.
 */
void hw_led_off(void)
{
    hw_led_set_color(0, 0, 0, 0.0f);
}

/**
 * @brief Blink the WS2812 LED with RGB color, hold duration, then dim to idle
 * color.
 * @param r Red
 * @param g Green
 * @param b Blue
 * @param duration_ms Duration in ms to show color before dimming.
 */
void hw_led_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms)
{
    hw_led_set_color(r, g, b, 0.5f);
    sleep_ms(duration_ms);
    hw_led_set_color(0, 10, 10, 0.1f);
}

/**
 * @brief Sound a buzzer on a pin at a frequency for given duration (ms).
 * @param pin GPIO pin of buzzer
 * @param frequency Tone frequency (Hz)
 * @param duration_ms Tone duration (ms)
 */
void hw_buzz(uint pin, uint frequency, uint duration_ms)
{
    if (frequency == 0)
        return;
    uint delay_us = 500000 / frequency;
    uint cycles = frequency * duration_ms / 1000;
    for (uint i = 0; i < cycles; i++) {
        gpio_put(pin, 1);
        sleep_us(delay_us);
        gpio_put(pin, 0);
        sleep_us(delay_us);
    }
}

/**
 * @brief Play visual/audio feedback for success (green LED, short buzzer).
 */
void hw_signal_success(void)
{
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

/**
 * @brief Play visual/audio signal for error (red LED, lower buzz).
 */
void hw_signal_error(void)
{
    hw_led_set_color(50, 0, 0, 0.5f);
    hw_buzz(18, 400, 100);
    sleep_ms(100);
    hw_led_set_color(0, 10, 10, 0.1f);
}

/**
 * @brief Play progress indication (short buzz, orange LED).
 */
void hw_signal_progress(void)
{
    hw_buzz(18, 1500, 30);
    hw_led_set_color(0, 50, 0, 0.5f);
}

/**
 * @brief Initialize button_t state and hardware pin.
 * @param btn Button state pointer
 * @param pin Hardware GPIO pin
 */
void hw_button_init(button_t *btn, uint pin)
{
    btn->pin = pin;
    btn->last_state = true;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

/**
 * @brief Check if button is pressed (debounce logic).
 * @param btn Pointer to button_t
 * @return true if pressed, false otherwise
 */
bool hw_button_pressed(button_t *btn)
{
    bool current = !gpio_get(btn->pin);
    bool pressed = current && btn->last_state;
    btn->last_state = !current;
    return pressed;
}

/**
 * @brief Initialize SD card, mount filesystem.
 * @param fs FATFS pointer for mount
 * @return true on success, false on failure
 */
bool hw_sd_init(FATFS *fs)
{
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

/**
 * @brief Check if a file exists on the mounted SD card.
 * @param filename Filename to check
 * @return true if file exists, false otherwise
 */
bool hw_file_exists(const char *filename)
{
    FIL test_file;
    FRESULT fr = f_open(&test_file, filename, FA_READ);
    if (fr == FR_OK) {
        f_close(&test_file);
        return true;
    }
    return false;
}

/**
 * @brief Play file transfer complete signal (triple beep with green LED
 * flashes).
 *
 * Used for visual and audible feedback to indicate a file operation (like
 * download) has completed successfully.
 *
 * @param pio PIO instance for WS2812 LED control.
 * @param sm State machine number for the PIO.
 * @param buzzer_pin GPIO pin connected to the buzzer.
 */
void hw_play_file_complete_signal(PIO pio, int sm, int buzzer_pin)
{
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1500, 60);
    sleep_ms(70);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1500, 60);
    sleep_ms(70);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1500, 150);
    sleep_ms(80);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 10, 10, 0.1f));
}

/**
 * @brief Play string notification received signal (double beep, green LED
 * flashes).
 *
 * Provides feedback that a notification (such as button notification or
 * message) has been successfully received from the server.
 *
 * @param pio PIO instance for WS2812 LED control.
 * @param sm State machine number for the PIO.
 * @param buzzer_pin GPIO pin connected to the buzzer.
 */
void hw_play_string_signal(PIO pio, int sm, int buzzer_pin)
{
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1200, 60);
    sleep_ms(80);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 0, 0, 0.1f));
    sleep_ms(40);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1200, 60);
    sleep_ms(80);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 10, 10, 0.1f));
}

/**
 * @brief Play progress signal (FETCH operation) as cyan LED flash and beep
 * sequence.
 *
 * Indicates progress or action during blockwise or FETCH operations.
 *
 * @param pio PIO instance for WS2812 LED control.
 * @param sm State machine number for the PIO.
 * @param buzzer_pin Buzzer GPIO pin.
 */
void hw_play_fetch_signal(PIO pio, int sm, int buzzer_pin)
{
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 50, 0.5f));
    hw_buzz(buzzer_pin, 1800, 40);
    sleep_ms(50);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 0, 0, 0.1f));
    sleep_ms(30);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 50, 0.5f));
    hw_buzz(buzzer_pin, 1800, 40);
    sleep_ms(50);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 0, 0, 0.1f));
    sleep_ms(30);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 50, 0.5f));
    hw_buzz(buzzer_pin, 1800, 40);
    sleep_ms(80);
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 10, 0, 0.1f));
}

/**
 * @brief Play iPATCH/APPEND success signal (double green beep and LED flashes).
 *
 * Used when an append (iPATCH) operation to file is acknowledged or completes.
 *
 * @param pio PIO instance for LED control.
 * @param sm State machine index for LED.
 * @param buzzer_pin GPIO pin driving the buzzer.
 */
void hw_play_append_success_signal(PIO pio, int sm, int buzzer_pin)
{
    // First blink
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1800, 60);
    sleep_ms(70);

    // Off period
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 0, 0, 0.0f));
    sleep_ms(30);

    // Second blink
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(buzzer_pin, 1800, 60);
    sleep_ms(80);

    // Return to idle
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 10, 10, 0.1f));
}

/**
 * @brief Play FETCH success signal (triple cyan beep and LED flashes).
 *
 * Signals to the user that a FETCH request or file transfer completed
 * successfully.
 *
 * @param pio PIO instance for LED.
 * @param sm State machine number for LED.
 * @param buzzer_pin Buzzer pin number.
 */
void hw_play_fetch_success_signal(PIO pio, int sm, int buzzer_pin)
{
    // First blink
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 50, 0.5f));
    hw_buzz(buzzer_pin, 1800, 40);
    sleep_ms(50);

    // Off period
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 0, 0, 0.0f));
    sleep_ms(30);

    // Second blink
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 50, 0.5f));
    hw_buzz(buzzer_pin, 1800, 40);
    sleep_ms(50);

    // Off period
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 0, 0, 0.0f));
    sleep_ms(30);

    // Third blink
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 50, 50, 0.5f));
    hw_buzz(buzzer_pin, 1800, 40);
    sleep_ms(80);

    // Return to idle
    ws2812_put_pixel(pio, sm, hw_urgb_u32(0, 10, 10, 0.1f));
}
