/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>

#include "app_input.h"

#define APP_INPUT_MOUSE_QUEUE_LEN 10

static char mouse_data_queue_buffer[APP_INPUT_MOUSE_QUEUE_LEN *
				    sizeof(struct mouse_data_element)];
static struct k_msgq mouse_data_queue;

void app_input_init(void)
{
	k_msgq_init(&mouse_data_queue,
		    mouse_data_queue_buffer,
		    sizeof(struct mouse_data_element),
		    APP_INPUT_MOUSE_QUEUE_LEN);
}

int app_input_submit_mouse(const struct mouse_data_element *mouse_data)
{
	if (mouse_data == NULL) {
		return -EINVAL;
	}

	while (k_msgq_put(&mouse_data_queue, mouse_data, K_NO_WAIT) != 0) {
		/*
		 * Preserve the current policy:
		 * if the queue is full, drop old mouse data and keep the
		 * newest report.
		 */
		k_msgq_purge(&mouse_data_queue);
	}

	return 0;
}

int app_input_get_mouse(struct mouse_data_element *mouse_data,
			k_timeout_t timeout)
{
	if (mouse_data == NULL) {
		return -EINVAL;
	}

	return k_msgq_get(&mouse_data_queue, mouse_data, timeout);
}

void app_input_flush(void)
{
	k_msgq_purge(&mouse_data_queue);
}