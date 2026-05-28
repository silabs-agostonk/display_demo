
#include "app_graphics.h"
#include "app_graphics_marker.h"
#include "app_display.h"
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_graphics, LOG_LEVEL_INF);


const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
struct display_capabilities display_dev_capabilities; // can be local in init func?

/* Canvas to store the drawn line in background
   It makes possible to use different color
   for the drawn line and for marker */
uint8_t canvas_bitmap [(DISPLAY_W * DISPLAY_H) / 8] = {0};
void canvas_clean();
uint8_t canvas_draw(uint16_t x0, uint16_t y0);
uint16_t canvas_set_pixel(uint16_t x, uint16_t y);
uint8_t canvas_get_pixel(uint16_t x, uint16_t y);
static inline uint8_t canvas_get_pixel_inline(uint16_t x, uint16_t y);


enum display_pixel_format display_dev_pixel_format; // maybe not needed
size_t display_dev_bits_per_pixel=0;

/* One line buffer for display: for updating the complete screen faster
   Allocated once dynamically in app_graphics_init
*/
void *display_dev_row_buf;
struct display_buffer_descriptor display_dev_row_buf_desc;
uint8_t init_display_dev_row_buf();


/* Working buffer to draw the marker and its surrondings when marker is moved.
   Allocated once dynamivally in app_graphics_init
   Its bigger then the marker itself, which makes possible to skip some display
   refreshing and make the app overall more responsable
   Size depends on: marker width and surrondings space
*/
void *marker_draw_buf;
struct display_buffer_descriptor marker_draw_buf_desc;
uint8_t init_marker_draw_buf_mono();
uint8_t init_marker_draw_buf_rgb565();


size_t game_background_id = 0;
uint16_t* game_backgrounds[BACKGROUND_ELEMENTS];
uint16_t* current_background;

int (*load_background)();
int load_background_mono();
int load_background_rgb565();

int (*draw_marker)(uint16_t, uint16_t);
int draw_marker_mono(uint16_t x, uint16_t y);
int draw_marker_rgb565(uint16_t x, uint16_t y);

uint16_t bg_color;
uint16_t mk_color;
uint16_t line_color;
uint16_t finish_color;

uint8_t app_graphics_init(){
	
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.",
			display_dev->name);
		return 0;
	}

 	display_get_capabilities(display_dev, &display_dev_capabilities);
	LOG_INF("Display capabilities: %dx%d | Supported formats: %04x | Current format: %04x",
        display_dev_capabilities.x_resolution, display_dev_capabilities.y_resolution,
        display_dev_capabilities.supported_pixel_formats,
        display_dev_capabilities.current_pixel_format);


    switch (display_dev_capabilities.current_pixel_format) {
    case PIXEL_FORMAT_MONO01:
    case PIXEL_FORMAT_MONO10:
        display_dev_bits_per_pixel = 1;
        load_background = load_background_mono;
        draw_marker = draw_marker_mono;
		init_marker_draw_buf_mono();
		init_display_dev_row_buf();

		bg_color=sys_cpu_to_be16(COLOR_RGB_565_BLACK);
		mk_color=sys_cpu_to_be16(COLOR_RGB_565_WHITE);
		line_color = sys_cpu_to_be16(COLOR_RGB_565_WHITE);
		finish_color = sys_cpu_to_be16(COLOR_RGB_565_GREEN);

        break;
    case PIXEL_FORMAT_RGB_565:
        display_dev_bits_per_pixel = 16;

        load_background = load_background_rgb565;
        draw_marker = draw_marker_rgb565;
		init_marker_draw_buf_rgb565();
		init_display_dev_row_buf();

		bg_color=sys_cpu_to_be16(COLOR_RGB_565_BLACK);
		mk_color=sys_cpu_to_be16(COLOR_RGB_565_RED);
		line_color = sys_cpu_to_be16(COLOR_RGB_565_YELLOW);
		finish_color = sys_cpu_to_be16(COLOR_RGB_565_GREEN);

        break;
    
    default:
		LOG_ERR("The current display format is not supported.");
        return -1;
    }

    game_backgrounds [0] = (uint16_t*) bg_square.pixel_data;
    game_backgrounds [1] = (uint16_t*) bg_triangle.pixel_data;
    game_backgrounds [2] = (uint16_t*) bg_hex.pixel_data;
	//game_backgrounds [3] = (uint16_t*) bg_test.pixel_data;

	game_background_id = 0;
    current_background = game_backgrounds[game_background_id];

    return 0;

}

