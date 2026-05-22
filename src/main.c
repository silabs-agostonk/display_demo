/*
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
#include "app_graphics.h"


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


#define BYTES_PER_PIXEL 2


#define MARKER_RADIUS 5
#define MARKER_BUF_DIM 21
uint16_t marker_draw_buffer [MARKER_BUF_DIM * MARKER_BUF_DIM];

// Variables to check marker borders
#define MARKER_DRAW_BUFFER_BORDER_ELEMENTS 24
const struct int16_xy_pair marker_draw_buffer_border [] = {
	{-2,-4}, {-1,-4}, {0,-4}, {1,-4}, {2,-4},
	{-3,-3}, {3,-3},
	{-4,-2}, {4,-2},
	{-4,-1}, {4,-1},
	{-4,0}, {4,0},
	{-4,1}, {4,1},
	{-4,2}, {4,2},
	{-3,3}, {3,3},
	{-2,4}, {-1,4}, {0,4}, {1,4}, {2,4}
};

// Variables to draw marker buffer
#define MARKER_DRAW_BUFFER_SEGMENT_ELEMENTS 3
const struct int16_xy_pair marker_draw_buffer_segment_xy [] = {
	{-3,-3},
	{-2,-4},
	{-4,-2}
};
const struct int16_xy_pair marker_draw_buffer_segment_dimensions [] = {
	{7,7},
	{5,9},
	{9,5}
};

/*

uint16_t display_draw_marker_unused(const struct device *display_dev, uint16_t x, uint16_t y, uint16_t bg_color){
	static uint16_t marker_buf [9 * 5]; // be aware max size!
	size_t marker_buf_elements = 45;
	struct display_buffer_descriptor marker_buf_desc;
	int ret;

	if (marker_buf[0] != bg_color){
		for (size_t i = 0; i < marker_buf_elements; i++) {
    		marker_buf[i] = bg_color;
		}
	}
	static const struct int16_xy_pair xy_coordinates [] = {{-2,-4}, {-3,-3}, {-4,-2}, {-3,3}, {-2,4}};
	static const struct int16_xy_pair xy_dimensions [] = {{5,1},{7,1},{9,5},{7,1},{5,1}};
	size_t xy_shapes = 5;

	marker_buf_desc.frame_incomplete=true;

	for (size_t i=0; i< xy_shapes; i++){
		marker_buf_desc.pitch = xy_dimensions[i].x;
		marker_buf_desc.width = xy_dimensions[i].x;
		marker_buf_desc.height = xy_dimensions[i].y;
		marker_buf_desc.buf_size = xy_dimensions[i].x * xy_dimensions[i].y * sizeof(uint16_t);


		ret = display_write(display_dev, xy_coordinates[i].x + x, xy_coordinates[i].y + y, &marker_buf_desc, marker_buf);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return 1;
		}
	
	}
	return 0;
}

uint16_t display_draw_marker_faster_unused(const struct device *display_dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t bg_color, uint16_t mk_color){
	static const int8_t circle9[13 * 13] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0,
		0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
		0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
		0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
		0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
		0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
		0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
		0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
		0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	int16_t dx = x1 - x0;
	int16_t dy = y1 - y0;

	int ret;

	uint16_t *pixel_buf = &mk_color;
	struct display_buffer_descriptor pixel_buf_desc;

	pixel_buf_desc.pitch = 1;
	pixel_buf_desc.width = 1;
	pixel_buf_desc.height = 1;
	pixel_buf_desc.buf_size = sizeof(uint16_t);


	//printk("%d %d\n", dx, dy);
	//for (uint16_t i = 0; i < length; i++)
	//printk("%d ", circle9[3 + 5 * 13]);
	//printk("\n");
	//return 0;

	// Iterating over the original array
	// from (1;1) to (11;11)
	for (int16_t y=1; y < 12; y++){
		for (int16_t x=1; x < 12; x++){
			int8_t a = circle9[ x + y * 13];
			int8_t b = circle9[(x - dx) + (y - dy) * 13];
			//printk("%d ", a-b);
			//circle9[0][0]
			//k_msleep(10);

			// Middle part of the two "circle"
			if (a-b == 0) continue;

			if (a-b < 0) pixel_buf = &bg_color;
			if (a-b > 0) pixel_buf = &mk_color;
			ret = display_write(display_dev, x0 + x - 6, y0 + y - 6, &pixel_buf_desc, pixel_buf);
			if (ret < 0) {
				LOG_ERR("Failed to write to display (error %d)", ret);
				return 1;
			}
		}
		//printk("\n");
	}
	return 0;

}

uint16_t display_marker_set_color(uint16_t mk_color, uint16_t line_color){

	// First fill the complete buffer with line color
	for (size_t y=0; y < MARKER_BUF_DIM; y++){
		for (size_t x=0; x < MARKER_BUF_DIM; x++){
			marker_draw_buffer [ x + y * MARKER_BUF_DIM ] = line_color;
		}
	}

	// Then draw marker in the middle of the buffer according to segments
	for (size_t i=0; i < MARKER_DRAW_BUFFER_SEGMENT_ELEMENTS; i++){
		for (size_t y=0; y < marker_draw_buffer_segment_dimensions[i].y; y++){
			for (size_t x=0; x < marker_draw_buffer_segment_dimensions[i].x; x++){
				marker_draw_buffer [
					marker_draw_buffer_segment_xy[i].x + MARKER_BUF_DIM / 2 + x + 
					(marker_draw_buffer_segment_xy[i].y + MARKER_BUF_DIM / 2 + y) * MARKER_BUF_DIM
				] = mk_color;
			}
		}
	}

	return 0;
}

uint16_t display_marker_draw(const struct device *display_dev, int16_t x0, int16_t y0, int16_t x1, int16_t y1){
	struct display_buffer_descriptor marker_buf_desc;
	int ret;
	static uint16_t marker_segment_buffer[8 * 8];

	int16_t dx = x1 - x0;
	int16_t dy = y1 - y0;

	
	//static int16_t dx_max = 0;
	//static int16_t dy_max = 0;
	//if (dx > dx_max) dx_max = dx;
	//if (dy > dy_max) dy_max = dy;
	//printk("%d %d\n", dx_max, dy_max);
	

	marker_buf_desc.frame_incomplete=true;

	// Draw line at (x0;y0)
	for (size_t i=0; i < MARKER_DRAW_BUFFER_SEGMENT_ELEMENTS; i++){
		marker_buf_desc.pitch = marker_draw_buffer_segment_dimensions[i].x;
		marker_buf_desc.width = marker_draw_buffer_segment_dimensions[i].x;
		marker_buf_desc.height = marker_draw_buffer_segment_dimensions[i].y;
		marker_buf_desc.buf_size = marker_draw_buffer_segment_dimensions[i].x * marker_draw_buffer_segment_dimensions[i].y * sizeof(uint16_t);

		// Copy part into segment buffer from the draw_buffer
		for (size_t y=0; y < marker_draw_buffer_segment_dimensions[i].y; y++){
			for (size_t x=0; x < marker_draw_buffer_segment_dimensions[i].x; x++){
				marker_segment_buffer [x + y * marker_draw_buffer_segment_dimensions[i].x] = 
					marker_draw_buffer [
						marker_draw_buffer_segment_xy[i].x + x + MARKER_BUF_DIM / 2 - dx +
						( marker_draw_buffer_segment_xy[i].y + y + MARKER_BUF_DIM / 2 - dy) * MARKER_BUF_DIM
					];
			}
		}
		
		// And pass it to the driver
		ret = display_write(display_dev, marker_draw_buffer_segment_xy[i].x + x0, marker_draw_buffer_segment_xy[i].y + y0, &marker_buf_desc, marker_segment_buffer);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return 1;
		}
	}

	// Draw marker at (x1;y1)
	for (size_t i=0; i < MARKER_DRAW_BUFFER_SEGMENT_ELEMENTS; i++){
		marker_buf_desc.pitch = marker_draw_buffer_segment_dimensions[i].x;
		marker_buf_desc.width = marker_draw_buffer_segment_dimensions[i].x;
		marker_buf_desc.height = marker_draw_buffer_segment_dimensions[i].y;
		marker_buf_desc.buf_size = marker_draw_buffer_segment_dimensions[i].x * marker_draw_buffer_segment_dimensions[i].y * sizeof(uint16_t);

		// Copy part into segment buffer from the draw_buffer
		for (size_t y=0; y < marker_draw_buffer_segment_dimensions[i].y; y++){
			for (size_t x=0; x < marker_draw_buffer_segment_dimensions[i].x; x++){
				marker_segment_buffer [x + y * marker_draw_buffer_segment_dimensions[i].x] =
					marker_draw_buffer [
						marker_draw_buffer_segment_xy[i].x + x + MARKER_BUF_DIM / 2 +
						(  marker_draw_buffer_segment_xy[i].y + y + MARKER_BUF_DIM / 2) * MARKER_BUF_DIM];
			}
		}
		
		// And pass it to the driver
		ret = display_write(display_dev, marker_draw_buffer_segment_xy[i].x + x1, marker_draw_buffer_segment_xy[i].y + y1, &marker_buf_desc, marker_segment_buffer);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return 1;
		}
	}
		

	return 0;
}

uint16_t display_update_from_canvas(const struct device *display_dev, int16_t x0, int16_t y0, uint16_t *pixel_data_b16, uint16_t mk_color, uint16_t line_color){
	// 1. (clean buffer)
	// not needed, since we overwrite it completly
	// 3. draw line where exist
	// 4. draw marker
	// 5. send it to the display
	
	// 2. copy background to the buffer
	// 3. draw line where exist
	// (x,y) is the top left corner of buffer
	for (size_t y=0; y < MARKER_BUF_DIM; y++){
		for (size_t x=0; x < MARKER_BUF_DIM; x++){
			// Where already exist line
			if (canvas_get_pixel_inline(x0 + x - MARKER_BUF_DIM / 2, y0 + y - MARKER_BUF_DIM / 2)){
				marker_draw_buffer [ x + y * MARKER_BUF_DIM ] = line_color;
			}
			// Where not, fill with background
			else{
				marker_draw_buffer [ x + y * MARKER_BUF_DIM ] = sys_cpu_to_be16(pixel_data_b16[ (x0 + x - MARKER_BUF_DIM / 2) + (y0 + y - MARKER_BUF_DIM / 2) * DISPLAY_W]);
			}
		}
	}

	// 4. draw marker at (x0;y0) which is in the middle of marker_draw_buffer
	// (sx, sy) is now relative to the middle of the buffer (MARKER_BUF_DIM / 2)
	for (size_t i=0; i < MARKER_DRAW_BUFFER_SEGMENT_ELEMENTS; i++){

		// Draw marker segment into the buffer
		for (int16_t sy=0; sy < marker_draw_buffer_segment_dimensions[i].y; sy++){
			for (int16_t sx=0; sx < marker_draw_buffer_segment_dimensions[i].x; sx++){
				marker_draw_buffer [
					(MARKER_BUF_DIM / 2 + sx + marker_draw_buffer_segment_xy[i].x ) +
					(MARKER_BUF_DIM / 2 + sy + marker_draw_buffer_segment_xy[i].y ) * MARKER_BUF_DIM
				] = mk_color;
			}
		}
	}

	// 5. send it to the display
	struct display_buffer_descriptor marker_buf_desc;
	int ret;
	marker_buf_desc.pitch = MARKER_BUF_DIM;
	marker_buf_desc.width = MARKER_BUF_DIM;
	marker_buf_desc.height = MARKER_BUF_DIM;
	marker_buf_desc.buf_size = MARKER_BUF_DIM * MARKER_BUF_DIM * sizeof(uint16_t);

	//for (size_t x=0; x < 100; x++){
	ret = display_write(display_dev, x0 - MARKER_BUF_DIM / 2, y0 - MARKER_BUF_DIM / 2, &marker_buf_desc, marker_draw_buffer);
	if (ret < 0) {
		LOG_ERR("Failed to write to display (error %d)", ret);
		return 1;
	}

	return 0;
}

// Clean the display with the pre defined color
uint16_t display_load_bg_simple_color(const struct device *display_dev, uint16_t bg_color){
	//struct display_capabilities capabilities;
	uint16_t *display_row_buf;
	struct display_buffer_descriptor display_row_buf_desc;
	int ret;

	// Get display size
	//display_get_capabilities(display_dev, &capabilities);
	display_row_buf = k_malloc(DISPLAY_W * sizeof(uint16_t));
	if (display_row_buf == NULL) {
		LOG_ERR("Failed to allocate buffer for cleaning the screen.");
		return 0;
	}

	// Fill the buffer with background color:
	for (size_t i = 0; i < DISPLAY_W; i++) {
    	display_row_buf[i] = bg_color;
	}

	// Set buffer descriptor parameters
	display_row_buf_desc.frame_incomplete=true;
	display_row_buf_desc.pitch = DISPLAY_W;
	display_row_buf_desc.width = DISPLAY_W;
	display_row_buf_desc.height = 1;
	display_row_buf_desc.buf_size = DISPLAY_W * sizeof(uint16_t);

	for (size_t y=0; y<DISPLAY_H; y++){
		ret = display_write(display_dev, 0, y, &display_row_buf_desc, display_row_buf);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return 1;
		}
	}

	k_free(display_row_buf);

	return 0;
}

uint16_t display_load_bg_pixmap_unused(const struct device *display_dev, const c_image * image_data){
	int ret;
	struct display_buffer_descriptor pixel_data_buf_desc;

	pixel_data_buf_desc.frame_incomplete=true;
	pixel_data_buf_desc.pitch = image_data->width;
	pixel_data_buf_desc.width = image_data->width;
	pixel_data_buf_desc.height = image_data->height;
	pixel_data_buf_desc.buf_size = image_data->width * image_data->height * sizeof(uint16_t);


	ret = display_write(display_dev, 0, 0, &pixel_data_buf_desc, image_data->pixel_data);
	if (ret < 0) {
		LOG_ERR("Failed to write to display (error %d)", ret);
		return 1;
	}
	return 0;
}

uint16_t display_load_bg_pixmap_with_conversion(const struct device *display_dev, uint16_t *pixel_data_b16){
	//struct display_capabilities capabilities;
	uint16_t *display_row_buf;
	struct display_buffer_descriptor display_row_buf_desc;
	int ret;

	// Get display size
	//display_get_capabilities(display_dev, &capabilities);
	display_row_buf = k_malloc(DISPLAY_W * sizeof(uint16_t));
	if (display_row_buf == NULL) {
		LOG_ERR("Failed to allocate buffer for cleaning the screen.");
		return 0;
	}

	// Set buffer descriptor parameters
	display_row_buf_desc.frame_incomplete=true;
	display_row_buf_desc.pitch = DISPLAY_W;
	display_row_buf_desc.width = DISPLAY_W;
	display_row_buf_desc.height = 1;
	display_row_buf_desc.buf_size = DISPLAY_W * sizeof(uint16_t);

	for (size_t y=0; y < DISPLAY_H; y++){
		for (size_t x=0; x < DISPLAY_W; x++){
			display_row_buf[x] = sys_cpu_to_be16(pixel_data_b16[x + y*DISPLAY_W]);
		}
		ret = display_write(display_dev, 0, y, &display_row_buf_desc, display_row_buf);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return 1;
		}
	}

	k_free(display_row_buf);
	return 0;
}

uint16_t display_is_the_marker_touching_pixmap(uint16_t current_x, uint16_t current_y, uint16_t *pixel_data_b16){
	uint16_t x, y;

	for (size_t i=0; i < MARKER_DRAW_BUFFER_BORDER_ELEMENTS; i++){
		x = current_x + marker_draw_buffer_border[i].x;
		y = current_y + marker_draw_buffer_border[i].y;

		if (pixel_data_b16[y * DISPLAY_W + x] != 0x0000){
			// If not black, return the touched color:
			return sys_cpu_to_be16(pixel_data_b16[y * DISPLAY_W + x]);
		}
	}

	// If nothing touched, return BLACK value
	return 0x0000;
}

*/


