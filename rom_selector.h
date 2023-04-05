#ifndef _22B2B909_1134_6471_AE6D_14EF3AF46BF0
#define _22B2B909_1134_6471_AE6D_14EF3AF46BF0

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tar.h"
#include "builtinrom.h"
#include "InfoNES_Mapper.h"

inline bool checkNESMagic(const uint8_t *data)
{
    bool ok = false;
    int nIdx;
    if (memcmp(data, "NES\x1a", 4) == 0)
    {
        uint8_t MapperNo = *(data + 6) >> 4;
        for (nIdx = 0; MapperTable[nIdx].nMapperNo != -1; ++nIdx)
        {
            if (MapperTable[nIdx].nMapperNo == MapperNo)
            {
                ok = true;
                break;
            }
        }
        // if (!ok)
        // {
        //     printf("checkNESMagic: skipped rom with invalid mapper #%d\n", MapperNo);
        // }
    }
    return ok;
}

inline bool hasNVRAM(const uint8_t *data)
{
    auto info1 = data[6];
    return info1 & 2;
}

class ROMSelector
{
    const uint8_t *singleROM_{};
    const uint8_t *BuiltInRom_{};
    int startBuiltIn_ = false;
    int selectedIndex_ = 0;
    int numberOfEntries = 0;
    const uint8_t *tarAddress{};

public:
    void init(uintptr_t addr, int startingRom)
    {

        auto *p = reinterpret_cast<const uint8_t *>(addr);
        BuiltInRom_ = reinterpret_cast<const uint8_t *>(builtinrom);
        if (checkNESMagic(p))
        {
            singleROM_ = p;
            printf("Single ROM.\n");
            return;
        }
        numberOfEntries = GetValidTAREntries(p, checkNESMagic);
        printf("%zd compatible ROMs in flash.\n", numberOfEntries);
        if (numberOfEntries > 0)
        {
            tarAddress = p;
           
            for (int i = 0; i < numberOfEntries; i++)
            {
                TAREntry e = extractTAREntryatindex(i, p, checkNESMagic);
                if (e.data)
                {
                    printf("  %s: %p, %zd\n", e.filename.data(), e.data, e.size);
                }
            }
        }
        if (startingRom == -1 || numberOfEntries == 0)
        {
            startBuiltIn_ = true;
        }
        else
        {
            selectedIndex_ = startingRom;
        }
    }

    const bool isBuiltInRomPlaying() const
    {
        return startBuiltIn_;
    }

    const uint8_t *getCurrentROM() const
    {
        if (startBuiltIn_ == false)
        {
            if (singleROM_)
            {
                return singleROM_;
            }
            if (numberOfEntries)
            {
                return extractTAREntryatindex(selectedIndex_, tarAddress, checkNESMagic).data;
            }
        }
        return BuiltInRom_;
    }

    int getCurrentNVRAMSlot() const
    {
        auto currentROM = getCurrentROM();
        if (!currentROM)
        {
            return -1;
        }
        if (startBuiltIn_)
        {

            if (hasNVRAM(BuiltInRom_))
            {
                printf("Built-in rom has  NVRAM, returning slot 0\n");
                return 0; // BuiltIn Ram has slot 0
            }
            printf("Built-in rom has no NVRAM, returning slot -1\n");
            return -1;
        }

        if (!hasNVRAM(currentROM))
        {
            printf("Current rom has no NVRAM, returning slot -1\n");
            return -1;
        }

        if (singleROM_)
        {
            printf("Custom rom (single rom) Rom has no NVRAM,returning slot -1\n");
            return 1; // Singlerom has slot 1
        }
        // Has Game in Tar archie a SRAM slot?
        int slot = 1;
        int foundSlot = -1;
        for (int i = 0; i <= selectedIndex_; ++i)
        {
            if (hasNVRAM(extractTAREntryatindex(i, tarAddress, checkNESMagic).data))
            {
                ++slot;
                if (i == selectedIndex_)
                {
                    foundSlot = slot;
                }
            }
        }
        if (foundSlot == -1)
        {
            // ROm in archive has no SRAM
            printf("Rom %d in tar archive has no NVRAM. Returning slot -1\n", selectedIndex_);
        }
        else
        {
            printf("Rom %d in tar archive has NVRAM. Returning slot %d\n", selectedIndex_, foundSlot);
        }
        return foundSlot;
    }

    void next()
    {
        startBuiltIn_ = false;
        if (singleROM_ || numberOfEntries == 0)
        {
            return;
        }
        ++selectedIndex_;
        printf("Next: Selected Index %d\n", selectedIndex_);
        if (selectedIndex_ == numberOfEntries)
        {
            selectedIndex_ = 0;
            printf("Next: reset Index %d\n", selectedIndex_);
        }
    }

    void prev()
    {
        startBuiltIn_ = false;
        if (singleROM_ || numberOfEntries == 0)
        {
            return;
        }
        --selectedIndex_;
        printf("Prev: Selected Index %d\n", selectedIndex_);
        if (selectedIndex_ < 0)
        {
            selectedIndex_ = numberOfEntries - 1;
            printf("Prev: reset Index %d\n", selectedIndex_);
        }
    }

    uint8_t GetCurrentRomIndex() const
    {
        return (startBuiltIn_ ? -1 : selectedIndex_);
    }
    void selectcustomrom()
    {
        startBuiltIn_ = true;
    }

    const char *GetCurrentGameName()
    {

        if (startBuiltIn_)
            return BUILTINROMNAME;
        return (char *)extractTAREntryatindex(selectedIndex_, tarAddress, checkNESMagic).filename.data();
    }

    void setRomIndex(int index)
    {
        if (numberOfEntries > 0)
        {
            if (index >= 0 && index < numberOfEntries)
            {
                selectedIndex_ = index;
                startBuiltIn_ = false;
            }
        }
    }
};

#endif /* _22B2B909_1134_6471_AE6D_14EF3AF46BF0 */