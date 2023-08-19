

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/divider.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "hardware/interp.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <hardware/flash.h>
#include <memory>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <algorithm>

// #include "pico/util/queue.h"
#include "pico/multicore.h"

#include <InfoNES.h>
#include <InfoNES_System.h>
#include <InfoNES_pAPU.h>

#include "rom_selector.h"

#include "hardware.hpp"
#include "gpbuttons.h"
#include "menu.h"

bool fps_enabled = false;

// final wave buffer
int fw_wr, fw_rd;
int final_wave[2][735 + 1]; /* 44100 (just in case)/ 60 = 735 samples per sync */

// change volume
#define FW_VOL_MAX 100
int volume = 40;
unsigned int volume_increment = 5;
#define VOLUMEFRAMES 120   // number of frames the volume is shown
int showVolume = 0;        // When > 0 volume is shown on screen
char volumeOperator = '+'; // '+' or '-' to indicate if volume is increased or decreased
#include "font_8x8.h"
float overdrive = 1.0f; 
//micomenu
bool micromenu;
int menu_selected = 0;//audio volume brightness, a go b back
int menu_subselection = -1; //used for audio 0 s, 1 r, 2 w; and mode 0, piezo,1 speaker,2 both, 3 mute
const char* menuStrings[] = { "Exit","Rapid A", "Rapid B","Backlight"};
uint8_t backlight_value = 100;
// speaker
#ifdef SPEAKER_ENABLED
int mode = 0; // 0= piezo only 1= speaker only 2= both 3= mute all
char modeOperator = 'P';

// initialiize an arry of four strings
const char *modeStrings[] = {"Piezo only", "Speaker only", "Both", "Mute all"};
int showSpeakerMode = 0;      // When > 0 speaker mode is shown on screen
#define SPEAKERMODEFRAMES 120 // number of frames the speaker mode is shown
#endif

#ifdef LED_ENABLED
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
#endif

bool saveSettingsAndReboot = false;
#define STATUSINDICATORSTRING "STA"
#define VOLUMEINDICATORSTRING "VOL"

namespace
{
    static constexpr uintptr_t NES_FILE_ADDR = 0x10110000;         // Location of .nes rom or tar archive with .nes roms
    static constexpr uintptr_t NES_BATTERY_SAVE_ADDR = 0x100D0000; // 256K
                                                                   //  = 8K   D0000 - 0D1FFF for persisting some variables after reboot
                                                                   //  = 248K D2000 - 10FFFF for save games (=31 savegames MAX)
                                                                   // grows towards NES_FILE_ADDR
    ROMSelector romSelector_;
}

// color table in aaaarrrrggggbbbb format
//  a = alpha - 4 bit
//  r = red - 4 bit
//  g = green - 4 bit
//  b = blue - 4 bit
// Colors are stored as ggggbbbbaaaarrrr
// converted from http://wiki.picosystem.com/en/tools/image-converter
const WORD __not_in_flash_func(NesPalette)[] = {
    0x77f7,  // 00
    0x37f0,  // 01
    0x28f2,  // 02
    0x28f4,  // 03
    0x17f6,  // 04
    0x14f8,  // 05
    0x11f8,  // 06
    0x20f6,  // 07
    0x30f4,  // 08
    0x40f1,  // 09
    0x40f0,  // 0a
    0x41f0,  // 0b
    0x44f0,  // 0c
    0x00f0,  // 0d
    0x00f0,  // 0e
    0x00f0,  // 0f
    0xbbfb,  // 10
    0x7cf1,  // 11
    0x6df4,  // 12
    0x5df8,  // 13
    0x4bfb,  // 14
    0x47fc,  // 15
    0x43fc,  // 16
    0x50fa,  // 17
    0x60f7,  // 18
    0x70f4,  // 19
    0x81f1,  // 1a
    0x84f0,  // 1b
    0x88f0,  // 1c
    0x00f0,  // 1d
    0x00f0,  // 1e
    0x00f0,  // 1f
    0xFFFF,  // 20
    0xcff6,  // 21
    0xbff9,  // 22
    0x9ffd,  // 23
    0x9fff,  // 24
    0x9cff,  // 25
    0x98ff,  // 26
    0xa5ff,  // 27
    0xb3fc,  // 28
    0xc3f9,  // 29
    0xd6f6,  // 2a
    0xd9f4,  // 2b
    0xddf4,  // 2c
    0x55f5,  // 2d
    0x00f0,  // 2e
    0x00f0,  // 2f
    0xFFFF,  // 30
    0xeffc,  // 31
    0xeffe,  // 32
    0xefff,  // 33
    0xefff,  // 34
    0xdfff,  // 35
    0xddff,  // 36
    0xecff,  // 37
    0xebff,  // 38
    0xfbfd,  // 39
    0xfcfc,  // 3a
    0xfdfb,  // 3b
    0xfffb,  // 3c
    0xccfc,  // 3d
    0x00f0,  // 3e
    0x00f0}; // 3f

