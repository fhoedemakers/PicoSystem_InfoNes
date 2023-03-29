/*
 * author : Shuichi TAKANO
 * since  : Mon Aug 09 2021 03:01:23
 * Modified by Frank Hoedemakers 3/2023
 */

#include "tar.h"
#include <optional>
#include <string.h>
#include <stdio.h>

namespace
{
    std::optional<size_t>
    parseOct(const char *p, int count)
    {
        size_t v = 0;
        while (count)
        {
            char ch = *p++;
            if (ch == 0 || ch == 0x20)
            {
                return v;
            }

            if (ch < '0' || ch >= '8')
            {
                return {};
            }

            v <<= 3;
            v |= ch - '0';

            --count;
        }
        return v;
    }

    bool isAll0(const char *p, size_t s)
    {
        while (s--)
        {
            if (*p++)
            {
                return false;
            }
        }
        return true;
    }
}

// std::vector<TAREntry>
// parseTAR(const void *_p, bool (*validateEntry)(const uint8_t *))
// {
//     auto p = static_cast<const char *>(_p);
//     std::vector<TAREntry> r;

//     while (1)
//     {
//         if (isAll0(p, 1024))
//         {
//             return r;
//         }

//         TAREntry e;
//         if (strncmp("ustar", p + 257, 5) != 0)
//         {
//             printf("parseTAR: invalid magic\n");
//             return {};
//         }
//         if (auto size = parseOct(p + 124, 12))
//         {
//             e.size = size.value();
//         }
//         else
//         {
//             printf("parseTAR: invalid size\n");
//             return {};
//         }
//         e.filename = p;
//         e.data = reinterpret_cast<const uint8_t *>(p + 512);
//         if (!validateEntry || validateEntry(e.data))
//         {
//             r.push_back(std::move(e));
//         }

//         p += (e.size + 512 + 511) & ~511;
//     }
// }

int GetValidTAREntries(const void *_p, bool (*validateEntry)(const uint8_t *))
{
    int count = 0;
    auto p = static_cast<const char *>(_p);
    std::optional<size_t> size;
    const uint8_t *data;
    while (1)
    {
        if (isAll0(p, 1024))
        {
            break;
        }
        if (strncmp("ustar", p + 257, 5) != 0)
        {
            printf("parseTAR: invalid magic\n");
            return 0;
        }
        if (!(size = parseOct(p + 124, 12)))
        {
            printf("parseTAR: invalid size\n");
            return {};
        }
        data = reinterpret_cast<const uint8_t *>(p + 512);
        if (!validateEntry || validateEntry(data))
        {
            count++;
        }
        // else
        // {
        //     printf("Invalid: %s: %d\n", p, size.value());
        // }
        p += (size.value() + 512 + 511) & ~511;
    }
    return count;
}

TAREntry extractTAREntryatindex(int index, const void *_p, bool (*validateEntry)(const uint8_t *))
{
    TAREntry e{};
    auto p = static_cast<const char *>(_p);
    int i = 0;
    std::optional<size_t> size;
    const uint8_t *data;
    while (1)
    {
        if (isAll0(p, 1024))
        {
            break;
        }
        if (strncmp("ustar", p + 257, 5) != 0)
        {
            printf("parseTAR: invalid magic\n");
            return {};
        }
        if (!(size = parseOct(p + 124, 12)))
        {
            printf("parseTAR: invalid size\n");
            return {};
        }
        data = reinterpret_cast<const uint8_t *>(p + 512);

        // Archive contains files like ./PaxHeaders.5260/
        if (!validateEntry || validateEntry(data))
        {
            if (index == i)
            {
                e.filename = p;
                e.data = data;
                e.size = size.value();
                break;
            }
            i++;
        }
        p += (size.value() + 512 + 511) & ~511;
    }
    return e;
}