/* app_types.h */
#ifndef APP_TYPES_H
#define APP_TYPES_H

struct int16_xy_pair {
    int16_t x;
    int16_t y;
};

struct mouse_data_element {
    int16_t dx;
    int16_t dy;
    bool left_button;
    bool right_button;
};

// Image struct, exported with GIMP
typedef struct {
  unsigned int 	 width;
  unsigned int 	 height;
  unsigned int 	 bytes_per_pixel; /* 2:RGB16, 3:RGB, 4:RGBA */ 
  char         	*comment;
  unsigned char	 pixel_data[480 * 320 * 2 + 1];
} c_image;

#endif /* APP_TYPES_H */