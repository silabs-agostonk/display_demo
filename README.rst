Overview
********


Zephyr demo with SPI display and BLE mouse



Other info
********

It works on siwx917_dk2605a and xg24_dk2601b devices on most recent version of upstream.

On upstream::

   git checkout main
   git pull
   west update
   west blobs fetch hal_silabs

917 NWP firmware

* take SiWG917-B.2.15.5.2.0.2.rps (or similar)
* from https://github.com/SiliconLabs/wiseconnect/tree/master/connectivity_firmware/standard
* or from zephyrproject/modules/hal/silabs/zephyr/blobs/wiseconnect/connectivity_firmware/standard/SiWG917-B.2.15.5.2.0.2.rps

::

   west build -p -b siwx917_dk2605a ./app/display/ -d ./build_917/
   west flash -d ./build_917/ --dev-id 446000408

::

   west build -p -b xg24_dk2601b ./app/display/ -d ./build_efr/
   west flash -d ./build_efr/ --id 440333926


Limits, known bugs:
********
- Currently works only with Logitech M196 BLE mouses. There is no HID table interpretation, so HID Report data interpretation callback may have to changed to use other mice.
- after some time connection drops (due to mouse sleep), and the connection does not populates in every case. In this scenario, you have to reset the mice.
- if 917 cannot rebond, needs to erase the flash and flash again (settings loading)
- both has log on VCOM (115200) to check what happens in background


First usage, after start
********
- push BTN1 on the board to enable pairing
- push the button on mouse for 3sec to enable pairing mode

SPI connection
********

- DLC0347BWP00SF-1 display connector: https://www.dlcdisplay.com/viewfilebizce/1760130809082957824/DLC0347BWP00SF-1.pdf
- EXP pins are the connectors of devkits


In case of EFR BRD2601B
===================

::

                           +-----------+
                           |  DLC0347  |
                     +-----+-----+-----+-----+
                     |     |  2  |  1  | VCC | ------ (3.3) EXP20
                     +-----+-----+-----+-----+
                     |     |  4  |  3  |     |
                     ------+-----+-----+-----+
  EXP01 (GND) ------ | GND |  6  |  5  |     |
                     +-----+-----+-----+-----+
                     |     |  8  |  7  |     |
                     +-----+-----+-----+-----+
                     |     | 10  |  9  |     |
                     +-----+-----+-----+-----+
                     |     | 12  | 11  |     |
                     +-----+-----+-----+-----+
                     |     | 14  | 13  | RES | ------ (PD02) EXP13
                     +-----+-----+-----+-----+
                     |     | 16  | 15  |     |
                     +-----+-----+-----+-----+
                     | BLK | 18  | 17  |     |
                     +-----+-----+-----+-----+
                     |     | 20  | 19  | SDA | ------ (SPI_COPI/PC03) EXP04
                     +-----+-----+-----+-----+
  EXP09 (PB00) ----- | DC  | 22  | 21  |     |
                     +-----+-----+-----+-----+
  EXP10 (PA07) ----- | CS  | 24  | 23  | SCL | ------ (SPI_CLK/PC01) EXP08
                     +-----+-----+-----+-----+
                     |     | 26  | 25  |     |
                     +-----+-----+-----+-----+




In case of 917 BRD2605A
===================

::

                           +-----------+
                           |  DLC0347  |
                     +-----+-----+-----+-----+
                     |     |  2  |  1  | VCC | ------ (3.3) EXP20
                     +-----+-----+-----+-----+
                     |     |  4  |  3  |     |
                     ------+-----+-----+-----+
  EXP01/26 (GND) --- | GND |  6  |  5  |     |
                     +-----+-----+-----+-----+
                     |     |  8  |  7  |     |
                     +-----+-----+-----+-----+
                     |     | 10  |  9  |     |
                     +-----+-----+-----+-----+
                     |     | 12  | 11  |     |
                     +-----+-----+-----+-----+
                     |     | 14  | 13  | RES | ------ EXP23
                     +-----+-----+-----+-----+
                     |     | 16  | 15  |     |
                     +-----+-----+-----+-----+
                     | BLK | 18  | 17  |     |
                     +-----+-----+-----+-----+
                     |     | 20  | 19  | SDA | ------ EXP07
                     +-----+-----+-----+-----+
  EXP21 ------------ | DC  | 22  | 21  |     |
                     +-----+-----+-----+-----+
  EXP09 ------------ | CS  | 24  | 23  | SCL | ------ EXP03
                     +-----+-----+-----+-----+
                     |     | 26  | 25  |     |
                     +-----+-----+-----+-----+




Graphics
********


Some words about the app drawings.

The maze patterns are stored in src/bg_*.c files, generated in this way:
- go to page: https://codebox.net/pages/maze-generator/online
- save the generated picture
- scale and edit in Gimp to fit the display
- add some green part, which will trigger game finished state and load a new pattern
- export it to c file, in 16 bit format (like bt_square.c)
- Gimp creates static variable (has to be changed global)

There is not enough RAM to store the complete frame buffer so drawing follows these steps:
- loads the complete image from flash and writes to the display (only once)
- there is a 1bit/pixel buffer in RAM to store mouse movement line (canvas)
- if the mouse moves, the round marker and the surrondings is updated with one write transaction:
   - with the background
   - with marker line 
   - with marker itself

The buffer wich updates the screen looks like this:

::
  
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |     | -7  | -6  | -5  | -4  | -3  | -2  | -1  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -7  |     |     |     |     |     |     |     |     |     |     |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -6  |     |     |     |     |     |     |     |     |     |     |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -5  |     |     |     |     |     |     |     |     |     |     |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -4  |     |     |     |     |     |  #  |  #  |  #  |  #  |  #  |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -3  |     |     |     |     |  #  |     |     |     |     |     |  #  |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -2  |     |     |     |  #  |     |     |     |     |     |     |     |  #  |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  | -1  |     |     |     |  #  |     |     |     |     |     |     |     |  #  |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  0  |     |     |     |  #  |     |     |     |  x  |     |     |     |  #  |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  1  |     |     |     |  #  |     |     |     |     |     |     |     |  #  |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  2  |     |     |     |  #  |     |     |     |     |     |     |     |  #  |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  3  |     |     |     |     |  #  |     |     |     |     |     |  #  |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  4  |     |     |     |     |     |  #  |  #  |  #  |  #  |  #  |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  5  |     |     |     |     |     |     |     |     |     |     |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  6  |     |     |     |     |     |     |     |     |     |     |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
  |  7  |     |     |     |     |     |     |     |     |     |     |     |     |     |     |     |
  +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+



- x is the current mouse position; all other part is calculated relative to this
- marker_draw_buffer_border:
  - used used to check as well the touching logic (for walls and green finish line)
- marker_draw_buffer_segment_xy and marker_draw_buffer_segment_dimensions:
  - rectangles to cover a circle
