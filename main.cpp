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
int volume = 50;
unsigned int volume_increment = 10;
#define VOLUMEFRAMES 120   // number of frames the volume is shown
int showVolume = 0;        // When > 0 volume is shown on screen
char volumeOperator = '+'; // '+' or '-' to indicate if volume is increased or decreased
#include "font_8x8.h"

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

// static queue_t call_queue;
// typedef struct
// {
//     int scanline;
//     int bufferindex;
//     bool startframe;
//     bool endframe;
// } queue_entry_t;

// static queue_entry_t entry;

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
    uint32_t saveLocation = NES_BATTERY_SAVE_ADDR + SRAM_SIZE * (slot + 1);
    if (saveLocation >= NES_FILE_ADDR)
    {
        printf("No more save slots available, (Requested slot = %d)", slot);
        return {};
    }
    return saveLocation;
}

void __not_in_flash_func(_saveNVRAM)(uint32_t offset, int8_t currentGameIndex, char advance)
{
    static_assert((SRAM_SIZE & (FLASH_SECTOR_SIZE - 1)) == 0);

    uint32_t state = NES_BATTERY_SAVE_ADDR - XIP_BASE;
    uint32_t ints = save_and_disable_interrupts();
    // Save SRAM
    flash_range_erase(offset, SRAM_SIZE);
    flash_range_program(offset, SRAM, SRAM_SIZE);
    // Save state
    SRAM[0] = 'S';
    SRAM[1] = 'T';
    SRAM[2] = 'A';
    SRAM[3] = currentGameIndex;
    SRAM[4] = advance;
    flash_range_erase(state, SRAM_SIZE);
    flash_range_program(state, SRAM, SRAM_SIZE);

    restore_interrupts(ints);
}

