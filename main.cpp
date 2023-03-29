#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/divider.h"
#include "hardware/dma.h"
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
        auto ofs = addr - XIP_BASE;
        printf("write flash %x --> %x\n", addr, ofs);
        _saveNVRAM(ofs, statevar, advance);
        printf("done\n");
        printf("Rebooting...\n");
        // Reboot after SRAM is flashed
        watchdog_enable(100, 1);
        while (1);
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

static DWORD prevButtons = 0;
static int rapidFireMask = 0;
static int rapidFireCounter = 0;
void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem)
{
    static constexpr int LEFT = 1 << 6;
    static constexpr int RIGHT = 1 << 7;
    static constexpr int UP = 1 << 4;
    static constexpr int DOWN = 1 << 5;
    static constexpr int Y = 1 << 2;
    static constexpr int X = 1 << 3;
    static constexpr int A = 1 << 0;
    static constexpr int B = 1 << 1;

    // moved variables outside function body because prevButtons gets initialized to 0 everytime the function is called.
    // This is strange because a static variable inside a function is only initialsed once and retains it's value
    // throughout different function calls.
    // Am i missing something?
    // static DWORD prevButtons = 0;
    // static int rapidFireMask = 0;
    // static int rapidFireCounter = 0;

    ++rapidFireCounter;
    bool reset = false;
    picosystem::_gpio_get2();

    auto &dst = *pdwPad1;

    int v = (picosystem::button(picosystem::LEFT) ? LEFT : 0) |
            (picosystem::button(picosystem::RIGHT) ? RIGHT : 0) |
            (picosystem::button(picosystem::UP) ? UP : 0) |
            (picosystem::button(picosystem::DOWN) ? DOWN : 0) |
            (picosystem::button(picosystem::Y) ? Y : 0) |
            (picosystem::button(picosystem::X) ? X : 0) |
            (picosystem::button(picosystem::A) ? A : 0) |
            (picosystem::button(picosystem::B) ? B : 0) |
            0;
    int rv = v;
    if (rapidFireCounter & 2)
    {
        // 15 fire/sec
        rv &= ~rapidFireMask;
    }

    dst = rv;

    auto p1 = v;

    auto pushed = v & ~prevButtons;

    if (pushed & X)
    {
        printf("%d\n", fps);
    }

    if (p1 & Y)
    {
        if (pushed & LEFT)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), '-');
            romSelector_.prev();
            reset = true;
        }
        if (pushed & RIGHT)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), '+');
            romSelector_.next();
            reset = true;
        }
        if (pushed & UP)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), 'B');
            romSelector_.selectcustomrom();
            reset = true;
        }
        if (pushed & X)
        {
            saveNVRAM(romSelector_.GetCurrentRomIndex(), 'R');
            reset = true;
        }
        if (pushed & A)
        {
            // rapidFireMask[i] ^= io::GamePadState::Button::A;
        }
        if (pushed & B)
        {
            // rapidFireMask[i] ^= io::GamePadState::Button::B;
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
    return 0;
}

void __not_in_flash_func(InfoNES_SoundOutput)(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5)
{
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
    // util::WorkMeterMark(0xaaaa);
    // auto b = &lb[0];
    // util::WorkMeterMark(0x5555);

    // b.size --> 640
    // printf("Pre Draw%d\n", b->size());
    // WORD = 2 bytes
    // b->size = 640
    // printf("%d\n", b->size());
    // InfoNES_SetLineBuffer(b->data() + 32, b->size());
    // InfoNES_SetLineBuffer(b, sizeof(lb));
    //
    // #define SCANLINEBUFFERLINES 20
    // #define SCANLINEPIXELS      320
    // #define SCANLINEBYTESIZE = (SCANLINEPIXELS * sizeof(WORD))
    // WORD scanlinebuffer0[SCANLINEPIXELS * SCANLINEBUFFERLINES];
    // WORD scanlinebuffer1[SCANLINEPIXELS * SCANLINEBUFFERLINES];
    // WORD *scanlinesbuffers[] = { scanlinebuffer0, scanlinebuffer1 };
    // 0 / 30  = 0   0000      0 % 30 = 0  1 % 30 = 1 ...
    // 30 / 30 = 1   0001
    // 60 / 30 = 2   0010
    // 90 / 30 = 3   0011
    // 120/ 30 = 4   0100
    // 150 / 30 = 5  0101
    // 180 / 30 = 6  0110
    // 210 / 30 = 7  0111
    bufferIndex = hw_divider_s32_quotient_inlined(line, SCANLINEBUFFERLINES) & 1;
    lineInBuffer = hw_divider_s32_remainder_inlined(line, SCANLINEBUFFERLINES);
    // if (line == 4)
    // {
    //     memset(scanlinesbuffers[bufferIndex], 0, 640 * 4);
    // }
    WORD *b = scanlinesbuffers[bufferIndex] + lineInBuffer * SCANLINEPIXELS;
    // InfoNES_SetLineBuffer(b + 32, SCANLINEBYTESIZE);
    InfoNES_SetLineBuffer(lb, sizeof(lb));
    //    (*b)[319] = line + dvi_->getFrameCounter();

    currentLineBuffer_ = lb;
}
bool startframe = false;
bool endframe = false;