static auto frame = 0;
static uint32_t start_tick_us = 0;
static uint32_t fps = 0;

#define SCANLINEBUFFERLINES 24 // Max 40
#define SCANLINEPIXELS 240     // 320
#define SCANLINEBYTESIZE (SCANLINEPIXELS * sizeof(WORD))
WORD scanlinebuffer0[SCANLINEPIXELS * SCANLINEBUFFERLINES];
WORD scanlinebuffer1[SCANLINEPIXELS * SCANLINEBUFFERLINES];
WORD *scanlinesbuffers[] = {scanlinebuffer0, scanlinebuffer1};

uint32_t getCurrentNVRAMAddr()
{

    if (!romSelector_.getCurrentROM())
    {
        return {};
    }
    int slot = romSelector_.getCurrentNVRAMSlot();
    if (slot < 0)
    {
        return {};
    }
    printf("SRAM slot %d\n", slot);
    // Save Games are stored towards address stored roms.
    // calculate address of save game slot
    // slot 0 is reserved. (Some state variables are stored at this location)
    uint32_t saveLocation = NES_BATTERY_SAVE_ADDR + SRAM_SIZE * (slot + 1);
    if (saveLocation >= NES_FILE_ADDR)
    {
        printf("No more save slots available, (Requested slot = %d)", slot);
        return {};
    }
    return saveLocation;
}

// Positions in SRAM for storing state variables
#define STATUSINDICATORPOS 0
#define GAMEINDEXPOS 3
#define ADVANCEPOS 4
#define VOLUMEINDICATORPOS 5
#define MODEPOS 8
#define VOLUMEPOS 9
#define BACKLIGHTPOS 10

// Save NES Battery RAM (about 58 Games exist with save battery)
// Problem: First call to saveNVRAM  after power up is ok
// Second call  causes a crash in flash_range_erase()
// Because of this we reserve one flash block for saving state of current played game, selected action and sound settings.
// Then the RP2040 will always be rebooted
// After reboot, the state will be restored.
void saveNVRAM(uint8_t statevar, char advance)
{
    static_assert((SRAM_SIZE & (FLASH_SECTOR_SIZE - 1)) == 0);
    printf("save SRAM and/or settings\n");
    if (!SRAMwritten && saveSettingsAndReboot == false)
    {
        printf("  SRAM not updated and no audio settings changed.\n");
        return;
    }
   
    // Disable core 1 to prevent RP2040 from crashing while writing to flash.
    printf("  resetting Core 1\n");
    multicore_reset_core1();
    uint32_t ints = save_and_disable_interrupts();
    
    uint32_t addr = getCurrentNVRAMAddr();
    uint32_t ofs = addr - XIP_BASE;
    if (addr)
    {
        printf("  write SRAM to flash %x --> %x\n", addr, ofs);
        flash_range_erase(ofs, SRAM_SIZE);
        flash_range_program(ofs, SRAM, SRAM_SIZE);
    }
    // Save state variables
    // - Current game index
    // - Advance (+ Next, - Previous, B Build-in game, R  Reset)
    // - Speaker mode
    // - Volume
    printf("  write state variables and sound settings to flash\n");
   
    SRAM[STATUSINDICATORPOS] = STATUSINDICATORSTRING[0];
    SRAM[STATUSINDICATORPOS + 1] = STATUSINDICATORSTRING[1];
    SRAM[STATUSINDICATORPOS + 2] = STATUSINDICATORSTRING[2];
    SRAM[GAMEINDEXPOS] = statevar;
    SRAM[ADVANCEPOS] = advance;
    SRAM[VOLUMEINDICATORPOS] = VOLUMEINDICATORSTRING[0];
    SRAM[VOLUMEINDICATORPOS + 1] = VOLUMEINDICATORSTRING[1];
    SRAM[VOLUMEINDICATORPOS + 2] = VOLUMEINDICATORSTRING[2];
    SRAM[MODEPOS] = mode;
    SRAM[VOLUMEPOS] = volume;

    SRAM[BACKLIGHTPOS] = backlight_value;
    // first block of flash is reserved for storing state variables
    uint32_t state = NES_BATTERY_SAVE_ADDR - XIP_BASE;
    flash_range_erase(state, SRAM_SIZE);
    flash_range_program(state, SRAM, SRAM_SIZE);

    printf("  done\n");
    printf("  Rebooting...\n");
    restore_interrupts(ints);
    // Reboot after SRAM is flashed
    watchdog_enable(100, 1);
    while (1)
        ;

    SRAMwritten = false;
    // reboot
}

