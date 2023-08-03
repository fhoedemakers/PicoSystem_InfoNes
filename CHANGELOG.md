# CHANGELOG.md

## General Information

Comes with Homebrew game Blade Buster. When no rom is loaded, this game starts.

Support for flashing multiple games. In this case a menu is displayed in the emulator.

A [Micosoft Windows companion application](https://github.com/fhoedemakers/PicoSystemInfoNesLoader) is added to this release, so the user can easyly flash roms and flash a new version of the emulator.  

Extract the zip-file **PicoSystemInfoNesLoader.zip** to a folder of choice, then start **PicoSystemInfoNesLoader.exe** in subfolder PicoSystemInfoNesLoader.

![Screenshot](https://github.com/fhoedemakers/PicoSystemInfoNesLoader/blob/master/assets/Screen.png)

Attention! Base address for uploading rom with picotool has changed again. You have to re-upload your rom.

See [Readme](https://github.com/fhoedemakers/PicoSystem_InfoNes/blob/master/README.md) for more info.

## Release History

### 0.9-alpha (2023-08-03)

Fixes:

- Adjust for overclock. @Layer812

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