void __not_in_flash_func(RomSelect_PreDrawLine)(int line)
{
}

void __not_in_flash_func(InfoNES_PostDrawLine)(int line, bool frommenu)
{
#if !defined(NDEBUG)
    util::WorkMeterMark(0xffff);
    drawWorkMeter(line);
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

void __not_in_flash_func(core1_main)()
{

    while (true)
    {
        // dvi_->registerIRQThisCore();
        // dvi_->waitForValidLine();

        // dvi_->start();
        // while (!exclProc_.isExist())
        // {

        // queue_entry_t qentry;
        // queue_remove_blocking(&call_queue, &qentry);
        // FH if (qentry.startframe ) {
        // FH fstartframe();
        // FH }
        // FH fwritescanline(sizeof(scanlinebuffer0),(char *)scanlinesbuffers[qentry.bufferindex]);

        // printf("Core 1 index: %d startframe: %d endframe: %d\n", qentry.bufferindex, qentry.startframe, qentry.endframe);
        // FH if (qentry.endframe ) {
        // FH  fendframe();
        // FH }
    }
}

bool initSDCard()
{
    // FRESULT fr;
    // TCHAR str[40];
    // sleep_ms(1000);

    // printf("Mounting SDcard");
    // fr = f_mount(&fs, "", 1);
    // if (fr != FR_OK)
    // {
    //    snprintf(ErrorMessage, ERRORMESSAGESIZE, "SD card mount error: %d", fr);
    //     printf("%s\n", ErrorMessage);
    //     return false;
    // }
    // printf("\n");

    // fr = f_chdir("/");
    // if (fr != FR_OK)
    // {
    //     snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot change dir to / : %d", fr);
    //     printf("%s\n", ErrorMessage);
    //     return false;
    // }
    // // for f_getcwd to work, set
    // //   #define FF_FS_RPATH		2
    // // in drivers/fatfs/ffconf.h
    // fr = f_getcwd(str, sizeof(str));
    // if (fr != FR_OK)
    // {
    //     snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot get current dir: %d", fr);
    //     printf("%s\n", ErrorMessage);
    //     return false;
    // }
    // printf("Current directory: %s\n", str);
    // printf("Creating directory %s\n", GAMESAVEDIR);
    // fr = f_mkdir(GAMESAVEDIR);
    // if (fr != FR_OK)
    // {
    //     if (fr == FR_EXIST)
    //     {
    //         printf("Directory already exists.\n");
    //     }
    //     else
    //     {
    //         snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot create dir %s: %d", GAMESAVEDIR, fr);
    //         printf("%s\n", ErrorMessage);
    //         return false;
    //     }
    // }
    return true;
}
using namespace picosystem;
int main()
{

    _init_hardware();
    _start_audio();
    backlight(75);
    memset(scanlinebuffer0, 0, sizeof(scanlinebuffer0));
    memset(scanlinebuffer1, 0, sizeof(scanlinebuffer1));

    stdio_init_all();
    //printf("Start program, flash size = %d\n", PICO_FLASH_SIZE_BYTES);
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
    if (watchdog_caused_reboot()  && strncmp((char *)SRAM, "STA", 3) == 0)
    {
        // Game which caused the reboot
        // When reboot is caused by built-in game, startingGame will be -1
        int8_t startingGame = (int8_t)SRAM[3];
        printf("Game caused reboot: %d\n", startingGame);
        // + start next Game
        // - start previous game 
        // R restart current game
        // B Start built-in Game
        char advance = (char)SRAM[4];
        int tmpGame = startingGame;
        // When coming from built-in game, just start the first game.
        if ( tmpGame == -1 && advance != 'R' ) tmpGame = 0;
        romSelector_.init(NES_FILE_ADDR, tmpGame);
        if (startingGame >= 0 && advance != 'R')   // R = Reset
        {
            if (advance == '+') romSelector_.next();
            if (advance == '-') romSelector_.prev();
            if (advance == 'B') romSelector_.selectcustomrom();
        }
        prevButtons = -1;
    }
    else
    {
        romSelector_.init(NES_FILE_ADDR, 0);
    }

    while (true)
    {       
        int index = romSelector_.GetCurrentRomIndex();
        printf("Starting '%s'.\n", romSelector_.GetCurrentGameName());
        InfoNES_Main();
    }

    return 0;
}