bool loadNVRAM()
{
    if (auto addr = getCurrentNVRAMAddr())
    {
        printf("load SRAM %x\n", addr);
        memcpy(SRAM, reinterpret_cast<void *>(addr), SRAM_SIZE);
    }
    SRAMwritten = false;
    return true;
}

void loadState()
{
    memcpy(SRAM, reinterpret_cast<void *>(NES_BATTERY_SAVE_ADDR), SRAM_SIZE);
}

int getbuttons()
{
    picosystem::_gpio_get2();
    return (picosystem::button(picosystem::LEFT) ? GPLEFT : 0) |
           (picosystem::button(picosystem::RIGHT) ? GPRIGHT : 0) |
           (picosystem::button(picosystem::UP) ? GPUP : 0) |
           (picosystem::button(picosystem::DOWN) ? GPDOWN : 0) |
           (picosystem::button(picosystem::Y) ? GPY : 0) |
           (picosystem::button(picosystem::X) ? GPX : 0) |
           (picosystem::button(picosystem::A) ? GPA : 0) |
           (picosystem::button(picosystem::B) ? GPB : 0) |
           0;
}
void RomMenu()
{
    micromenu = false;
    saveNVRAM(romSelector_.GetCurrentRomIndex(), 'R');

    pwm_set_gpio_level(11, 0);

#ifdef SPEAKER_ENABLED
    pwm_set_gpio_level(1, 0);
#endif
}
  static DWORD prevButtons = 0;
    static int rapidFireMask = 0;
    static int rapidFireCounter = 0;
    static bool jumptomenu = false;

 void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem)
{

    // moved variables outside function body because prevButtons gets initialized to 0 everytime the function is called.
    // This is strange because a static variable inside a function is only initialsed once and retains it's value
    // throughout different function calls.
    // Am i missing something?
    // static DWORD prevButtons = 0;
    // static int rapidFireMask = 0;
    // static int rapidFireCounter = 0;

     int v = getbuttons();
    
    ++rapidFireCounter;
    bool reset = jumptomenu = false;

    auto &dst = *pdwPad1;

   

    int rv = v;
    if (rapidFireCounter & 2)
    {
        // 15 fire/sec
        rv &= ~rapidFireMask;
    }

    dst = rv;

    auto p1 = v;

    auto pushed = v & ~prevButtons;

    /*if (pushed & GPX)
    {
        printf("%d\n", fps);
    }*/
    if (p1 & GPX)
    {
        if (pushed & GPA)
        {
            fps_enabled = !fps_enabled;
            printf("Show fps: %d\n", fps_enabled);
        }
    }
    if (p1 && micromenu)
    {
        if (p1 & GPUP && !(prevButtons & GPUP))
        {
            menu_selected = (menu_selected - 1 < 0) ? 0 : menu_selected - 1;
        }
         if (p1 & GPDOWN && !(prevButtons & GPDOWN))
        {
            menu_selected = (menu_selected + 1 < std::size(menuStrings)) ? menu_selected + 1 : menu_selected;
        }
        if (p1 & GPB)
        {
            micromenu = false;
        }
        if (p1 & GPA &&!(prevButtons & GPA))
        {
            switch (menu_selected)
            {
                case 0://exit
                    reset = true;
                    jumptomenu = true;
                    RomMenu();
                   
                break;
                case 1://rapid A
                    //toggle
                    rapidFireMask ^= GPA;
                    break;
                case 2://rapid B
                    //toggle
                    rapidFireMask ^= GPB;
                    break;
                    

            }
        }
        if (menu_selected==3)//backlight
        { 
            if (p1 & GPLEFT && !(prevButtons & GPLEFT))
            {
                backlight_value = (backlight_value - 10 <= 0) ? 0 : backlight_value - 10;
                picosystem::backlight(backlight_value);
            }
            if (p1 & GPRIGHT && !(prevButtons & GPRIGHT))
            {
                backlight_value = (backlight_value + 10 >= 100) ? 100 : backlight_value + 10;
                picosystem::backlight(backlight_value);
            }
        }
    }
    if (p1 & GPY)
    {
        if (pushed & GPLEFT)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), '-');
            romSelector_.prev();
            reset = true;
        }
        if (pushed & GPRIGHT)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), '+');
            romSelector_.next();
            reset = true;
        }
        if (pushed & GPUP)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), 'B');
            romSelector_.selectcustomrom();
            reset = true;
        }
        if (pushed & GPDOWN)
        {
#ifdef SPEAKER_ENABLED

            mode = (mode + 1) % 4;
            showSpeakerMode = SPEAKERMODEFRAMES;
            saveSettingsAndReboot = true;
#endif
        }
        if (pushed & GPX)
        {
            micromenu = true;
           
        }

        if (pushed & GPA)
        {
            volume = (volume + volume_increment >= FW_VOL_MAX) ? FW_VOL_MAX : volume + volume_increment;
            showVolume = VOLUMEFRAMES;
            volumeOperator = '+';
            saveSettingsAndReboot = true;
            // set_fw_vol(volume);
            //  rapidFireMask[i] ^= io::GamePadState::Button::A;
        }
        if (pushed & GPB)
        {
            volume = volume - volume_increment;
            if (volume < 0)
                volume = 0;
            showVolume = VOLUMEFRAMES;
            volumeOperator = '-';
            saveSettingsAndReboot = true;
            // set_fw_vol(volume);
            //  rapidFireMask[i] ^= io::GamePadState::Button::B;
        }
    
    }

    prevButtons = v;

    *pdwSystem = reset ? PAD_SYS_QUIT : 0;
}

