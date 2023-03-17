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
//#include <util/dump_bin.h>
//#include <util/exclusive_proc.h>
//#include <util/work_meter.h>
#include <string.h>
#include <stdarg.h>
#include <algorithm>
#include "pico/util/queue.h"
#include "pico/multicore.h"

#include <InfoNES.h>
#include <InfoNES_System.h>
#include <InfoNES_pAPU.h>

// #include <dvi/dvi.h>
// NOUSB #include <tusb.h>
#include <gamepad.h>
#include "rom_selector.h"
#include "menu.h"
//#include "rgbled.hpp"
//#define NESPAD
#ifdef NESPAD
#include "nespad.h"
#endif

#include "ff.h"
#include "fgraphics.h"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

#define ERRORMESSAGESIZE 40
#define GAMESAVEDIR "/SAVES"
//util::ExclusiveProc exclProc_;
char *ErrorMessage;
bool isFatalError = false;
static FATFS fs;
char *romName;
//pimoroni::RGBLED led(6, 7, 8);
namespace
{
    constexpr uint32_t CPUFreqKHz = 252000;
    static constexpr uintptr_t NES_FILE_ADDR = 0x10080000;

    ROMSelector romSelector_;
}

const WORD __not_in_flash_func(NesPalette)[] = {
    0x2C63,
    0x6D01,
    0xEF10,
    0x8F38,
    0x4C60,
    0x4670,
    0x6070,
    0xC058,
    0x4031,
    0xA009,
    0xE001,
    0xE201,
    0xC801,
    0x0000,
    0x0000,
    0x0000,
    0x75AD,
    0x160B,
    0x9A42,
    0x197A,
    0xB5A1,
    0xAEC1,
    0xE6B9,
    0x409A,
    0xE06A,
    0x6033,
    0xA003,
    0xC703,
    0x8F03,
    0x0000,
    0x0000,
    0x0000,
    0xFFFF,
    0x9F5D,
    0xE08F,
    0x9FCC,
    0x3FF4,
    0x18FC,
    0x4FFC,
    0xC9EC,
    0x65BD,
    0xE585,
    0xEA55,
    0x513E,
    0x193E,
    0x694A,
    0x0000,
    0x0000,
    0xFFFF,
    0x5FBF,
    0xDFD6,
    0x9FEE,
    0x7FFE,
    0x7CFE,
    0x79FE,
    0xB6FE,
    0xF5E6,
    0x15CF,
    0x57BF,
    0x5AAF,
    0x3DAF,
    0xB6B5,
    0x0000,
    0x0000};

static queue_t call_queue;
typedef struct
{
    int scanline;
    int bufferindex;
    bool startframe;
    bool endframe;
} queue_entry_t;
static queue_entry_t entry;

#define SCANLINEBUFFERLINES 24
#define SCANLINEPIXELS 240    // 320
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
    return NES_FILE_ADDR - SRAM_SIZE * (slot + 1);
}

// void saveNVRAM()
// {
//     if (!SRAMwritten)
//     {
//         printf("SRAM not updated.\n");
//         return;
//     }

//     printf("save SRAM\n");
//     exclProc_.setProcAndWait([]
//                              {
//         static_assert((SRAM_SIZE & (FLASH_SECTOR_SIZE - 1)) == 0);
//         if (auto addr = getCurrentNVRAMAddr())
//         {
//             auto ofs = addr - XIP_BASE;
//             printf("write flash %x\n", ofs);
//             {
//                 flash_range_erase(ofs, SRAM_SIZE);
//                 flash_range_program(ofs, SRAM, SRAM_SIZE);
//             }
//         } });
//     printf("done\n");

//     SRAMwritten = false;
// }
// void loadNVRAM()
// {
//     if (auto addr = getCurrentNVRAMAddr())
//     {
//         printf("load SRAM %x\n", addr);
//         memcpy(SRAM, reinterpret_cast<void *>(addr), SRAM_SIZE);
//     }
//     SRAMwritten = false;
// }