void next_background(){
	game_background_id++;
	game_background_id %= BACKGROUND_ELEMENTS;
	current_background = game_backgrounds[game_background_id];
}

uint8_t init_display_dev_row_buf(){
    display_dev_row_buf = k_malloc(DISPLAY_W * display_dev_bits_per_pixel / 8);
	if (display_dev_row_buf == NULL) {
        LOG_ERR("Failed to allocate buffer for cleaning the screen.");
		return -1;
	}

	// Set buffer descriptor parameters
	display_dev_row_buf_desc.frame_incomplete=true;
	display_dev_row_buf_desc.pitch = DISPLAY_W;
	display_dev_row_buf_desc.width = DISPLAY_W;
	display_dev_row_buf_desc.height = 1;
	display_dev_row_buf_desc.buf_size = DISPLAY_W * display_dev_bits_per_pixel / 8;

    return 0;
}

uint8_t init_marker_draw_buf_mono(){
    marker_draw_buf = k_malloc(DISPLAY_W * MARKER_BUF_MIN_HEIGHT * display_dev_bits_per_pixel / 8);
	if (marker_draw_buf == NULL) {
        LOG_ERR("Failed to allocate buffer for drawing marker.");
		return -1;
	}

	// Set buffer descriptor parameters
	marker_draw_buf_desc.frame_incomplete=true;
	marker_draw_buf_desc.pitch = DISPLAY_W;
	marker_draw_buf_desc.width = DISPLAY_W; // has to aligned to display width!
	marker_draw_buf_desc.height = MARKER_BUF_MIN_HEIGHT;
	marker_draw_buf_desc.buf_size = DISPLAY_W * MARKER_BUF_MIN_HEIGHT * display_dev_bits_per_pixel / 8;

    return 0;
}

uint8_t init_marker_draw_buf_rgb565(){
    marker_draw_buf = k_malloc(MARKER_BUF_DIM * MARKER_BUF_DIM * display_dev_bits_per_pixel / 8);
	if (marker_draw_buf == NULL) {
        LOG_ERR("Failed to allocate buffer for cleaning the screen.");
		return -1;
	}

	// Set buffer descriptor parameters
	marker_draw_buf_desc.frame_incomplete=true;
	marker_draw_buf_desc.pitch = MARKER_BUF_DIM;
	marker_draw_buf_desc.width = MARKER_BUF_DIM;
	marker_draw_buf_desc.height = MARKER_BUF_DIM;
	marker_draw_buf_desc.buf_size = MARKER_BUF_DIM * MARKER_BUF_DIM * display_dev_bits_per_pixel / 8;

    return 0;
}

void canvas_clean(){
	memset(canvas_bitmap, 0x00, (DISPLAY_W * DISPLAY_H) / 8);
}

uint8_t canvas_draw(uint16_t x0, uint16_t y0){
    int16_t w = LINE_WIDTH / 2;
	for(int16_t y = -w; y <= w; y++){
		for(int16_t x = -w; x <= w; x++){
			canvas_set_pixel(x0 + x, y0 + y);
		}
	}
}


uint16_t canvas_set_pixel(uint16_t x, uint16_t y){
	if (x >= DISPLAY_W || y >= DISPLAY_H)
        return 1;  // bounds protection

	uint32_t bit_index  = (uint32_t)y * DISPLAY_W + x;
	uint32_t byte_index = bit_index >> 3;      // divide by 8
    uint8_t  bit_offset = bit_index & 0x07;    // mod 8

    canvas_bitmap[byte_index] |= (1u << bit_offset);
	return 0;
}

uint8_t canvas_get_pixel(uint16_t x, uint16_t y){
	if (x >= DISPLAY_W || y >= DISPLAY_H)
        return 0;  // bounds protection

	uint32_t bit_index  = (uint32_t)y * DISPLAY_W + x;
	uint32_t byte_index = bit_index >> 3;      // divide by 8
    uint8_t  bit_offset = bit_index & 0x07;    // mod 8

	return (canvas_bitmap[byte_index] >> bit_offset) & 1u;
}