enum states_t {
  start,
  load_game,
  run_game,
  finish_game
};


int main(void) {

	uint16_t bg_color=sys_cpu_to_be16(COLOR_RGB_565_BLACK);
	uint16_t mk_color=sys_cpu_to_be16(COLOR_RGB_565_RED);
	uint16_t line_color=sys_cpu_to_be16(COLOR_RGB_565_YELLOW);
	



	enum states_t app_state = start;

	struct int16_xy_pair marker_pos_actual;
	struct int16_xy_pair marker_pos_new;

	struct mouse_data_element mouse_data_new_element;

	LOG_INF("Starting BLE Maze game");

	// Initialize input queue to receive data from BLE task to main task
	app_input_init();

	app_graphics_init();

	load_background(0);

	draw_marker();

	return 0;

	LOG_INF("01");
	//display_blanking_on(display_dev);
	LOG_INF("02");


	k_msleep(500);
	LOG_INF("03");
	ble_hid_app_start();
	LOG_INF("04");
/*
	while (1){

		switch (app_state){
			case start:
			display_blanking_on(display_dev);
			// Prepare marker colors
			display_marker_set_color(mk_color, line_color);
			app_state = load_game;
			break;

			case load_game:
			// Load background
			current_background = game_backgrounds[game_background_id];
			canvas_init();
			display_load_bg_pixmap_with_conversion(display_dev, current_background);
			display_blanking_off(display_dev);

			marker_pos_actual.x = 15;
			marker_pos_actual.y = 160;
			display_marker_draw(display_dev, marker_pos_actual.x, marker_pos_actual.y, marker_pos_actual.x, marker_pos_actual.y);

			app_input_flush();

			app_state = run_game;
			break;

			case run_game:

			if (app_input_get_mouse(&mouse_data_new_element, K_FOREVER) == 0){

				if (mouse_data_new_element.left_button){
					// Clean the actual game
					app_state = load_game;
				}
				else if (mouse_data_new_element.right_button){
					// Jump to the next game
					app_state = load_game;
					game_background_id++;
					if (game_background_id == BACKGROUND_ELEMENTS) game_background_id = 0;
				}
				// Calculate new position
				marker_pos_new.x = marker_pos_actual.x + mouse_data_new_element.dx;
				marker_pos_new.y = marker_pos_actual.y + mouse_data_new_element.dy;
				
				// Keep new position within display
				if (marker_pos_new.x > DISPLAY_W - MARKER_BUF_DIM / 2 - 1) marker_pos_new.x = DISPLAY_W - MARKER_BUF_DIM / 2 - 1;
				if (marker_pos_new.x < MARKER_BUF_DIM / 2 + 1) marker_pos_new.x = MARKER_BUF_DIM / 2 + 1;

				if (marker_pos_new.y > DISPLAY_H - MARKER_BUF_DIM / 2 - 1) marker_pos_new.y = DISPLAY_H - MARKER_BUF_DIM / 2 - 1;
				if (marker_pos_new.y < MARKER_BUF_DIM / 2 + 1) marker_pos_new.y = MARKER_BUF_DIM / 2 + 1;
				
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

					uint16_t touched_color = display_is_the_marker_touching_pixmap(x0, y0, current_background);
					if (touched_color == sys_cpu_to_be16(COLOR_RGB_565_GREEN)){
						LOG_INF("Drawing: touched finish color");
						app_state = finish_game;
						break;
					}

					if(touched_color != bg_color){
						LOG_INF("Drawing: touched wall");
						// it would be okay to stop here, but try to move only one axes
						app_input_flush();
						
						// move only by X
						if (!display_is_the_marker_touching_pixmap(x0, marker_pos_actual.y, current_background)){
							//display_marker_draw(display_dev, marker_pos_actual.x, marker_pos_actual.y, x0, marker_pos_actual.y);
							canvas_draw_line(x0, marker_pos_actual.y);
							display_update_from_canvas(display_dev, x0, marker_pos_actual.y, current_background, mk_color, line_color);

							marker_pos_actual.x = x0;
							break;
						}

						// move only by Y
						else if (!display_is_the_marker_touching_pixmap(marker_pos_actual.x, y0, current_background)){
							//display_marker_draw(display_dev, marker_pos_actual.x, marker_pos_actual.y, marker_pos_actual.x, y0);
							canvas_draw_line(marker_pos_actual.x, y0);
							display_update_from_canvas(display_dev, marker_pos_actual.x, y0, current_background, mk_color, line_color);

							marker_pos_actual.y = y0;
							break;
						}

						else {
							// nothing left to do here
						}
						
						break;
					}
					else {
						canvas_draw_line(x0, y0);
						if (k_step_cnt % 5 == 0){
							//display_marker_draw(display_dev, marker_pos_actual.x, marker_pos_actual.y, x0, y0);
							display_update_from_canvas(display_dev, x0, y0, current_background, mk_color, line_color);
							marker_pos_actual.x = x0;
							marker_pos_actual.y = y0;
						}
					}
					k_step_cnt++;
				}
			}

			break;

			case finish_game:
			// Would be great to display some text
			k_msleep(500);
			
			game_background_id++;
			if (game_background_id == BACKGROUND_ELEMENTS) game_background_id = 0;

			app_state = load_game;
			break;

			default:
			app_state = start;
        	break;
		}

	}

	*/
	return 0;
}