void InfoNES_MessageBox(const char *pszMsg, ...)
{
    printf("[MSG]");
    va_list args;
    va_start(args, pszMsg);
    vprintf(pszMsg, args);
    va_end(args);
    printf("\n");
}

void InfoNES_Error(const char *pszMsg, ...)
{
    printf("[Error]");
    va_list args;
    va_start(args, pszMsg);
    // vsnprintf(ErrorMessage, ERRORMESSAGESIZE, pszMsg, args);
    // printf("%s", ErrorMessage);
    va_end(args);
    printf("\n");
}
bool parseROM(const uint8_t *nesFile)
{
    memcpy(&NesHeader, nesFile, sizeof(NesHeader));
    if (!checkNESMagic(NesHeader.byID))
    {
        return false;
    }

    nesFile += sizeof(NesHeader);

    memset(SRAM, 0, SRAM_SIZE);

    if (NesHeader.byInfo1 & 4)
    {
        memcpy(&SRAM[0x1000], nesFile, 512);
        nesFile += 512;
    }

    auto romSize = NesHeader.byRomSize * 0x4000;
    ROM = (BYTE *)nesFile;
    nesFile += romSize;

    if (NesHeader.byVRomSize > 0)
    {
        auto vromSize = NesHeader.byVRomSize * 0x2000;
        VROM = (BYTE *)nesFile;
        nesFile += vromSize;
    }

    return true;
}

void InfoNES_ReleaseRom()
{
    ROM = nullptr;
    VROM = nullptr;
}

void InfoNES_SoundInit()
{
}

int InfoNES_SoundOpen(int samples_per_sync, int sample_rate)
{
    return 0;
}

void InfoNES_SoundClose()
{
}

int __not_in_flash_func(InfoNES_GetSoundBufferSize)()
{
    return 735;
}

void InfoNES_SoundOutput(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5)
{
    int i;

    for (i = 0; i < samples; i++)
    {
        final_wave[fw_wr][i] = (unsigned char)wave1[i] + (unsigned char)wave2[i] + (unsigned char)wave3[i] + (unsigned char)wave4[i] + (unsigned char)wave5[i];
    }
    final_wave[fw_wr][i] = -1;
    fw_wr = 1 - fw_wr;
}

extern WORD PC;

int InfoNES_LoadFrame()
{

    auto count = frame++;
#ifdef LED_ENABLED
    auto onOff = hw_divider_s32_quotient_inlined(count, 60) & 1;
    gpio_put(LED_PIN, onOff);
#endif
    // Wait for vsync to pace emulator at 60fps as suggested by shuichitakano in issue #4, thanks!
    picosystem::_wait_vsync();
    uint32_t tick_us = picosystem::time_us() - start_tick_us;
    // calculate fps and round to nearest value (instead of truncating/floor)
    fps = (1000000 - 1) / tick_us + 1;
    start_tick_us = picosystem::time_us();
    if (showVolume > 0)
    {
        showVolume--;
    }
#ifdef SPEAKER_ENABLED
    if (showSpeakerMode > 0)
    {
        showSpeakerMode--;
    }
#endif
    return count;
}

namespace
{
    WORD lb[256];
    WORD *currentLineBuffer_{};
}

void __not_in_flash_func(drawWorkMeterUnit)(int timing,
                                            [[maybe_unused]] int span,
                                            uint32_t tag)
{
    if (timing >= 0 && timing < 640)
    {
        auto p = currentLineBuffer_;
        p[timing] = tag; // tag = color
    }
}

void __not_in_flash_func(drawWorkMeter)(int line)
{
    if (!currentLineBuffer_)
    {
        return;
    }
}

