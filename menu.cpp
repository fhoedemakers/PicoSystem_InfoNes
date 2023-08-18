#include <stdio.h>
#include <string.h>

#include "FrensHelpers.h"
#include <pico.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "InfoNES_System.h"

#include "hardware/watchdog.h"
#include "InfoNES.h"
#include "font_8x8.h"
#include "RomLister.h"
#include "gpbuttons.h"
#include "menu.h"

#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 8
#define FONT_N_CHARS 95
#define FONT_FIRST_ASCII 32
#define SCREEN_COLS 31
#define SCREEN_ROWS 30

#define STARTROW 2
#define ENDROW 25
#define PAGESIZE (ENDROW - STARTROW + 1)

#define VISIBLEPATHSIZE (SCREEN_COLS - 3)

void screenMode(int incr);
extern const WORD __not_in_flash_func(NesPalette)[];

#define CBLACK 15
#define CWHITE 48
#define CRED 6
#define CGREEN 10
#define CBLUE 2
#define CLIGHTBLUE 0x2C
#define DEFAULT_FGCOLOR CBLACK // 60
#define DEFAULT_BGCOLOR CWHITE

static int fgcolor = DEFAULT_FGCOLOR;
static int bgcolor = DEFAULT_BGCOLOR;
DWORD PAD1_Latch_Menu, PAD1_Prev;
ROMSelector romSelector_;
struct charCell
{
    uint8_t fgcolor : 4;
    uint8_t bgcolor : 4;
    char charvalue;
};

#define SCREENBUFCELLS SCREEN_ROWS *SCREEN_COLS
static charCell *screenBuffer;

static WORD *WorkLineRom = nullptr;
void RomSelect_SetLineBuffer(WORD *p, WORD size)
{
    WorkLineRom = p;
}

static DWORD prevButtons{};

void RomSelect_PadState(DWORD *pdwPad1, bool ignorepushed = false)
{

    auto &dst = *pdwPad1;

    int v = getbuttons();

    *pdwPad1 = 0;

    unsigned long pushed;

    if (ignorepushed == false)
    {
        pushed = v & ~prevButtons;
    }
    else
    {
        pushed = v;
    }
    // if (p1 & SELECT)
    // {
    //     if (pushed & UP)
    //     {
    //         screenMode(-1);
    //         v = 0;
    //     }
    //     else if (pushed & DOWN)
    //     {
    //         screenMode(+1);
    //         v = 0;
    //     }
    // }
    if (pushed)
    {
        *pdwPad1 = v;
    }
    prevButtons = v;
}
void RomSelect_DrawLine(int line, int selectedRow)
{
    WORD fgcolor, bgcolor;
    memset(WorkLineRom, 0, 640);

    for (auto i = 0; i < SCREEN_COLS; ++i)
    {
        int charIndex = i + line / FONT_CHAR_HEIGHT * SCREEN_COLS;
        int row = charIndex / SCREEN_COLS;
        uint c = screenBuffer[charIndex].charvalue;
        if (row == selectedRow)
        {
            fgcolor = NesPalette[screenBuffer[charIndex].bgcolor];
            bgcolor = NesPalette[screenBuffer[charIndex].fgcolor];
        }
        else
        {
            fgcolor = NesPalette[screenBuffer[charIndex].fgcolor];
            bgcolor = NesPalette[screenBuffer[charIndex].bgcolor];
        }

        int rowInChar = line % FONT_CHAR_HEIGHT;
        char fontSlice = font_8x8[(c - FONT_FIRST_ASCII) + (rowInChar)*FONT_N_CHARS];
        for (auto bit = 0; bit < 8; bit++)
        {
            if (fontSlice & 1)
            {
                *WorkLineRom = fgcolor;
            }
            else
            {
                *WorkLineRom = bgcolor;
            }
            fontSlice >>= 1;
            WorkLineRom++;
        }
    }
    return;
}

void drawline(int scanline, int selectedRow)
{
    RomSelect_PreDrawLine(scanline);
    RomSelect_DrawLine(scanline, selectedRow);
    InfoNES_PostDrawLine(scanline, true);
}