static inline uint8_t canvas_get_pixel_inline(uint16_t x, uint16_t y){
    return (canvas_bitmap[y * (DISPLAY_W / 8) + (x >> 3)] >> (x & 7)) & 1u;
}

static inline bool rgb565_to_mono(uint16_t rgb565)
{
	uint8_t r = (rgb565 >> 11) & 0x1F;
	uint8_t g = (rgb565 >> 5)  & 0x3F;
	uint8_t b = rgb565 & 0x1F;

	/* Convert to rough 8-bit luma.
	 * Expand:
	 *   r: 5-bit -> 8-bit
	 *   g: 6-bit -> 8-bit
	 *   b: 5-bit -> 8-bit
	 */
	uint8_t r8 = (r << 3) | (r >> 2);
	uint8_t g8 = (g << 2) | (g >> 4);
	uint8_t b8 = (b << 3) | (b >> 2);

	/* Approximate brightness: 0..255 */
	uint16_t y = (uint16_t)((30 * r8 + 59 * g8 + 11 * b8) / 100);

	/* true means white/on, false means black/off */
	return y >= 128;
}

static inline bool rgb565_to_mono_dither(uint16_t rgb565, size_t x, size_t y)
{
    static const uint8_t bayer4x4[4][4] = {
        {  0,  8,  2, 10 },
        { 12,  4, 14,  6 },
        {  3, 11,  1,  9 },
        { 15,  7, 13,  5 },
    };

	uint8_t r = (rgb565 >> 11) & 0x1F;
	uint8_t g = (rgb565 >> 5)  & 0x3F;
	uint8_t b = rgb565 & 0x1F;

	uint8_t r8 = (r << 3) | (r >> 2);
	uint8_t g8 = (g << 2) | (g >> 4);
	uint8_t b8 = (b << 3) | (b >> 2);

	/* Luma, approximately 0..255 */
	uint16_t luma = (30 * r8 + 59 * g8 + 11 * b8) / 100;

	/* Convert Bayer value 0..15 to threshold around 0..255 */
	uint8_t threshold = bayer4x4[y & 3][x & 3] * 16 + 8;

	return luma > threshold;
}


int load_background_mono(){
    uint16_t *pixel_data_b16 = current_background;
    uint8_t *row_buf = (uint8_t *)display_dev_row_buf;
    int ret;

    LOG_INF("Loading background for Mono display.");

    for (size_t y=0; y < DISPLAY_H; y++){
        memset(row_buf, 0, display_dev_row_buf_desc.buf_size);

		for (size_t x=0; x < DISPLAY_W; x++){
			uint16_t pixel = pixel_data_b16[x + y*DISPLAY_W];

			if (rgb565_to_mono_dither(pixel, x, y)) {
				size_t byte_idx = x / 8;
				uint8_t bit_idx = x % 8; /* LSB first */

				row_buf[byte_idx] |= BIT(bit_idx);
			}
		}
		ret = display_write(display_dev, 0, y, &display_dev_row_buf_desc, row_buf);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return ret;
		}
	}
    return 0;
}

int load_background_rgb565(){
    uint16_t *pixel_data_b16 = current_background;
    uint16_t *row_buf = (uint16_t *)display_dev_row_buf;

    int ret;

    LOG_INF("Loading background for RGB565 display");
	for (size_t y=0; y < DISPLAY_H; y++){
		for (size_t x=0; x < DISPLAY_W; x++){
			row_buf[x] = (uint16_t) sys_cpu_to_be16(pixel_data_b16[x + y*DISPLAY_W]);
		}
		ret = display_write(display_dev, 0, y, &display_dev_row_buf_desc, row_buf);
		if (ret < 0) {
			LOG_ERR("Failed to write to display (error %d)", ret);
			return ret;
		}
	}
	return 0;
}

