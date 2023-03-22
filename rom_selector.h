#ifndef _22B2B909_1134_6471_AE6D_14EF3AF46BF0
#define _22B2B909_1134_6471_AE6D_14EF3AF46BF0

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "builtinrom.h"
//#include "tar.h"

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
    const uint8_t *customROM_{};
   // std::vector<TAREntry> entries_;

    // int selectedIndex_ = 0;

public:
    void init(uintptr_t addr)
    {
        auto *p = reinterpret_cast<const uint8_t *>(addr);
        BuiltInRom_ = reinterpret_cast<const uint8_t *>(builtinrom);
        if (checkNESMagic(p))
        {
            printf("Found custom rom\n");
            singleROM_ = customROM_ = p;
            printf("Single ROM.\n");
            return;
        } else {
            printf("No rom found, starting Blade Buster\n");
            singleROM_ = customROM_  = BuiltInRom_;
            printf("Single ROM.\n");
            return;
        }

        // entries_ = parseTAR(p, checkNESMagic);
        // printf("%zd ROMs.\n", entries_.size());
        // for (auto &e : entries_)
        // {
        //     printf("  %s: %p, %zd\n", e.filename.data(), e.data, e.size);
        // }
    }

    const uint8_t *getCurrentROM() const
    {
        if (singleROM_)
        {
            return singleROM_;
        }
        // if (!entries_.empty())
        // {
        //     return entries_[selectedIndex_].data;
        // }
        return {};
    }

    int getCurrentNVRAMSlot() const
    {
        auto currentROM = getCurrentROM();
        if (!currentROM)
        {
            return -1;
        }
        if (!hasNVRAM(currentROM))
        {
            return -1;
        }
        if (singleROM_)
        {
            return 0;
        }
        // int slot = 0;
        // for (int i = 0; i < selectedIndex_; ++i)
        // {
        //     if (hasNVRAM(entries_[i].data))
        //     {
        //         ++slot;
        //     }
        // }
        // return slot;
        return -1;
    }

    void StartCustomRom() {
            printf("Starting custom rom.\n");
            singleROM_ = customROM_;
    }

    void StartBladeBuster() {
            printf("Starting built-in rom.\n");
            singleROM_ = BuiltInRom_;
    }
    // void next()
    // {
    //     if (singleROM_ || entries_.empty())
    //     {
    //         return;
    //     }
    //     ++selectedIndex_;
    //     if (selectedIndex_ == static_cast<int>(entries_.size()))
    //     {
    //         selectedIndex_ = 0;
    //     }
    // }

    // void prev()
    // {
    //     if (singleROM_ || entries_.empty())
    //     {
    //         return;
    //     }
    //     --selectedIndex_;
    //     if (selectedIndex_ < 0)
    //     {
    //         selectedIndex_ = static_cast<int>(entries_.size() - 1);
    //     }
    // }
};

#endif /* _22B2B909_1134_6471_AE6D_14EF3AF46BF0 */
