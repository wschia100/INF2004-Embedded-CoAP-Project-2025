#pragma once
#include "hardware/pio.h"
#include <stdint.h>
#include <stdbool.h>

void ws2812_put_pixel(PIO pio, uint sm, uint32_t pixel_grb);
void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, float freq, bool rgbw);
