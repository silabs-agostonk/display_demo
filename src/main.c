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
#include <zephyr/sys/util.h>

#include <stdint.h>
#include <stdlib.h>

#include "app_types.h"
#include "ble_hid_app.h"
#include "app_input.h"
#include "app_display.h"
#include "app_graphics.h"
#include "app_graphics_marker.h"

enum states_t {
	APP_STATE_START,
	APP_STATE_LOAD_GAME,
	APP_STATE_RUN_GAME,
	APP_STATE_FINISH_GAME,
};

int main(void)
{
	enum states_t app_state = APP_STATE_START;
	struct int16_xy_pair marker_pos_actual;
	struct int16_xy_pair marker_pos_new;
	struct mouse_data_element mouse_data_new_element;
	int ret;

	while (true) {
		switch (app_state) {
		case APP_STATE_START:
			LOG_INF("Starting BLE Maze game");

			app_input_init();
			ret = app_graphics_init();
			if (ret) {
				LOG_ERR("App graphics init failed: %d", ret);
				return ret;
			}

			ret = ble_hid_app_start();
			if (ret) {
				LOG_ERR("HID app start failed: %d", ret);
				return ret;
			}

			app_state = APP_STATE_LOAD_GAME;
			break;

		case APP_STATE_LOAD_GAME:
			app_graphics_canvas_clear();
			ret = app_graphics_load_background();
			if (ret) {
				LOG_ERR("Load background failed: %d", ret);
				return ret;
			}

			marker_pos_actual.x = 10;
			marker_pos_actual.y = DISPLAY_H / 2;
			app_graphics_canvas_draw(marker_pos_actual.x, marker_pos_actual.y);
			ret = app_graphics_draw_marker(marker_pos_actual.x, marker_pos_actual.y);
			if (ret) {
				LOG_ERR("Draw marker failed: %d", ret);
				return ret;
			}

			app_input_flush();

			app_state = APP_STATE_RUN_GAME;
			break;

		case APP_STATE_RUN_GAME:

			if (app_input_get_mouse(&mouse_data_new_element, K_FOREVER) == 0) {

				if (mouse_data_new_element.left_button) {
					app_state = APP_STATE_LOAD_GAME;
					break;
				} else if (mouse_data_new_element.right_button) {
					app_state = APP_STATE_LOAD_GAME;
					app_graphics_next_background();
					break;
				}

				marker_pos_new.x = CLAMP(
					(int32_t)marker_pos_actual.x + mouse_data_new_element.dx,
					MARKER_BUF_DIM / 2 + 1, DISPLAY_W - MARKER_BUF_DIM / 2 - 1);
				marker_pos_new.y = CLAMP(
					(int32_t)marker_pos_actual.y + mouse_data_new_element.dy,
					MARKER_BUF_DIM / 2 + 1, DISPLAY_H - MARKER_BUF_DIM / 2 - 1);

				/* Draw a line with Bresenham's algorithm. The display-frame
				 * limits have already been handled above.
				 */
				int16_t x0 = marker_pos_actual.x;
				int16_t y0 = marker_pos_actual.y;
				int16_t x1 = marker_pos_new.x;
				int16_t y1 = marker_pos_new.y;

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

					enum touch_element touched =
						app_graphics_marker_touching(next_x, next_y);

					if (touched == TOUCH_FINISH_LINE) {
						LOG_INF("Drawing: touched finish color");
						app_state = APP_STATE_FINISH_GAME;
						break;
					}

					if (touched != TOUCH_WALL) {
						/* The normal Bresenham step is free. */
						x0 = next_x;
						y0 = next_y;
						err = next_err;
					} else {
						/*
						 * The requested step is blocked. Try moving along
						 * one axis from the last collision-free position.
						 * This produces sliding along horizontal, vertical
						 * and diagonal obstacles.
						 */
						bool can_move_x = false;
						bool can_move_y = false;

						enum touch_element touched_x = TOUCH_WALL;
						enum touch_element touched_y = TOUCH_WALL;

						if (x0 != x1) {
							touched_x = app_graphics_marker_touching(
								x0 + sx, y0);
							can_move_x = touched_x != TOUCH_WALL;
						}

						if (y0 != y1) {
							touched_y = app_graphics_marker_touching(
								x0, y0 + sy);
							can_move_y = touched_y != TOUCH_WALL;
						}

						/* Check the finish line for the alternative
						 * movements too. */
						if ((can_move_x &&
						     touched_x == TOUCH_FINISH_LINE) ||
						    (can_move_y &&
						     touched_y == TOUCH_FINISH_LINE)) {
							LOG_INF("Drawing: touched finish color");
							app_state = APP_STATE_FINISH_GAME;
							break;
						}

						/*
						 * Prefer the axis having the largest remaining
						 * mouse movement. If it is blocked, slide along the
						 * other axis.
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
								x1 = x0;
								y1 = y0;
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
								x1 = x0;
								y1 = y0;
							}
						}
					}

					app_graphics_canvas_draw(x0, y0);

					/*
					 * Keep the logical position synchronized with every
					 * collision-free step. Display updates can still be
					 * throttled.
					 */
					marker_pos_actual.x = x0;
					marker_pos_actual.y = y0;

					draw_step++;

					/*
					 * A wall can stop the Bresenham loop before the next
					 * throttled display update. In that case (x0 == x1 && y0 ==
					 * y1) render the final position as well.
					 */

					if (draw_step >= MAX_MOVEMENT_WITHOUT_UPDATE ||
					    (x0 == x1 && y0 == y1)) {
						ret = app_graphics_draw_marker(x0, y0);
						if (ret) {
							LOG_ERR("Draw marker failed: %d", ret);
							return ret;
						}
						draw_step = 0;
					}
				}
			}

			break;

		case APP_STATE_FINISH_GAME:

			LOG_INF("Game: loading the next background");

			k_msleep(500);

			app_graphics_next_background();

			app_state = APP_STATE_LOAD_GAME;
			break;

		default:
			app_state = APP_STATE_START;
			break;
		}
	}
}
