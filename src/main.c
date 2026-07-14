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
	struct int16_xy_pair marker_pos_new;
	struct mouse_data_element mouse_data_new_element;

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
				if (marker_pos_new.x > DISPLAY_W - MARKER_BUF_MIN_WIDTH / 2 - 1) marker_pos_new.x = DISPLAY_W - MARKER_BUF_MIN_WIDTH / 2 - 1;
				if (marker_pos_new.x < MARKER_BUF_MIN_WIDTH / 2 + 1) marker_pos_new.x = MARKER_BUF_MIN_WIDTH / 2 + 1;

				if (marker_pos_new.y > DISPLAY_H - MARKER_BUF_MIN_HEIGHT / 2 - 1) marker_pos_new.y = DISPLAY_H - MARKER_BUF_MIN_HEIGHT / 2 - 1;
				if (marker_pos_new.y < MARKER_BUF_MIN_HEIGHT / 2 + 1) marker_pos_new.y = MARKER_BUF_MIN_HEIGHT / 2 + 1;
				
				// Draw a line with Bresenham’s algorithm
				uint16_t x0 = marker_pos_actual.x;
				uint16_t y0 = marker_pos_actual.y;
				uint16_t x1 = marker_pos_new.x;
				uint16_t y1 = marker_pos_new.y;

				int16_t dx = abs(x1 - x0);
				int16_t dy = abs(y1 - y0);
				int16_t sx = (x0 < x1) ? 1 : -1;
				int16_t sy = (y0 < y1) ? 1 : -1;
				int16_t err = dx - dy;

				uint8_t k_step_cnt = 0;
				while (1) {
					if (x0 == x1 && y0 == y1) break;

					int16_t e2 = err << 1;

					if (e2 > -dy) { err -= dy; x0 += sx; }
					if (e2 < dx) { err += dx; y0 += sy; }

					touch_elements_t touched;
					touched = marker_touching(x0, y0);

					if (touched_finish_line == touched){
						LOG_INF("Drawing: touched finish color");
						app_state = finish_game;
						break;
					}

					if(touched_wall == touched){
						LOG_INF("Drawing: touched wall");
						// it would be okay to stop here, but try to move only one axes
						// it makes the app more responsive
						app_input_flush();
						
						// move only by X
						if (marker_touching(x0, marker_pos_actual.y) != touched_wall){
							canvas_draw(x0, marker_pos_actual.y);
							draw_marker(x0, marker_pos_actual.y);

							marker_pos_actual.x = x0;
							break;
						}

						// move only by Y
						else if (marker_touching(marker_pos_actual.x, y0) != touched_wall){
							canvas_draw(marker_pos_actual.x, y0);
							draw_marker(marker_pos_actual.x, y0);

							marker_pos_actual.y = y0;
							break;
						}

						else {
							// nothing left to do here
						}
						
						break;
					}
					else {
						canvas_draw(x0, y0);
						if (k_step_cnt % 1 == 0){
							//display_marker_draw(display_dev, marker_pos_actual.x, marker_pos_actual.y, x0, y0);
							//display_update_from_canvas(display_dev, x0, y0, current_background, mk_color, line_color);
							draw_marker(x0, y0);
							marker_pos_actual.x = x0;
							marker_pos_actual.y = y0;
						}
					}
					k_step_cnt++;
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