void saveNVRAM()
{
    char pad[FF_MAX_LFN];

    if (!SRAMwritten)
    {
        printf("SRAM not updated.\n");
        return;
    }
    snprintf(pad, FF_MAX_LFN, "%s/%s.SAV", GAMESAVEDIR, romName);
    printf("save SRAM to %s\n", pad);
    FIL fil;
    FRESULT fr;
    fr = f_open(&fil, pad, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot open save file: %d", fr);
        printf("%s\n", ErrorMessage);
        return;
    }
    size_t bytesWritten;
    fr = f_write(&fil, SRAM, SRAM_SIZE, &bytesWritten);
    if (bytesWritten < SRAM_SIZE)
    {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Error writing save: %d %d/%d written", fr, bytesWritten, SRAM_SIZE);
        printf("%s\n", ErrorMessage);
    }
    f_close(&fil);
    printf("done\n");
    SRAMwritten = false;
}

bool loadNVRAM()
{
    char pad[FF_MAX_LFN];
    FILINFO fno;
    bool ok = false;
    snprintf(pad, FF_MAX_LFN, "%s/%s.SAV", GAMESAVEDIR, romName);
    FIL fil;
    FRESULT fr;

    size_t bytesRead;
    if (auto addr = getCurrentNVRAMAddr())
    {
        fr = f_stat(pad, &fno);
        if (fr == FR_NO_FILE)
        {
            printf("Save file not found, load SRAM from flash %x\n", addr);
            memcpy(SRAM, reinterpret_cast<void *>(addr), SRAM_SIZE);
            ok = true;
        }
        else
        {
            if (fr == FR_OK)
            {
                printf("Loading save file %s\n", pad);
                fr = f_open(&fil, pad, FA_READ);
                if (fr == FR_OK)
                {
                    fr = f_read(&fil, SRAM, SRAM_SIZE, &bytesRead);
                    if (fr == FR_OK)
                    {
                        printf("Savefile read from disk\n");
                        ok = true;
                    }
                    else
                    {
                        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot read save file: %d %d/%d read", fr, bytesRead, SRAM_SIZE);
                        printf("%s\n", ErrorMessage);
                    }
                }
                else
                {
                    snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot open save file: %d", fr);
                    printf("%s\n", ErrorMessage);
                }
                f_close(&fil);
            }
            else
            {
                snprintf(ErrorMessage, ERRORMESSAGESIZE, "f_stat() failed on save file: %d", fr);
                printf("%s\n", ErrorMessage);
            }
        }
    }
    else
    {
        ok = true;
    }
    SRAMwritten = false;
    return ok;
}

