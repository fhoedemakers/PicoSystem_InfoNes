/*
 * author : Shuichi TAKANO
 * since  : Mon Aug 09 2021 02:50:15
 * Modified by Frank Hoedemakers 3/2023
 */
#ifndef _3A466107_0134_6471_2615_EA5DEBBBFAE6
#define _3A466107_0134_6471_2615_EA5DEBBBFAE6

#include <vector>
#include <string_view>
#include <stdint.h>

struct TAREntry
{
    std::string_view filename;
    const uint8_t *data{};
    size_t size = 0;
    // const uint8_t *nextTAR{};
    // const uint8_t *prevTAR{};
};

// std::vector<TAREntry>
// parseTAR(const void *_p, bool (*validateEntry)(const uint8_t *) = {});
int GetValidTAREntries(const void *_p, bool (*validateEntry)(const uint8_t *) = {});
TAREntry extractTAREntryatindex(int index, const void *_p, bool (*validateEntry)(const uint8_t *));

#endif /* _3A466107_0134_6471_2615_EA5DEBBBFAE6 */