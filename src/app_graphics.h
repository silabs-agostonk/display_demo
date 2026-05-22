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



#ifdef __cplusplus
}
#endif

#endif /* APP_GRAPHICS_H_ */