# InfoNes a NES emulator for the Pimoroni PicoSystem handheld


This is a work in progress.

- Colors are somewhat off
- Audio has to be implemented

## Button maps

- Y: SELECT
- X: START
- Y + X: Reset

## Uploading Roms
Load roms by setting the device in BOOTSEL mode. (Connect to computer then Hold X and power on device)
The ROM should be placed at address 0x10080000, and can be  transferred using [picotool](https://github.com/raspberrypi/picotool).
```
picotool load foo.nes -t bin -o 0x10080000
```
