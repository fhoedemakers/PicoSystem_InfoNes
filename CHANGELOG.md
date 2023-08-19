# CHANGELOG.md

## General Information

> To go back to the games list menu when in a game, you have to press (Y+X) and then (A). This is because of a newly added in-game menu. 

Comes with Homebrew game Blade Buster. When no rom is loaded, this game starts.

Support for flashing multiple games. In this case a menu is displayed in the emulator.

A [Micosoft Windows companion application](https://github.com/fhoedemakers/PicoSystemInfoNesLoader) is added to this release, so the user can easyly flash roms and flash a new version of the emulator.  

Extract the zip-file **PicoSystemInfoNesLoader.zip** to a folder of choice, then start **PicoSystemInfoNesLoader.exe** in subfolder PicoSystemInfoNesLoader.

![Screenshot](https://github.com/fhoedemakers/PicoSystemInfoNesLoader/blob/master/assets/Screen.png)

See [Readme](https://github.com/fhoedemakers/PicoSystem_InfoNes/blob/master/README.md) for more info.

## Release History

### 1.2.0 (2023-08-18)

Fixes

- Various minor fixes and code cleanup.

Features

- Y + X toggles an in-game menu. (See [Readme](https://github.com/fhoedemakers/PicoSystem_InfoNes/blob/master/README.md) for more info). Presents options for return to games list, rapid fire and backlight settings.
- Battery indicator shown on low battery.

### 1.0.0 (2023-08-13)

Fixes:

- improved audio. [@newschooldev](https://github.com/newschooldev) and  [@Layer812](https://github.com/Layer812)

Features

- Speaker selection and volume settings are saved now. You must reset to menu (Y + X) or toggle game (Y + Left, Y + Right or Y + Up) for this to work.

### 0.9-alpha (2023-08-03)

Fixes:

- Adjust for overclock. [@Layer812](https://github.com/Layer812)

Features:

- none

### 0.8-alpha (2023-07-28)

Fixes:

- Updated README and CHANGELOG
- several audio fixes

Features:

- Merged the changes done by @newschooldev and @Layer812 to master branch
- Volume control OSD added @fhoedemakers
- Speaker select OSD added @fhoedemakers

### 0.7-alpha (2023-07-24)

> this release is never published

Fixes:

 - None

Features:

- Test CI/CD with Github Actions

### 0.6-alpha (2023-07-23)

Fixes:

 - None
 
Features:

 - Audio implemented by @Layer812. The speaker is limited, but sounds ok.
 - Added workflow to automatically generate uf2 binary on each release