void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem)
{
    static constexpr int LEFT = 1 << 6;
    static constexpr int RIGHT = 1 << 7;
    static constexpr int UP = 1 << 4;
    static constexpr int DOWN = 1 << 5;
    static constexpr int SELECT = 1 << 2;
    static constexpr int START = 1 << 3;
    static constexpr int A = 1 << 0;
    static constexpr int B = 1 << 1;

    static DWORD prevButtons[2]{};
    static int rapidFireMask[2]{};
    static int rapidFireCounter = 0;

    ++rapidFireCounter;
    bool reset = false;

    for (int i = 0; i < 2; ++i)
    {
        auto &dst = i == 0 ? *pdwPad1 : *pdwPad2;
        auto &gp = io::getCurrentGamePadState(i);

        int v = (gp.buttons & io::GamePadState::Button::LEFT ? LEFT : 0) |
                (gp.buttons & io::GamePadState::Button::RIGHT ? RIGHT : 0) |
                (gp.buttons & io::GamePadState::Button::UP ? UP : 0) |
                (gp.buttons & io::GamePadState::Button::DOWN ? DOWN : 0) |
                (gp.buttons & io::GamePadState::Button::A ? A : 0) |
                (gp.buttons & io::GamePadState::Button::B ? B : 0) |
                (gp.buttons & io::GamePadState::Button::SELECT ? SELECT : 0) |
                (gp.buttons & io::GamePadState::Button::START ? START : 0) |
                0;
#ifdef NESPAD
        if (i == 0)
            v |= nespad_state;
#endif
        int rv = v;
        if (rapidFireCounter & 2)
        {
            // 15 fire/sec
            rv &= ~rapidFireMask[i];
        }

        dst = rv;

        auto p1 = v;

        auto pushed = v & ~prevButtons[i];

        if (p1 & SELECT)
        {
            // if (pushed & LEFT)
            // {
            //     saveNVRAM();
            //     romSelector_.prev();
            //     reset = true;
            // }
            // if (pushed & RIGHT)
            // {
            //     saveNVRAM();
            //     romSelector_.next();
            //     reset = true;
            // }
            if (pushed & START)
            {
                saveNVRAM();
                reset = true;
            }
            if (pushed & A)
            {
                rapidFireMask[i] ^= io::GamePadState::Button::A;
            }
            if (pushed & B)
            {
                rapidFireMask[i] ^= io::GamePadState::Button::B;
            }
        }

        prevButtons[i] = v;
    }

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
    vsnprintf(ErrorMessage, ERRORMESSAGESIZE, pszMsg, args);
    printf("%s", ErrorMessage);
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

static auto frame = 0;
int InfoNES_LoadFrame()
{
#ifdef NESPAD
    nespad_read_start();
#endif
    auto count = frame++;
    auto onOff = hw_divider_s32_quotient_inlined(count, 60) & 1;
    gpio_put(LED_PIN, onOff);
#ifdef NESPAD
    nespad_read_finish(); // Sets global nespad_state var
#endif
   // NO USB tuh_task();
    // sleep_ms(5);
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
    // util::WorkMeterMark(0xaaaa);
    // auto b = &lb[0];
    // util::WorkMeterMark(0x5555);
    // b.size --> 640
    // printf("Pre Draw%d\n", b->size());
    // WORD = 2 bytes
    // b->size = 640
    // printf("%d\n", b->size());

    // Note: First character is cutted off to the left with +32 offset

    // RomSelect_SetLineBuffer(b->data() + 32, b->size());
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
    //     memset(scanlinesbuffers[bufferIndex], 0, 4 * SCANLINEBYTESIZE);
    // }
    // printf("    Line in buffer: index %d lineinbuffer %d scanline %d\n", bufferIndex, lineInBuffer, line);
    WORD *b = scanlinesbuffers[bufferIndex] + lineInBuffer * SCANLINEPIXELS;
    // WORD *b = scanlinesbuffers[bufferIndex];
    //RomSelect_SetLineBuffer(b, SCANLINEBYTESIZE);
    RomSelect_SetLineBuffer(lb, sizeof(lb));
    // RomSelect_SetLineBuffer(b->data() + 34, b->size());

    //    (*b)[319] = line + dvi_->getFrameCounter();

    currentLineBuffer_ = lb;
}
void __not_in_flash_func(InfoNES_PostDrawLine)(int line, bool frommenu)
{
#if !defined(NDEBUG)
    util::WorkMeterMark(0xffff);
    drawWorkMeter(line);
#endif
    // printf("%d\n", line & 3);
    // static queue_t call_queue;
    // typedef struct {
    //     int scanline;
    //     int bufferindex;
    // } queue_entry_t;
    // static queue_entry_t entry;
    bufferIndex = hw_divider_s32_quotient_inlined(line, SCANLINEBUFFERLINES) & 1;
    lineInBuffer = hw_divider_s32_remainder_inlined(line, SCANLINEBUFFERLINES);
    // if (line == 4)
    // {
    //     memset(scanlinesbuffers[bufferIndex], 0, 4 * SCANLINEBYTESIZE);
    // }
    // printf("    Line in buffer: index %d lineinbuffer %d scanline %d\n", bufferIndex, lineInBuffer, line);
    WORD *b = scanlinesbuffers[bufferIndex] + lineInBuffer * SCANLINEPIXELS;
    memcpy(b, lb + 8, SCANLINEPIXELS * sizeof(WORD));
    if (prevbufferIndex != bufferIndex)
    {
       if (prevbufferIndex != -1) {
            entry.bufferindex = prevbufferIndex;
            //printf("Pushing index %d\n", entry.bufferindex);
            entry.startframe = startframe;
            entry.endframe = endframe;
            queue_add_blocking(&call_queue, &entry);
            if (frommenu) {
                 sleep_ms(5);
            }
            startframe = endframe = false;
       }
       //memset(scanlinesbuffers[bufferIndex], 0, sizeof(scanlinebuffer0));
       prevbufferIndex = bufferIndex;
    }
    if (line == 0)
    {
        startframe = true;
        //endframe = false;
    }
    if (line == 239 )
    {
        //startframe = false;
        endframe = true;
    }
    // if (line == 4)
    // {
    //     fstartframe();
    //     fwritescanline(sizeof(emptylines), (char *)emptylines);
    // }
    // fwritescanline(640, (char *)currentLineBuffer_);
    // if (line == 235)
    // {
    //     fwritescanline(sizeof(emptylines), (char *)emptylines);
    //     fendframe();
    // }
    assert(currentLineBuffer_);
    // dvi_->setLineBuffer(line, currentLineBuffer_);
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
           
        queue_entry_t qentry;
        queue_remove_blocking(&call_queue, &qentry);
        if (qentry.startframe ) {
                //printf("Startframe\n");
                fstartframe();
                //for (int i = 1; i<5 ; i++) 
                //fwritescanline(sizeof(emptylines), (char *)emptylines);
                //fwritescanline(sizeof(scanlinebuffer0),(char *)scanlinesbuffers[qentry.bufferindex]);
        }
        fwritescanline(sizeof(scanlinebuffer0),(char *)scanlinesbuffers[qentry.bufferindex]);
      
        // printf("Core 1 index: %d startframe: %d endframe: %d\n", qentry.bufferindex, qentry.startframe, qentry.endframe);
         if (qentry.endframe ) {
                //printf("Endframe\n");
                fendframe();
        }
      
        // printf("Line: %d\n", line);
        // if (line == 4)
        // {
        //     fstartframe();
        //     fwritescanline(sizeof(emptylines), (char *)emptylines);
        // }
        // fwritescanline(640, (char *)SCANLINESCORE1[line & SCANLINEMASK]);
        // if (line == 235)
        // {
        //     fwritescanline(sizeof(emptylines), (char *)emptylines);
        //     fendframe();
        // }
        // }

        // dvi_->unregisterIRQThisCore();
        // dvi_->stop();

        // exclProc_.processOrWaitIfExist();
    }
}

