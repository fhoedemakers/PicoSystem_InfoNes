# PicoSystem_InfoNes a NES emulator for the Pimoroni PicoSystem RP2040 gaming handheld
``
!!!This is an experimentarl branch to support PWM audio from [Fhoedemakers github.](https://github.com/fhoedemakers/PicoSystem_InfoNes/releases)
``

Emulater is now bundled with the freeware homebrew game [Blade Buster](https://www.rgcd.co.uk/2011/05/blade-buster-nes.html).

[A pre-release .uf2 binary to flash on your picosystem can be found here.](https://github.com/fhoedemakers/PicoSystem_InfoNes/releases)

Now you can store and play Nintendo multiple NES games on the [Pimoroni PicoSystem](https://shop.pimoroni.com/products/picosystem) RP2040 gaming handheld. A menu system is included for choosing your games to play.

(Note: Screenshots below do not represent the actual picture quality. The distortion is caused by the camera)
![image](assets/gamescreen.jpeg)

![image](assets/menuscreen.jpeg)

Click on image below to see a demo video.

[![Video](https://img.youtube.com/vi/4VYKSMvYWc8/0.jpg)](https://www.youtube.com/watch?v=4VYKSMvYWc8)

 Things to do:

- Sound has to be implemented. Will be a challenge since the PicoSystem has as simple Piezo buzzer/speaker which has not the capabilities for generating proper sound from the NES. 
- Code has to be cleaned up. Uses parts of the [PicoSystem library](https://github.com/pimoroni/picosystem).


## Uploading games and flashing the emulator
Since there is no SD card slot available, a companion app for Microsoft Windows is created that lets the user choose games and flash them to the Pico.
[More information here](https://github.com/fhoedemakers/PicoSystemInfoNesLoader). The app is included in the latest release. 
The application also allows you to flash the emulator when not installed or a newer version is available. No more need for manually flashing.

![image](assets/Screen.png)

## Manually flashing the PicoSystem
- Download **PicoSystem_InfoNes.uf2** from the [releases page](https://github.com/fhoedemakers/PicoSystem_InfoNes/releases/latest).
- Connect PicoSystem using an USB-C cable to your computer. Make sure the PicoSystem is switched off.
- Push and hold the X button then the power-on button. Release the buttons and the drive RPI-RP2 appears on your computer.
- Drag and drop the UF2 file on to the RPI-RP2 drive. The PicoSystem will reboot and will now run the emulator.

## Manually uploading a game
Load single game rom by setting the device in BOOTSEL mode. (Connect to computer then Hold X and power on device)
The ROM should be placed at address **0x10110000**, and can be  transferred using [picotool](https://github.com/raspberrypi/picotool).

```
picotool load rom.nes -t bin -o 0x10110000
```

**Attention: the upload address has been changed from 0x10080000 to 0x10110000.** This is because of the additional size of the built-in game baked into the executable.


## Button maps

- Y: is mapped to NES Select
- X: is mapped to NES Start

### In-game
- Y + X: Open menu
- Y + LEFT: Previous game
- Y + RIGHT: Next Game
- Y + UP: Start built-in freeware game [Blade Buster](https://www.rgcd.co.uk/2011/05/blade-buster-nes.html)

### In-Menu
- Up/DOWN: Scroll through list
- A : Start selected game
- B : Exit menu



## Screen Resolution
The original Nintendo Entertainment System has a resolution of 256x240 pixels. The PicoSystem has a resolution of 240x240 pixels. Therefore, on the PicoSystem the first and last 8 Pixels of each horizontal line are not visible.

## Credits
InfoNes is programmed by [Jay Kumogata](https://github.com/jay-kumogata/InfoNES) and ported for DVI output to the Raspberry PI Pico by [Shuichi Takano](https://github.com/shuichitakano/pico-infones). I used the port of Shuichi Takano as a starting point for this project.
