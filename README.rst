Overview
********


Zephyr demo with SPI display and BLE mouse



Other info
********

It works on siwx917_dk2605a and xg24_dk2601b devices with an 2026 January version. Earlier and later (other not yet released) versions can have incompatibility issues. (SPI, display driver and Wiseconnect SDK)

On upstream:

git checkout 24dc151e5d6650129b5b7c237998a204c39473e1
west update
west blobs fetch hal_silabs

917 NWP:
https://github.com/SiliconLabs/wiseconnect/tree/master/connectivity_firmware/standard
and take SiWG917-B.2.14.5.2.0.7.rps


west build -p -b siwx917_dk2605a .\app\display\ -d .\build_917\
west flash -d .\build_917\


west build -p -b xg24_dk2601b .\app\display\ -d .\build_efr\
west flash -d .\build_efr\

## Limits, known bugs:
- Currently works only with Logitech M196 BLE mouses. There is no HID table interpretation, so HID Report data interpretation callback may have to changed to use other mice.
- after some time connection drops (due to mouse sleep), and the connection does not populates in every case. In this scenario, you have to reset the mice.
- if 917 cannot rebond, needs to erase the flash and flash again (settings loading)
- both has log on VCOM (115200) to check what happens in background


## First usage
After start
- push BTN1 on the board to enable pairing
- push the button on mouse for 3sec to enable pairing mode

