#ifndef _22B2B909_1134_6471_AE6D_14EF3AF46BF0
#define _22B2B909_1134_6471_AE6D_14EF3AF46BF0

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tar.h"
#include "builtinrom.h"

inline bool checkNESMagic(const uint8_t *data)
{
    return memcmp(data, "NES\x1a", 4) == 0;
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
    std::vector<TAREntry> entries_;
    int startBuiltIn_ = false;
    int selectedIndex_ = 0;

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

        entries_ = parseTAR(p, checkNESMagic);
        printf("%zd ROMs.\n", entries_.size());
        for (auto &e : entries_)
        {
            printf("  %s: %p, %zd\n", e.filename.data(), e.data, e.size);
        }
        if (startingRom == -1) {
            startBuiltIn_ = true;
        } else {
            selectedIndex_ = startingRom;
        }
    }

  
    std::string_view GetRomname() {
        if (startBuiltIn_ == false)
        {
            if (singleROM_)
            {
                return "unknown";
            }
            if (!entries_.empty())
            {
                return entries_[selectedIndex_].filename;
            }
        } else {
            return BUILTINROMNAME;
        }
    }
    const bool isBuiltInRomPlaying()  const {
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
            if (!entries_.empty())
            {
                return entries_[selectedIndex_].data;
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
        if (startBuiltIn_) {
            
            if ( hasNVRAM(BuiltInRom_)) {
               printf("Built-in rom has  NVRAM, returning slot 0\n");  
               return 0;    // BuiltIn Ram has slot 0
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
            return 1;  // Singlerom has slot 1
        }
        // Has Game in Tar archie a SRAM slot?
        int slot = 1;
        int foundSlot = -1;
        for (int i = 0; i <= selectedIndex_; ++i)
        {
            if (hasNVRAM(entries_[i].data))
            {             
                ++slot;
                if ( i == selectedIndex_) {
                    foundSlot = slot;
                }
            }
        }
        if ( foundSlot == -1) {
             // ROm in archive has no SRAM
             printf("Rom %d in tar archive has no NVRAM. Returning slot -1\n", selectedIndex_);
           
        } else {
             printf("Rom %d in tar archive has NVRAM. Returning slot %d\n", selectedIndex_, foundSlot);
        }     
        return foundSlot;
    }

    void next()
    {
        startBuiltIn_  = false;
        if (singleROM_ || entries_.empty())
        {
            return;
        }
        ++selectedIndex_;
        printf("Next: Selected Index %d\n", selectedIndex_);
        if (selectedIndex_ == static_cast<int>(entries_.size()))
        {
            selectedIndex_ = 0;
             printf("Next: reset Index %d\n", selectedIndex_);
        }
    }

    void prev()
    {
        startBuiltIn_  = false;
        if (singleROM_ || entries_.empty())
        {
            return;
        }
        --selectedIndex_;
        printf("Prev: Selected Index %d\n", selectedIndex_);
        if (selectedIndex_ < 0)
        {
            selectedIndex_ = static_cast<int>(entries_.size() - 1);
            printf("Prev: reset Index %d\n", selectedIndex_);
        }
    }

    uint8_t GetCurrentRomIndex() const {
        return (startBuiltIn_ ? -1 : selectedIndex_);
    }
    void selectcustomrom() {
        startBuiltIn_ = true;
    }
};

#endif /* _22B2B909_1134_6471_AE6D_14EF3AF46BF0 */