/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HID_MOUSE_H_
#define HID_MOUSE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int hid_mouse_decode_logitech_m196(const uint8_t *data,
				   size_t length,
				   struct mouse_data_element *mouse_data);

#ifdef __cplusplus
}
#endif

#endif /* HID_MOUSE_H_ */