static void putText(int x, int y, const char *text, int fgcolor, int bgcolor)
{

    if (text != nullptr)
    {
        auto index = y * SCREEN_COLS + x;
        auto maxLen = strlen(text);
        if (strlen(text) > SCREEN_COLS - x)
        {
            maxLen = SCREEN_COLS - x;
        }
        while (index < SCREENBUFCELLS && *text && maxLen > 0)
        {
            screenBuffer[index].charvalue = *text++;
            screenBuffer[index].fgcolor = fgcolor;
            screenBuffer[index].bgcolor = bgcolor;
            index++;
            maxLen--;
        }
    }
}

void DrawScreen(int selectedRow)
{
    for (auto line = 0; line < 240; line++)
    {
        drawline(line, selectedRow);
    }
}

void ClearScreen(charCell *screenBuffer, int color)
{
    for (auto i = 0; i < SCREENBUFCELLS; i++)
    {
        screenBuffer[i].bgcolor = color;
        screenBuffer[i].fgcolor = color;
        screenBuffer[i].charvalue = ' ';
    }
}

void displayRoms(Frens::RomLister romlister, int startIndex)
{
    char buffer[ROMLISTER_MAXPATH + 4];
    auto y = STARTROW;
    
    auto entries = romlister.GetEntries();
    ClearScreen(screenBuffer, bgcolor);
    putText(1, 0, "Choose a rom to play:", fgcolor, bgcolor);
    putText(1, SCREEN_ROWS - 1, "A:Select, B:Back   " PICO_PROGRAM_VERSION_STRING, fgcolor, bgcolor);
    for (auto index = startIndex; index < romlister.Count(); index++)
    {
        if (y <= ENDROW)
        {
            auto info = entries[index];
            if (info.IsDirectory)
            {
                snprintf(buffer, sizeof(buffer), "D %s", info.Path);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "R %s", info.Path);
            }

            putText(1, y, buffer, fgcolor, bgcolor);
            y++;
        }
    }
}

void DisplayFatalError(char *error)
{
    ClearScreen(screenBuffer, bgcolor);
    putText(0, 0, "Fatal error:", fgcolor, bgcolor);
    putText(1, 3, error, fgcolor, bgcolor);
    while (true)
    {
        auto frameCount = InfoNES_LoadFrame();
        DrawScreen(-1);
    }
}

void DisplayEmulatorErrorMessage(char *error)
{
    //DWORD PAD1_Latch;
    ClearScreen(screenBuffer, bgcolor);
    putText(0, 0, "Error occured:", fgcolor, bgcolor);
    putText(0, 3, error, fgcolor, bgcolor);
    putText(0, ENDROW, "Press a button to continue.", fgcolor, bgcolor);
    while (true)
    {
        auto frameCount = InfoNES_LoadFrame();
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch);
        if (PAD1_Latch_Menu> 0)
        {
            return;
        }
    }
}

void showSplashScreen()
{
    //DWORD PAD1_Latch;
    char s[SCREEN_COLS];
    ClearScreen(screenBuffer, bgcolor);

    strcpy(s, "PicoSystem-Info");
    putText(SCREEN_COLS / 2 - (strlen(s) + 4) / 2, 2, s, fgcolor, bgcolor);

    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 13, 2, "N", CRED, bgcolor);
    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 14, 2, "E", CGREEN, bgcolor);
    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 15, 2, "S", CBLUE, bgcolor);
    //putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 10, 2, "+", fgcolor, bgcolor);
    strcpy(s, "Emulator");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 5, s, fgcolor, bgcolor);
    strcpy(s, "@jay_kumogata");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 6, s, CLIGHTBLUE, bgcolor);

    strcpy(s, "Pico Port");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 9, s, fgcolor, bgcolor);
    strcpy(s, "@shuichi_takano");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 10, s, CLIGHTBLUE, bgcolor);

    strcpy(s, "PicoSystem Port");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 13, s, fgcolor, bgcolor);
    strcpy(s, "@frenskefrens");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 14, s, CLIGHTBLUE, bgcolor);

    strcpy(s, "Sound programming");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 17, s, fgcolor, bgcolor);

    strcpy(s, "Sonny J Gray (newschooldev)");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 18, s, CLIGHTBLUE, bgcolor);
    strcpy(s, "and");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 19, s, fgcolor, bgcolor);

    strcpy(s, "@Layer812");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 20, s, CLIGHTBLUE, bgcolor);
   
    strcpy(s, PICO_PROGRAM_VERSION_STRING);
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 23, s, fgcolor, bgcolor);

    strcpy(s, "https://github.com/");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 25, s, CLIGHTBLUE, bgcolor);
    strcpy(s, "fhoedemakers/");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 26, s, CLIGHTBLUE, bgcolor);
     strcpy(s, "PicoSystem-infones");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 27, s, CLIGHTBLUE, bgcolor);
    int startFrame = -1;
    while (true)
    {
        auto frameCount = InfoNES_LoadFrame();
        if (startFrame == -1)
        {
            startFrame = frameCount;
        }
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch_Menu);
        if (PAD1_Latch_Menu> 0 || (frameCount - startFrame) > 500)
        {
            return;
        }
        if ((frameCount % 30) == 0)
        {
            for (auto i = 0; i < SCREEN_COLS; i++)
            {
                auto col = rand() % 63;
                putText(i, 0, " ", col, col);
                col = rand() % 63;
                putText(i, SCREEN_ROWS - 1, " ", col, col);
            }
            for (auto i = 1; i < SCREEN_ROWS - 1; i++)
            {
                auto col = rand() % 63;
                putText(1, i, " ", col, col);
                col = rand() % 63;
                putText(SCREEN_COLS - 1, i, " ", col, col);
            }
        }
    }
}