static int bufferIndex = 0;
static int prevbufferIndex = -1;
static int lineInBuffer = 0;
void __not_in_flash_func(InfoNES_PreDrawLine)(int line)
{

    bufferIndex = hw_divider_s32_quotient_inlined(line, SCANLINEBUFFERLINES) & 1;
    lineInBuffer = hw_divider_s32_remainder_inlined(line, SCANLINEBUFFERLINES);

    WORD *b = scanlinesbuffers[bufferIndex] + lineInBuffer * SCANLINEPIXELS;

    InfoNES_SetLineBuffer(lb, sizeof(lb));

    currentLineBuffer_ = lb;
}

bool startframe = false;
bool endframe = false;

void __not_in_flash_func(RomSelect_PreDrawLine)(int line)
{
    bufferIndex = hw_divider_s32_quotient_inlined(line, SCANLINEBUFFERLINES) & 1;
    lineInBuffer = hw_divider_s32_remainder_inlined(line, SCANLINEBUFFERLINES);
    RomSelect_SetLineBuffer(lb, sizeof(lb));
    currentLineBuffer_ = lb;
}

void __not_in_flash_func(DisplayChar)(WORD *buffer, int y, char c, WORD fgColor, WORD bgColor)
{
    char fontSlice = font_8x8[(c - 32) + (y)*95];
    for (auto bit = 0; bit < 8; bit++)
    {
        if (fontSlice & 1)
        {
            *buffer++ = fgColor;
        }
        else
        {
            *buffer++ = bgColor;
        }
        fontSlice >>= 1;
    }
}

