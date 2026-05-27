/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_DISPLAY_H_
#define APP_DISPLAY_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif


#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
BUILD_ASSERT(DT_NODE_HAS_STATUS(DISPLAY_NODE, okay), "zephyr,display node is not okay");

#define DISPLAY_W DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_H DT_PROP(DISPLAY_NODE, height)

extern const struct device *display_dev;
extern struct display_capabilities display_dev_capabilities;



#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H_ */