void screenSaver()
{
    //DWORD PAD1_Latch;
    WORD frameCount;
    while (true)
    {
        frameCount = InfoNES_LoadFrame();
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch_Menu);
        if (PAD1_Latch_Menu> 0)
        {
            return;
        }
        if ((frameCount % 3) == 0)
        {
            auto color = rand() % 63;
            auto row = rand() % SCREEN_ROWS;
            auto column = rand() % SCREEN_COLS;
            putText(column, row, " ", color, color);
        }
    }
}
// Global instances of local vars in romselect() some used in Lambda expression later on
static char *selectedRomOrFolder;
static bool errorInSavingRom = false;
static char *globalErrorMessage;
static uintptr_t FLASH_ADDRESS;
static bool showSplash = true;
 
int menu(uintptr_t NES_FILE_ADDR, char *errorMessage, bool nosplash)
{

    FLASH_ADDRESS = NES_FILE_ADDR;
    int firstVisibleRowINDEX = 0;
    int selectedRow = STARTROW;

    int totalFrames = -1;

    globalErrorMessage = errorMessage;

   

    int horzontalScrollIndex = 0;
    printf("Starting Menu\n");
    size_t ramsize;
    // Borrow Emulator RAM buffer for screen.
    screenBuffer = (charCell *)InfoNes_GetRAM(&ramsize);
    size_t chr_size;
    // Borrow ChrBuffer to store directory contents
    void *buffer = InfoNes_GetChrBuf(&chr_size);
    Frens::RomLister romlister(NES_FILE_ADDR, buffer, chr_size);
    romlister.list();
    if (romlister.Count() == 0)
    {
        return -1;
    }
    if (strlen(errorMessage) > 0)
    {

        DisplayEmulatorErrorMessage(errorMessage); // Emulator cannot start, show error

        showSplash = false;
    }
    if (showSplash)
    {
        showSplash = false;
        if ( nosplash == false ) {
            showSplashScreen();
        }
    }
    Frens::RomLister::RomEntry *entries;
    displayRoms(romlister, firstVisibleRowINDEX);
    int index = -1;

    while (1)
    {
        
        auto frameCount = InfoNES_LoadFrame();
        index = selectedRow - STARTROW + firstVisibleRowINDEX;
        entries = romlister.GetEntries();
        selectedRomOrFolder = (romlister.Count() > 0) ? entries[index].Path : nullptr;
        errorInSavingRom = false;
        DrawScreen(selectedRow);
        DWORD padcheck = prevButtons; // is reset in romselect. 
        
        RomSelect_PadState(&PAD1_Latch_Menu);
       // if (PAD1_Latch_Menu == padcheck) // no change
         //   continue;
       

        if (PAD1_Latch_Menu> 0)
        {
            totalFrames = frameCount; // Reset screenSaver
            // reset horizontal scroll of highlighted row
            horzontalScrollIndex = 0;
            putText(3, selectedRow, selectedRomOrFolder, fgcolor, bgcolor);
            if (selectedRomOrFolder)
            {

            }
            if ((PAD1_Latch_Menu& GPUP) && !(padcheck & GPUP))
            {
                if (selectedRow > STARTROW)
                {
                    selectedRow--;
                }
                else
                {
                    if (firstVisibleRowINDEX > 0)
                    {
                        firstVisibleRowINDEX--;
                        displayRoms(romlister, firstVisibleRowINDEX);
                    }
                }
            }
            else if ((PAD1_Latch_Menu& GPDOWN) && !(padcheck & GPDOWN))
            {
                if (selectedRow < ENDROW && (index) < romlister.Count() - 1)
                {
                    selectedRow++;
                }
                else
                {
                    if (index < romlister.Count() - 1)
                    {
                        firstVisibleRowINDEX++;
                        displayRoms(romlister, firstVisibleRowINDEX);
                    }
                }
            }
            if ((PAD1_Latch_Menu& GPLEFT) && !(padcheck & GPLEFT))
            {
                firstVisibleRowINDEX -= PAGESIZE;
                if (firstVisibleRowINDEX < 0)
                {
                    int index = romlister.Count() / (float)PAGESIZE;
                    int lastpage = romlister.Count() - (index * PAGESIZE);
                    if (lastpage > 0)
                    {
                        firstVisibleRowINDEX = romlister.Count() - lastpage - 1;
                    }
                    else
                    {

                        firstVisibleRowINDEX = romlister.Count() - (PAGESIZE );//Wrap attempt
                    }
                }
                selectedRow = STARTROW;
                displayRoms(romlister, firstVisibleRowINDEX);
            }
            else if ((PAD1_Latch_Menu& GPRIGHT) && !(padcheck & GPRIGHT))
            {
                if (firstVisibleRowINDEX + PAGESIZE < romlister.Count())
                {
                    firstVisibleRowINDEX += PAGESIZE;
                }
                else
                {
                    firstVisibleRowINDEX = 0;
                }
                selectedRow = STARTROW;
                displayRoms(romlister, firstVisibleRowINDEX);
            }
            if ((PAD1_Latch_Menu& GPB) && !(padcheck & GPB))
            {
                // start emulator with currently loaded game
                index = -1;
                break;
            }
            // else if ((PAD1_Latch_Menu& GPY) == GPY && (PAD1_Latch_Menu& GPX) != GPX)
            // {
            //     // start emulator with currently loaded game
            //     index = -1;
            //     break;
            // }
            if ((PAD1_Latch_Menu& GPA) && !(padcheck & GPA)) //prevent button hold from micro menu exit. 
            {
                break;

                // if (entries[index].IsDirectory)
                // {
                //     romlister.list(selectedRomOrFolder);
                //     firstVisibleRowINDEX = 0;
                //     selectedRow = STARTROW;
                //     displayRoms(romlister, firstVisibleRowINDEX);
                // }
            }
        }
        // scroll selected row horizontally if textsize exceeds rowlength
        if (selectedRomOrFolder)
        {
            if ((frameCount % 30) == 0)
            {
                if (strlen(selectedRomOrFolder + horzontalScrollIndex) > VISIBLEPATHSIZE)
                {
                    horzontalScrollIndex++;
                }
                else
                {
                    horzontalScrollIndex = 0;
                }
                putText(3, selectedRow, selectedRomOrFolder + horzontalScrollIndex, fgcolor, bgcolor);
            }
        }
        if (totalFrames == -1)
        {
            totalFrames = frameCount;
        }
        if ((frameCount - totalFrames) > 1200)
        {
            printf("Starting screensaver\n");
            totalFrames = -1;
            screenSaver();
            displayRoms(romlister, firstVisibleRowINDEX);
        }
    }
    // Wait until user has released all buttons
    //what is the purpose?
    /*
    while (1)
    {
        InfoNES_LoadFrame();
        DrawScreen(-1);
        RomSelect_PadState(&PAD1_Latch_Menu, true);
        if (PAD1_Latch_Menu== 0)
        {
            break;
        }
    }*/
    InfoNES_LoadFrame();
    DrawScreen(-1);
    
    if ( index != -1) {
        index = entries[index].Index;
    }
    return index;
}