bool initSDCard()
{
    FRESULT fr;
    TCHAR str[40];
    sleep_ms(1000);

    printf("Mounting SDcard");
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK)
    {
       snprintf(ErrorMessage, ERRORMESSAGESIZE, "SD card mount error: %d", fr);
        printf("%s\n", ErrorMessage);
        return false;
    }
    printf("\n");

    fr = f_chdir("/");
    if (fr != FR_OK)
    {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot change dir to / : %d", fr);
        printf("%s\n", ErrorMessage);
        return false;
    }
    // for f_getcwd to work, set
    //   #define FF_FS_RPATH		2
    // in drivers/fatfs/ffconf.h
    fr = f_getcwd(str, sizeof(str));
    if (fr != FR_OK)
    {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot get current dir: %d", fr);
        printf("%s\n", ErrorMessage);
        return false;
    }
    printf("Current directory: %s\n", str);
    printf("Creating directory %s\n", GAMESAVEDIR);
    fr = f_mkdir(GAMESAVEDIR);
    if (fr != FR_OK)
    {
        if (fr == FR_EXIST)
        {
            printf("Directory already exists.\n");
        }
        else
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot create dir %s: %d", GAMESAVEDIR, fr);
            printf("%s\n", ErrorMessage);
            return false;
        }
    }
    return true;
}