/// about 58 Games exist with save battery
// Problem: First call to saveNVRAM  after power up is ok
// Second call  causes a crash in flash_range_erase()
// Because of this we reserve one flash block for saving state of current played game and selected action.
// Then the RP2040 will be rebooted
// After reboot, the state will be restored.
void saveNVRAM(uint8_t statevar, char advance)
{
    if (!SRAMwritten)
    {
        printf("SRAM not updated.\n");
        return;
    }

    if (auto addr = getCurrentNVRAMAddr())
    {
        printf("save SRAM\n");
        printf("  resetting Core 1\n");
        multicore_reset_core1();
        auto ofs = addr - XIP_BASE;

        printf("  write flash %x --> %x\n", addr, ofs);
        _saveNVRAM(ofs, statevar, advance);

        printf("  done\n");
        printf("  Rebooting...\n");

        // Reboot after SRAM is flashed
        watchdog_enable(100, 1);
        while (1)
            ;
    }
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

    ++rapidFireCounter;
    bool reset = jumptomenu = false;

    auto &dst = *pdwPad1;

    int v = getbuttons();

    int rv = v;
    if (rapidFireCounter & 2)
    {
        // 15 fire/sec
        rv &= ~rapidFireMask;
    }

    dst = rv;

    auto p1 = v;

    auto pushed = v & ~prevButtons;

    if (pushed & GPX)
    {
        printf("%d\n", fps);
    }
    if (p1 & GPX)
    {
        if (pushed & GPA)
        {
            fps_enabled = !fps_enabled;
            printf("Show fps: %d\n", fps_enabled);
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
#endif
        }
        if (pushed & GPX)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), 'R');
            reset = true;
            jumptomenu = true;
        }
        if (pushed & GPA)
        {
            volume = (volume + volume_increment >= FW_VOL_MAX) ? FW_VOL_MAX : volume + volume_increment;
            showVolume = VOLUMEFRAMES;
            volumeOperator = '+';
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
#ifndef NO_OVERCLOCK 
        final_wave[fw_wr][i] =
            ((unsigned char)wave1[i] + (unsigned char)wave2[i] + (unsigned char)wave3[i] + (unsigned char)wave4[i] + (unsigned char)wave5[i]) * 4096 / 1280;
#else 
        final_wave[fw_wr][i] =
            ((unsigned char)wave1[i] + (unsigned char)wave2[i] + (unsigned char)wave3[i] + (unsigned char)wave4[i] + (unsigned char)wave5[i]) * 2048 / 1280;
#endif
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
void __not_in_flash_func(InfoNES_PostDrawLine)(int line, bool frommenu)
{
#if !defined(NDEBUG)
    util::WorkMeterMark(0xffff);
    drawWorkMeter(line);
#endif

    char charBuffer[5];
    WORD *fpsBuffer = nullptr;
    WORD fgc = NesPalette[48];
    WORD bgc = NesPalette[15];

    if (fps_enabled && line >= 8 && line < 16)
    {

        fpsBuffer = lb + 8;

        charBuffer[0] = '0' + (fps / 10);
        charBuffer[1] = '0' + (fps % 10);
        charBuffer[2] = 0;
        int rowInChar = line % 8;
        for (auto i = 0; i < strlen(charBuffer); i++)
        {
            char aChar = charBuffer[i];
            DisplayChar(fpsBuffer, rowInChar, aChar, fgc, bgc);
            fpsBuffer += 8;
        }
    }
    if (showVolume > 0 && line >= 120 && line < 128)
    {
        fpsBuffer = lb + 120;
        // fill charbuffer with 3 digits from volume
        charBuffer[0] = volumeOperator;
        charBuffer[1] = '0' + (volume / 100);
        charBuffer[2] = '0' + ((volume % 100) / 10);
        charBuffer[3] = '0' + (volume % 10);
        charBuffer[4] = 0;

        int rowInChar = line % 8;
        for (auto i = 0; i < strlen(charBuffer); i++)
        {
            char aChar = charBuffer[i];
            DisplayChar(fpsBuffer, rowInChar, aChar, fgc, bgc);
            fpsBuffer += 8;
        }
    }
#ifdef SPEAKER_ENABLED
    if (showSpeakerMode > 0 && line >= 120 && line < 128)
    {
        int l = (strlen(modeStrings[mode]) * 8) / 2;
        fpsBuffer = lb + 120 - l;
        int rowInChar = line % 8;
        for (auto i = 0; i < strlen(modeStrings[mode]); i++)
        {
            char aChar = modeStrings[mode][i];
            DisplayChar(fpsBuffer, rowInChar, aChar, fgc, bgc);
            fpsBuffer += 8;
        }
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
    // InfoNES_Main() のループで最初に呼ばれる
    return loadAndReset() ? 0 : -1;
    // return 0;
}

using namespace picosystem;

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
#if 0
            if (volume > 0)
            {
                // int scaler = 600;
#endif
#ifdef SPEAKER_ENABLED
//                int scaler = 20;
                unsigned int freq = final_wave[fw_rd][i] * volume / FW_VOL_MAX;
                switch (mode)
                {
                case 0: // piezo only
                    pwm_set_gpio_level(11, freq * 8);
                    pwm_set_gpio_level(1, 0);
                    break;
                case 1: // speaker only
                    pwm_set_gpio_level(11, 0);
                    pwm_set_gpio_level(1, freq);
                    break;
                case 2: // both only
                    pwm_set_gpio_level(11, freq * 8);
                    pwm_set_gpio_level(1, freq);
                    break;
                case 3: // mute all
                    pwm_set_gpio_level(11, 0);
                    pwm_set_gpio_level(1, 0);
                    break;
                }
#else
                picosystem::psg_vol((scaler * final_wave[fw_rd][i] * volume) / (255 + scaler / volume));
#endif
#if 0
            }
            else
                picosystem::psg_vol(0);
#endif
            while (et < nt)
            {
                et = to_us_since_boot(get_absolute_time());
                sleep_us(1);
            }
        }
        final_wave[fw_rd][0] = -1;
        fw_rd = fw_wr;
    }
}

int main()
{
    char errorMessage[30];
    strcpy(errorMessage, "");
    _init_hardware();
    //    _start_audio();
    // set_fw_vol(50);
    //    set_fw_vol(0); // for mute

    fw_wr = fw_rd = 0;
    final_wave[0][0] = final_wave[1][0] = -1;
    multicore_launch_core1(fw_callback);

    backlight(100);
    memset(scanlinebuffer0, 0, sizeof(scanlinebuffer0));
    memset(scanlinebuffer1, 0, sizeof(scanlinebuffer1));

    stdio_init_all();
    // printf("Start program, flash size = %d\n", PICO_FLASH_SIZE_BYTES);
    printf("Start program\n");

#ifdef LED_ENABLED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
#endif
    // romSelector_.init(NES_FILE_ADDR);

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

    // When system is rebooted after dlashing SRAM, load the saved state from flash and proceed.
    loadState();

    if (watchdog_caused_reboot() && strncmp((char *)SRAM, "STA", 3) == 0)
    {

        // Game which caused the reboot
        // When reboot is caused by built-in game, startingGame will be -1
        int8_t startingGame = (int8_t)SRAM[3];
        printf("Game caused reboot: %d\n", startingGame);
        // + start next Game
        // - start previous game
        // R reset to menu
        // B Start built-in Game
        char advance = (char)SRAM[4];
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
        if (advance == 'B')
            romSelector_.selectcustomrom();
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
            romSelector_.setRomIndex(menu(NES_FILE_ADDR, errorMessage));
        }
    }

    return 0;
}