void __not_in_flash_func(DisplayText)(const char* charBuffer,int e , WORD* fpsBuffer, WORD fgc, WORD bgc)
{
    //picosystem::text(charBuffer, 40, e);

    for (auto i = 0; i < strlen(charBuffer); i++)
    {
        char aChar = charBuffer[i];
        DisplayChar(fpsBuffer, e, aChar, fgc, bgc);
        fpsBuffer += 8;
    }
}
void __not_in_flash_func(InfoNES_PostDrawLine)(int line, bool frommenu)
{ 
#if !defined(NDEBUG)
    util::WorkMeterMark(0xffff);
    drawWorkMeter(line);
#endif

    
    WORD *fpsBuffer = nullptr;
    WORD fgc = NesPalette[48];
    WORD bgc = NesPalette[15];
    int bat = picosystem::battery();
    if (bat < 11 && !micromenu)
    {
        if (line >= 8 && line < 16)
        {
            char charBuffer[9];
            charBuffer[0] = 'B';
            charBuffer[1] = 'A';
            charBuffer[2] = 'T';
            charBuffer[3] = '[';
            charBuffer[4] = '0' + (bat / 100);
            charBuffer[5] = '0' + ((bat % 100) / 10);
            charBuffer[6] = '0' + (bat % 10);
            charBuffer[7] = ']';
            charBuffer[8] = 0;
            DisplayText(charBuffer, line % 8, fpsBuffer, fgc, bgc);
        }
    }
    if (fps_enabled && line >= 24 && line < 32)
    {

        fpsBuffer = lb + 180;
        char charBuffer[3];
        charBuffer[0] = '0' + (fps / 10);
        charBuffer[1] = '0' + (fps % 10);
        charBuffer[2] = 0;
        DisplayText(charBuffer, line % 8, fpsBuffer, fgc, bgc);
        /*
        int rowInChar = line % 8;
        for (auto i = 0; i < strlen(charBuffer); i++)
        {
            char aChar = charBuffer[i];
            DisplayChar(fpsBuffer, rowInChar, aChar, fgc, bgc);
            fpsBuffer += 8;
        }*/
    } 
    if (showVolume > 0 && line >= 40 && line < 48)
    {
        fpsBuffer = lb + 115;
        // fill charbuffer with volume and overdrive. 
        char charBuffer[9];
        charBuffer[0] = volumeOperator;
        charBuffer[1] = '0' + (volume / 100);
        charBuffer[2] = '0' + ((volume % 100) / 10);
        charBuffer[3] = '0' + (volume % 10);
        charBuffer[4] = '*';
        charBuffer[5] = '0' + ((short)overdrive % 10);
        charBuffer[6] = '.';
        charBuffer[7] = '0' + ((short)(overdrive * 10) % 10);
        charBuffer[8] = 0;
        DisplayText(charBuffer, line % 8, fpsBuffer, fgc, bgc);
      /*  int rowInChar = line % 8;
        for (auto i = 0; i < strlen(charBuffer); i++)
        {
            char aChar = charBuffer[i];
            DisplayChar(fpsBuffer, rowInChar, aChar, fgc, bgc);
            fpsBuffer += 8;
        }*/
    }
    if (micromenu) {
        if (line >= 8 && line < 16)
        {
            fpsBuffer = lb + 180;
            char charBuffer[9];
            charBuffer[0] = 'B';
            charBuffer[1] = 'A';
            charBuffer[2] = 'T';
            charBuffer[3] = '[';
            charBuffer[4] = '0' + (bat / 100);
            charBuffer[5] = '0' + ((bat % 100) / 10);
            charBuffer[6] = '0' + (bat % 10);
            charBuffer[7] = ']';
            charBuffer[8] = 0;
            DisplayText(charBuffer, line % 8, fpsBuffer, fgc, bgc);
        }
        if (line >= 80 && line < 88)
        {
            fpsBuffer = lb + 80;
            //menu title
            DisplayText("Micro Menu",line % 8, fpsBuffer, fgc, bgc);
           
        }
        if (line >= 96 && line < 104)
        {
            fpsBuffer = lb + 80;
            DisplayText(menuStrings[menu_selected], line % 8, fpsBuffer, fgc, bgc);
          

        }
        if (line >= 112 && line < 120)
        {
            fpsBuffer = lb + 60;
            //menu value display

            switch (menu_selected)
            {
            case 0://exit
                DisplayText("A Quit, B Close", line % 8, fpsBuffer, fgc, bgc);
                break;
            case 1://rapid A
                if (rapidFireMask & GPA) {
                    DisplayText("Rapid A On", line % 8, fpsBuffer, fgc, bgc);
                }
                else
                {
                    DisplayText("Rapid A Off", line % 8, fpsBuffer, fgc, bgc);
                }
                break;
            case 2://rapid B
                if (rapidFireMask & GPB) {
                    DisplayText("Rapid B On", line % 8, fpsBuffer, fgc, bgc);
                }
                else
                {
                    DisplayText("Rapid B Off", line % 8, fpsBuffer, fgc, bgc);
                }

                break;


            }
        }
        if (line >= 120 && line < 128)
        {
            switch (menu_selected)
            {
            case 3://backlight
                fpsBuffer = lb + 76;
                //menu switch case here. 
                int targetvalue = backlight_value / 10;
                //insert '|' into a array of "-----------"
                // Each dash is a percentage: 0% 10% 20% 30% 40% 50% 60% 70% 80% 90% 100%
                // So 50% would be:  "-----|-----"
                char valueString[] = "-----------"; //inserts | based on value.
                valueString[targetvalue ] = '|';
                DisplayText(valueString, line % 8, fpsBuffer, fgc, bgc);
                break;
            }
        }

    }
#ifdef SPEAKER_ENABLED
    if (showSpeakerMode > 0 && line >= 96 && line < 104)
    {
        int l = (strlen(modeStrings[mode]) * 8) / 2;
        fpsBuffer = lb + 104 - l;
        DisplayText(modeStrings[mode], line % 8, fpsBuffer, fgc, bgc);
       /* int rowInChar = line % 8;
        for (auto i = 0; i < strlen(modeStrings[mode]); i++)
        {
            char aChar = modeStrings[mode][i];
            DisplayChar(fpsBuffer, rowInChar, aChar, fgc, bgc);
            fpsBuffer += 8;
        }
        */
    }
#endif
  
    bufferIndex = hw_divider_s32_quotient_inlined(line, SCANLINEBUFFERLINES) & 1;
    lineInBuffer = hw_divider_s32_remainder_inlined(line, SCANLINEBUFFERLINES);

    WORD *b = scanlinesbuffers[bufferIndex] + lineInBuffer * SCANLINEPIXELS;
    memcpy(b, lb + 8, SCANLINEPIXELS * sizeof(WORD));
    if (prevbufferIndex != bufferIndex)
    {
        if (prevbufferIndex != -1)
        {
            // wait untill DMA is ready to write next buffer
            while (picosystem::_is_flipping())
            {
            }
            picosystem::_flipbuffer((void *)scanlinesbuffers[prevbufferIndex], sizeof(scanlinebuffer0) / 4);
            startframe = endframe = false;
        }
        prevbufferIndex = bufferIndex;
    }
    if (line == 0)
    {
        startframe = true;
    }
    if (line == 239)
    {
        endframe = true;
    }

    assert(currentLineBuffer_);

    currentLineBuffer_ = nullptr;
}

bool loadAndReset()
{
    auto rom = romSelector_.getCurrentROM();
    if (!rom)
    {
        printf("ROM does not exists.\n");
        return false;
    }

    if (!parseROM(rom))
    {
        printf("NES file parse error.\n");
        return false;
    }
    if (loadNVRAM() == false)
    {
        return false;
    }

    if (InfoNES_Reset() < 0)
    {
        printf("NES reset error.\n");
        return false;
    }

    return true;
}