int main()
{

    char selectedRom[80];
    romName = selectedRom;
  
    memset(scanlinebuffer0, 0, sizeof(scanlinebuffer0));
    memset(scanlinebuffer1, 0, sizeof(scanlinebuffer1));
    char errMSG[ERRORMESSAGESIZE];
    errMSG[0] = selectedRom[0] = 0;
    ErrorMessage = errMSG;
  
    // Overclocking messes up the display
    // vreg_set_voltage(VREG_VOLTAGE_1_20);
    // sleep_ms(10);
    // set_sys_clock_khz(CPUFreqKHz, true);
   
    stdio_init_all();
    printf("Start program\n");
   
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    // NOUSB tusb_init();

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
    //
    // dvi_ = std::make_unique<dvi::DVI>(pio0, &DVICONFIG,
    //                                   dvi::getTiming640x480p60Hz());
    // //    dvi_->setAudioFreq(48000, 25200, 6144);
    // dvi_->setAudioFreq(44100, 28000, 6272);

    // dvi_->allocateAudioBuffer(256);
    // //    dvi_->setExclusiveProc(&exclProc_);

    // dvi_->getBlankSettings().top = 4 * 2;
    // dvi_->getBlankSettings().bottom = 4 * 2;
    // // dvi_->setScanLine(true);

#ifdef NESPAD
    nespad_begin(CPUFreqKHz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT);
#endif
    // 空サンプル詰めとく
    // dvi_->getAudioRingBuffer().advanceWritePointer(255);

    queue_init(&call_queue, sizeof(queue_entry_t), 2);
    multicore_launch_core1(core1_main);

    printf("Initialising Graphics subsystem.\n");
    // finitgraphics(ROTATE_0, 320, 240);
     finitgraphics(ROTATE_270, 240, 240);
    // led.set_rgb(0,0,0);
    // isFatalError = !initSDCard();
    // When a game is started from the menu, the menu will reboot the device.
    // After reboot the emulator will start the selected game.
    // if (watchdog_caused_reboot() && isFatalError == false)
    // {
    //     // Determine loaded rom
    //     printf("Rebooted by menu\n");
    //     FIL fil;
    //     FRESULT fr;
    //     size_t tmpSize;
    //     printf("Reading current game from %s and starting emulator\n", ROMINFOFILE);
    //     fr = f_open(&fil, ROMINFOFILE, FA_READ);
    //     if (fr == FR_OK)
    //     {
    //         size_t r;
    //         fr = f_read(&fil, selectedRom, sizeof(selectedRom), &r);
    //         if (fr != FR_OK)
    //         {
    //             snprintf(ErrorMessage, 40, "Cannot read %s:%d\n", ROMINFOFILE, fr);
    //             selectedRom[0] = 0;
    //             printf(ErrorMessage);
    //         }
    //         else
    //         {
    //             selectedRom[r] = 0;
    //         }
    //     }
    //     else
    //     {
    //         snprintf(ErrorMessage, 40, "Cannot open %s:%d\n", ROMINFOFILE, fr);
    //         printf(ErrorMessage);
    //     }
    //     f_close(&fil);
    // }
    while (true)
    {
        // if (strlen(selectedRom) == 0)
        // {

        //     menu(NES_FILE_ADDR, ErrorMessage, isFatalError); // never returns, but reboots upon selecting a game
        // }
        // printf("Now playing: %s\n", selectedRom);
        romSelector_.init(NES_FILE_ADDR);
        InfoNES_Main();
        selectedRom[0] = 0;
    }

    return 0;
}
