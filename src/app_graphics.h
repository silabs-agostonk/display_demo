/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_GRAPHICS_H_
#define APP_GRAPHICS_H_

#include <zephyr/kernel.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif




extern uint8_t canvas_bitmap[];

// Images from src/bg_*.c
#define BACKGROUND_ELEMENTS 1
extern const c_image bg_hex;
extern const c_image bg_square;
extern const c_image bg_triangle;
extern const c_image bg_test;

// https://codebox.net/pages/maze-generator/online
// Square 14x10 Seed: 857133801
// Triangle 22x10 Seed: 741051981
// Hex 12x10 Seed: 620715798

uint8_t app_graphics_init();

extern int (*load_background)(uint8_t);
extern int (*draw_marker)(uint16_t, uint16_t);
uint8_t canvas_draw_line(uint16_t x0, uint16_t y0);

#define RGB_TO_RGB565(r,g,b) ( (uint16_t)( \
    (((uint16_t)((r) & 0xFF) >> 3) << 11) | \
    (((uint16_t)((g) & 0xFF) >> 2) <<  5) | \
    (((uint16_t)((b) & 0xFF) >> 3) <<  0)   \
) )

#define COLOR_RGB_565_WHITE     (uint16_t) 0xffff
#define COLOR_RGB_565_BLACK     (uint16_t) 0x0000
#define COLOR_RGB_565_RED       RGB_TO_RGB565(255, 0, 0)
#define COLOR_RGB_565_GREEN     RGB_TO_RGB565(0, 255, 0)
#define COLOR_RGB_565_BLUE      RGB_TO_RGB565(0, 0, 255)

#define COLOR_RGB_565_GRAY_50   RGB_TO_RGB565(128, 128, 128)
#define COLOR_RGB_565_GRAY_35   RGB_TO_RGB565(90, 90, 90)
#define COLOR_RGB_565_GRAY_25   RGB_TO_RGB565(64, 64, 64)


#define COLOR_RGB_565_RED_30	RGB_TO_RGB565(77, 0, 0)
#define COLOR_RGB_565_GREEN_30	RGB_TO_RGB565(0, 77, 0)
#define COLOR_RGB_565_BLUE_30	RGB_TO_RGB565(0, 0, 77)

#define COLOR_RGB_565_YELLOW	RGB_TO_RGB565(255, 208, 84)
#define COLOR_RGB_565_BLUE_M	RGB_TO_RGB565(36, 57, 102)
#define COLOR_RGB_565_RED_M		RGB_TO_RGB565(176, 0, 00)


#ifdef __cplusplus
}
#endif

#endif /* APP_GRAPHICS_H_ */