int InfoNES_Menu()
{

    //micro menu
    if (micromenu)
    {
      
        return -1;
    }
    // InfoNES_Main() のループで最初に呼ばれる is called first in the loop of
    return loadAndReset() ? 0 : -1;
    // return 0;
}

using namespace picosystem;

#define piezolevelmax 12800
#define buffermax 1280
void fw_callback()
{
    uint64_t et, nt;
    int i;
    while (1)
    {
        while (fw_wr == fw_rd)
        {
            sleep_us(1);
        }
        for (i = 0; final_wave[fw_rd][i] != -1; i++)
        {
            et = to_us_since_boot(get_absolute_time());
            nt = et + SAMPLE_INTERVAL;

#ifdef SPEAKER_ENABLED


            if (final_wave[fw_rd][i] > 0)
            {
                //volume setting 0-50 : 0-100 volume output
                //volume 51-100 : 1.1 - 4.0 overdrive multiplyer. 
                uint16_t volumelevel = final_wave[fw_rd][i];
                uint16_t pwm_piezo_level_volume, pwm_speaker_level_volume;

                if (volume > 0 && volume < 51)
                {
                    //0-50
                    pwm_piezo_level_volume = (volumelevel * piezolevelmax / (float)buffermax) * (volume * 2) / FW_VOL_MAX; 
                    pwm_speaker_level_volume = volumelevel * (volume * 2) / FW_VOL_MAX;
                    overdrive = 1.0f;//for display
                }
                else if (volume > 50)
                {
                    overdrive =(((volume-50)  * 2.92f) / (FW_VOL_MAX * 0.5f))  + 1.08f; //0.02 - 1 * 2.92f + 1.08f = 1.1-4.0f
                    //100 volume so no reduction. * overdrive 
                    pwm_piezo_level_volume =ceil(volumelevel *  overdrive * piezolevelmax / (float)buffermax);
                    pwm_speaker_level_volume =ceil( volumelevel * overdrive);
                    //clipping 
                    if (pwm_piezo_level_volume > piezolevelmax) {
                        pwm_piezo_level_volume = piezolevelmax;
                    }
                       if (pwm_speaker_level_volume > buffermax) {
                            pwm_speaker_level_volume = buffermax;
                    }
                }
                else {
                    pwm_piezo_level_volume = 0;
                    pwm_speaker_level_volume = 0;
                }
                switch (mode)
                {
                case 0: // piezo only
                    pwm_set_gpio_level(11, pwm_piezo_level_volume);
                    pwm_set_gpio_level(1, 0);
                    break;
                case 1: // speaker only
                    pwm_set_gpio_level(11, 0);
                    pwm_set_gpio_level(1, pwm_speaker_level_volume);
                    break;
                case 2: // both only
                    pwm_set_gpio_level(11, pwm_piezo_level_volume);
                    pwm_set_gpio_level(1, pwm_speaker_level_volume);
                    break;
                case 3: // mute all
                    pwm_set_gpio_level(11, 0);
                    pwm_set_gpio_level(1, 0);
                    break;
                }
            }
            else
            {
                pwm_set_gpio_level(11, 0);
                pwm_set_gpio_level(1, 0);
            }
#else


            if (final_wave[fw_rd][i] > 0)
            {
                //volume setting 0-50 : 0-100 volume output
                //volume 51-100 : 1.1 - 4.0 overdrive multiplyer. 
                uint16_t volumelevel = final_wave[fw_rd][i];
                uint16_t pwm_piezo_level_volume;

                if (volume > 0 && volume < 51)
                {
                    //0-50
                    pwm_piezo_level_volume = (volumelevel * piezolevelmax / (float)buffermax) * (volume * 2) / FW_VOL_MAX;
                     overdrive = 1.0f;//for display
                }
                else if (volume > 50)
                {
                    overdrive = (((volume - 50) * 2.92f) / (FW_VOL_MAX * 0.5f)) + 1.08f; //0.02 - 1 * 2.92f + 1.08f = 1.1-4.0f
                    //100 volume so no reduction. * overdrive 
                    pwm_piezo_level_volume = ceil(volumelevel * overdrive * piezolevelmax / (float)buffermax);
                    //clipping 
                    if (pwm_piezo_level_volume > piezolevelmax) {
                        pwm_piezo_level_volume = piezolevelmax;
                    }
                 
                }
                else {
                    pwm_piezo_level_volume = 0;
                   
                }
                pwm_set_gpio_level(11, pwm_piezo_level_volume);
                
            }
            else
            {
                pwm_set_gpio_level(11, 0);
                
            }
        
#endif

        while (et < nt)
        {
            et = to_us_since_boot(get_absolute_time());
            sleep_us(1);
        }
        }
    }
    final_wave[fw_rd][0] = -1;
    fw_rd = fw_wr;
    
}

