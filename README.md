# InfoNes a NES emulator for the Pimoroni PicoSystem RP2040 gaming handheld
Now you can play Nintendo NES games on the [Pimoroni PicoSystem](https://shop.pimoroni.com/products/picosystem) RP2040 gaming handheld.

This is a work in progress.

- Colors are somewhat off
- Sound has to be implemented

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
