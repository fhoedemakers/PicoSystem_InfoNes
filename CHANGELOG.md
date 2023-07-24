# CHANGELOG.md

## General Information

Comes with Homebrew game Blade Buster. When no rom is loaded, this game starts.

Support for flashing multiple games. In this case a menu is displayed in the emulator.

A [Micosoft Windows companion application](https://github.com/fhoedemakers/PicoSystemInfoNesLoader) is added, so the user can easyly flash roms and flash a new version of the emulator.  

Extract the zip-file **PicoSystemInfoNesLoader.zip** to a folder of choice, then start **PicoSystemInfoNesLoader.exe** in subfolder PicoSystemInfoNesLoader.

![Screenshot](https://github.com/fhoedemakers/PicoSystemInfoNesLoader/blob/master/assets/Screen.png)

Attention! Base address for uploading rom with picotool has changed again. You have to re-upload your rom.

See [Readme](https://github.com/fhoedemakers/PicoSystem_InfoNes/blob/master/README.md) for more info.

## 0.7-alpha (2021-09-19)

Fixes:

 - None

Features:

- Test CI/CD with Github Actions

## 0.6-alpha (2023-07-24)

Fixes:

 - None
 
Features:

 - Audio implemented by @Layer812. The speaker is limited, but sounds ok.
 - Initial release of MiSTer_tty2Waveshare program
 - Added workflow to automatically generate uf2 binary on each release