int main()
{
    micromenu = false;
    char errorMessage[30];
    saveSettingsAndReboot = false;
    strcpy(errorMessage, "");
    _init_hardware();
 
    final_wave[0][0] = final_wave[1][0] = -1; //click fix
    fw_wr = fw_rd = 0;
    multicore_launch_core1(fw_callback);

    backlight(backlight_value);
    memset(scanlinebuffer0, 0, sizeof(scanlinebuffer0));
    memset(scanlinebuffer1, 0, sizeof(scanlinebuffer1));

    stdio_init_all();
   
    printf("Start program\n");

#ifdef LED_ENABLED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
#endif
  

    // util::dumpMemory((void *)NES_FILE_ADDR, 1024);

#if 0
    //
    auto *i2c = i2c0;
    static constexpr int I2C_SDA_PIN = 16;
    static constexpr int I2C_SCL_PIN = 17;
    i2c_init(i2c, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    // gpio_pull_up(I2C_SDA_PIN);
    // gpio_pull_up(I2C_SCL_PIN);
    i2c_set_slave_mode(i2c, false, 0);

    {
        constexpr int addrSegmentPointer = 0x60 >> 1;
        constexpr int addrEDID = 0xa0 >> 1;
        constexpr int addrDisplayID = 0xa4 >> 1;

        uint8_t buf[128];
        int addr = 0;
        do
        {
            printf("addr: %04x\n", addr);
            uint8_t tmp = addr >> 8;
            i2c_write_blocking(i2c, addrSegmentPointer, &tmp, 1, false);

            tmp = addr & 255;
            i2c_write_blocking(i2c, addrEDID, &tmp, 1, true);
            i2c_read_blocking(i2c, addrEDID, buf, 128, false);

            util::dumpMemory(buf, 128);
            printf("\n");

            addr += 128;
        } while (buf[126]); 
    }
#endif

    // When system is rebooted after flashing SRAM, load the saved state and volume from flash and proceed.
    loadState();
    // Restore backlight
    if (SRAM[BACKLIGHTPOS] > 0) //wont restore a off screen or if value was never set. 
    {
        backlight_value = SRAM[BACKLIGHTPOS];
        backlight(backlight_value);
    }
    // Restore speaker and volume settings
    if (strncmp((char *)&SRAM[VOLUMEINDICATORPOS], VOLUMEINDICATORSTRING, 3) == 0)
    {
            mode = SRAM[MODEPOS];
            volume = SRAM[VOLUMEPOS];
            
            printf("Restored mode %d, volume %d\n", mode, volume);
    }
    if (watchdog_caused_reboot() && strncmp((char *)SRAM, STATUSINDICATORSTRING, 3) == 0)
    {

        // Game which caused the reboot
        // When reboot is caused by built-in game, startingGame will be -1
        int8_t startingGame = (int8_t)SRAM[GAMEINDEXPOS];
        printf("Game caused reboot: %d\n", startingGame);
       
        // + start next Game
        // - start previous game
        // R reset to menu
        // B Start built-in Game
        char advance = (char)SRAM[ADVANCEPOS];
        int tmpGame = startingGame;
        // When coming from built-in game, just start the first game.
        if (tmpGame == -1 && advance != 'R')
            tmpGame = 0;
        romSelector_.init(NES_FILE_ADDR, tmpGame);
        if (startingGame >= 0 && advance != 'R') // R = Reset
        {
            if (advance == '+')
                romSelector_.next();
            if (advance == '-')
                romSelector_.prev();
        }
        if (advance == 'B') {
            romSelector_.selectcustomrom();
        }
        if (advance == 'R')
        {
            romSelector_.setRomIndex(menu(NES_FILE_ADDR, errorMessage, true));
        }
        prevButtons = -1;
      
    }
    else
    {

        romSelector_.init(NES_FILE_ADDR, 0);
        romSelector_.setRomIndex(menu(NES_FILE_ADDR, errorMessage));
    }

    while (true)
    {
        int index = romSelector_.GetCurrentRomIndex();
        printf("Starting '%s'.\n", romSelector_.GetCurrentGameName());
        InfoNES_Main();
        if (jumptomenu)
        {
            pwm_set_gpio_level(11, 0);
#ifdef SPEAKER_ENABLED
            pwm_set_gpio_level(1, 0);
#endif //fix sound continue from exit to menu while playing a game. 
            romSelector_.setRomIndex(menu(NES_FILE_ADDR, errorMessage));
        }
    }

    return 0;
}
