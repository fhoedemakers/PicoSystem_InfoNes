# InfoNes a NES emulator for the Pimoroni PicoSystem RP2040 gaming handheld
Now you can play Nintendo NES games on the [Pimoroni PicoSystem](https://shop.pimoroni.com/products/picosystem) RP2040 gaming handheld.

![image](https://github.com/fhoedemakers/PicoSystem_InfoNes/blob/master/assets/PicoSystem.jpg)

Click on image below to see a demo video.

[![Video](https://img.youtube.com/vi/4VYKSMvYWc8/0.jpg)](https://www.youtube.com/watch?v=4VYKSMvYWc8)

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