int draw_marker_mono(uint16_t x0, uint16_t y0){
    uint16_t *pixel_data_b16 = current_background;
    uint8_t *marker_buf = (uint8_t *)marker_draw_buf;
    int ret;

	LOG_INF("draw_marker_mono %d %d", x0, y0);

    // Fill marker buffer with canvas data
    // (or with background if there is no line)
    // (x,y) is the top left corner of buffer
    memset(marker_buf, 0, marker_draw_buf_desc.buf_size);
    for (size_t y=0; y < MARKER_BUF_MIN_HEIGHT; y++){
		for (size_t x=0; x < DISPLAY_W; x++){
            // Where already exist line
            uint16_t pixel; // its now rgb565!!!
			if (canvas_get_pixel_inline(x, y0 + y - MARKER_BUF_MIN_HEIGHT / 2)){
				pixel = line_color;
			}
			// Where not, get pixel color from background
			else{
                pixel = pixel_data_b16[ (x) + (y0 + y - MARKER_BUF_MIN_HEIGHT / 2) * DISPLAY_W];
			}

            // Convert and place it in marker buffer
            if (rgb565_to_mono_dither(pixel, x,  y0 + y - MARKER_BUF_MIN_HEIGHT / 2)) {
				size_t byte_idx = x / 8;
				uint8_t bit_idx = x % 8; /* LSB first */

				marker_buf[byte_idx + y * (marker_draw_buf_desc.width / 8)] |= BIT(bit_idx);
			}
		}
	}

    // Draw marker at (x0;y0) which is vertically in the middle of marker_draw_buffer

	// (sx, sy) is now relative to the middle of the buffer (MARKER_BUF_MIN_HEIGHT / 2)
	for (size_t i=0; i < MARKER_DRAW_BUFFER_SEGMENT_ELEMENTS; i++){
        // Iterate over segments
		// Draw one marker segment into the buffer
		for (int16_t sy=0; sy < marker_draw_buffer_segment_dimensions[i].y; sy++){
			for (int16_t sx=0; sx < marker_draw_buffer_segment_dimensions[i].x; sx++){

                // Convert and place it in marker buffer
				// display coordinates
				// x: x0 + sx + marker_draw_buffer_segment_xy[i].x
				// y: y0 + sy + marker_draw_buffer_segment_xy[i].y
                if (rgb565_to_mono_dither(mk_color, x0 + sx + marker_draw_buffer_segment_xy[i].x,  y0 + sy + marker_draw_buffer_segment_xy[i].y)) {
					// from here, it has to be buffer coordinates:
					int16_t buf_x = x0 + sx + marker_draw_buffer_segment_xy[i].x;
					int16_t buf_y = y0 + sy + marker_draw_buffer_segment_xy[i].y;

					int16_t bit_x = buf_x;
					int16_t bit_y = buf_y - (y0 - MARKER_BUF_MIN_HEIGHT / 2);

					size_t byte_idx = bit_x / 8;
					uint8_t bit_idx = bit_x % 8; /* LSB first */

					marker_buf[byte_idx + bit_y * (marker_draw_buf_desc.width / 8)] |= BIT(bit_idx);
                }
			}
		}
	}

    // Write buffer to display:
	ret = display_write(display_dev, 0, y0 - MARKER_BUF_MIN_HEIGHT / 2, &marker_draw_buf_desc, marker_buf);
	if (ret < 0) {
		LOG_ERR("Failed to write to display (error %d)", ret);
		return ret;
	}
    return 0;
}

int draw_marker_rgb565(uint16_t x0, uint16_t y0){
    // We have the marker_buffer: 
    void *marker_draw_buf;
    struct display_buffer_descriptor marker_draw_buf_desc;
    uint8_t init_marker_draw_buf();

}

touch_elements_t marker_touching(uint16_t x0, uint16_t y0){
	int16_t x, y;
	int16_t *pixel_data_b16 = current_background;

	for (size_t i=0; i < MARKER_DRAW_BUFFER_BORDER_ELEMENTS; i++){
		x = x0 + marker_draw_buffer_border[i].x;
		y = y0 + marker_draw_buffer_border[i].y;

		// touched_display_border
		// its also handled in game logic
		if (x < 0 || y < 0 || x > DISPLAY_W - 1 || y > DISPLAY_H - 1) return touched_display_border;

		// touched_finish_line
		if (sys_cpu_to_be16(pixel_data_b16[y * DISPLAY_W + x]) == finish_color){
			// If not black, return the touched color:
			return touched_finish_line;
		}
		// touched_wall
		else if (sys_cpu_to_be16(pixel_data_b16[y * DISPLAY_W + x]) != bg_color){
			// If not black, return the touched color:
			return touched_wall;
		}
	}

	// If nothing touched
	return touched_background;
}