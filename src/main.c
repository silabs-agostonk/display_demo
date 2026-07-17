/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * Copyright (c) 2019 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * Based on ST7789V sample:
 * Copyright (c) 2019 Marc Reilly
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>

#include <stdint.h>
#include <stdlib.h>

#include "app_types.h"
#include "ble_hid_app.h"
#include "app_input.h"
#include "app_display.h"
#include "app_graphics.h"
#include "app_graphics_marker.h"

enum states_t {
  start,
  load_game,
  run_game,
  finish_game
};


int main(void) {

	enum states_t app_state = start;
	struct int16_xy_pair marker_pos_actual;
	struct int16_xy_pair marker_pos_last_drawn;
	struct int16_xy_pair marker_pos_new;
	struct mouse_data_element mouse_data_new_element;

	display_blanking_off(display_dev);

	while (1){

		switch (app_state){
			case start:

			LOG_INF("Starting BLE Maze game");

			// Initialize inputs and graphics
			app_input_init();
			app_graphics_init();

			ble_hid_app_start();
			app_state = load_game;
			break;

			case load_game:
			canvas_clean();
			load_background();

			// Set and draw marker start position:
			marker_pos_actual.x = 10;
			marker_pos_actual.y = DISPLAY_H / 2;
			canvas_draw(marker_pos_actual.x, marker_pos_actual.y);
			draw_marker(marker_pos_actual.x, marker_pos_actual.y);
			marker_pos_last_drawn = marker_pos_actual;
		
			app_input_flush();

			app_state = run_game;
			break;

			case run_game:

			if (app_input_get_mouse(&mouse_data_new_element, K_FOREVER) == 0){

				if (mouse_data_new_element.left_button){
					// Clean the actual game
					app_state = load_game;
					break;
				}
				else if (mouse_data_new_element.right_button){
					// Jump to the next game
					app_state = load_game;
					next_background();
					break;
				}
				// Calculate new position
				marker_pos_new.x = marker_pos_actual.x + mouse_data_new_element.dx;
				marker_pos_new.y = marker_pos_actual.y + mouse_data_new_element.dy;

				// Keep new position within display
				if (marker_pos_new.x > DISPLAY_W - MARKER_BUF_DIM / 2 - 1) marker_pos_new.x = DISPLAY_W - MARKER_BUF_DIM / 2 - 1;
				if (marker_pos_new.x < MARKER_BUF_DIM / 2 + 1) marker_pos_new.x = MARKER_BUF_DIM / 2 + 1;

				if (marker_pos_new.y > DISPLAY_H - MARKER_BUF_DIM / 2 - 1) marker_pos_new.y = DISPLAY_H - MARKER_BUF_DIM / 2 - 1;
				if (marker_pos_new.y < MARKER_BUF_DIM / 2 + 1) marker_pos_new.y = MARKER_BUF_DIM / 2 + 1;
				
				/* Draw a line with Bresenham's algorithm. The display-frame
				* limits have already been handled above.
				*/
				int16_t x0 = marker_pos_actual.x;
				int16_t y0 = marker_pos_actual.y;
				const int16_t x1 = marker_pos_new.x;
				const int16_t y1 = marker_pos_new.y;

				const int16_t dx = abs(x1 - x0);
				const int16_t dy = abs(y1 - y0);
				const int16_t sx = (x0 < x1) ? 1 : -1;
				const int16_t sy = (y0 < y1) ? 1 : -1;

				int16_t err = dx - dy;
				uint8_t draw_step = 0;

				while (x0 != x1 || y0 != y1) {
					int16_t old_err = err;
					int16_t e2 = 2 * old_err;

					bool step_x = (x0 != x1) && (e2 > -dy);
					bool step_y = (y0 != y1) && (e2 < dx);

					int16_t next_x = x0 + (step_x ? sx : 0);
					int16_t next_y = y0 + (step_y ? sy : 0);
					int16_t next_err = old_err;

					if (step_x) {
						next_err -= dy;
					}

					if (step_y) {
						next_err += dx;
					}

					touch_elements_t touched = marker_touching(next_x, next_y);

					if (touched == touched_finish_line) {
						LOG_INF("Drawing: touched finish color");
						app_state = finish_game;
						break;
					}

					if (touched != touched_wall) {
						/* The normal Bresenham step is free. */
						x0 = next_x;
						y0 = next_y;
						err = next_err;
					} else {
						/*
						* The requested step is blocked. Try moving along one axis
						* from the last collision-free position. This produces sliding
						* along horizontal, vertical and diagonal obstacles.
						*/
						bool can_move_x = false;
						bool can_move_y = false;

						touch_elements_t touched_x = touched_wall;
						touch_elements_t touched_y = touched_wall;

						if (x0 != x1) {
							touched_x = marker_touching(x0 + sx, y0);
							can_move_x = touched_x != touched_wall;
						}

						if (y0 != y1) {
							touched_y = marker_touching(x0, y0 + sy);
							can_move_y = touched_y != touched_wall;
						}

						/* Check the finish line for the alternative movements too. */
						if ((can_move_x && touched_x == touched_finish_line) ||
							(can_move_y && touched_y == touched_finish_line)) {
							LOG_INF("Drawing: touched finish color");
							app_state = finish_game;
							break;
						}

						/*
						* Prefer the axis having the largest remaining mouse movement.
						* If it is blocked, slide along the other axis.
						*/
						if (abs(x1 - x0) >= abs(y1 - y0)) {
							if (can_move_x) {
								x0 += sx;
								err = old_err - dy;
							} else if (can_move_y) {
								y0 += sy;
								err = old_err + dx;
							} else {
								LOG_DBG("Drawing: movement completely blocked");
								break;
							}
						} else {
							if (can_move_y) {
								y0 += sy;
								err = old_err + dx;
							} else if (can_move_x) {
								x0 += sx;
								err = old_err - dy;
							} else {
								LOG_DBG("Drawing: movement completely blocked");
								break;
							}
						}
					}

					canvas_draw(x0, y0);

					/*
					* Keep the logical position synchronized with every collision-free
					* step. Display updates can still be throttled.
					*/
					marker_pos_actual.x = x0;
					marker_pos_actual.y = y0;

					draw_step++;

					/*
					* A wall can stop the Bresenham loop before the next throttled
					* display update. In that case (x0 == x1 && y0 == y1) render
					* the final position as well.
					*/
				
					if (draw_step >= MAX_MOVEMENT_WITHOUT_UPDATE || (x0 == x1 && y0 == y1)) {
						draw_marker(x0, y0);
						marker_pos_last_drawn = marker_pos_actual;
						draw_step = 0;
					}
				}
			}

			break;

			case finish_game:

			LOG_INF("Game: loading the next background");

			// Would be great to show some text on display
			k_msleep(500);
			
			next_background();

			app_state = load_game;
			break;

			default:
			app_state = start;
        	break;
		}

	}
	return 0;
}
