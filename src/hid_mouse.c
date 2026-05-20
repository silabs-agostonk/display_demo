/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_mouse.h"

static int16_t sign_extend_12(uint16_t v)
{
	/* Helper function for Logitech MX with 12-bit signed data.
	 * If bit 11, the sign bit, is set, extend with 1s.
	 */
	if (v & 0x0800) {
		v |= 0xF000;
	}

	return (int16_t)v;
}

int hid_mouse_decode_logitech_m196(const uint8_t *data,
				   size_t length,
				   struct mouse_data_element *mouse_data)
{
	int16_t dx;
	int16_t dy;

	if ((data == NULL) || (mouse_data == NULL)) {
		return -EINVAL;
	}

	if (length < 5) {
		return -EMSGSIZE;
	}

	mouse_data->left_button = (data[0] & 0x01) != 0;
	mouse_data->right_button = (data[0] & 0x02) != 0;

	/*
	 * This is the right place to interpret the HID report table in the
	 * future.
	 */

	/*
	 * Logitech MX Master style 12-bit movement format.
	 * Kept here for future decoder selection support.
	 *
	 * uint16_t x12 = ((uint16_t)(data[3] & 0x0F) << 8) | data[2];
	 * uint16_t y12 = ((uint16_t)(data[4] << 4)) |
	 *		  ((data[3] >> 4) & 0x0F);
	 * dx = sign_extend_12(x12);
	 * dy = sign_extend_12(y12);
	 */

	/* Logitech M196: 1000 dpi */
	dx = (int16_t)(((uint16_t)data[2] << 8) | data[1]);
	dy = (int16_t)(((uint16_t)data[4] << 8) | data[3]);

	mouse_data->dx = dx;
	mouse_data->dy = dy;

	//ARG_UNUSED(sign_extend_12);

	return 0;
}