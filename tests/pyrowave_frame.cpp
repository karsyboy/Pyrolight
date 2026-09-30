#ifdef TEST_WINDOWS_MINMAX
// Windows headers may precede this helper in production. Standard-library
// declarations are loaded first, just as they are in that include order.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#include "streaming/video/pyrowaveframe.h"
#ifdef TEST_WINDOWS_MINMAX
#undef min
#undef max
#endif
#include <cassert>
#include <cstdio>

static void word(std::vector<uint8_t>& data, unsigned x)
{
    for (int b=0;b<4;b++) data.push_back(x >> (8*b));
}
static std::vector<uint8_t> frame(int w, int h, bool c444, bool hdr)
{
    std::vector<uint8_t> data;
    word(data, 0x80000000 | 3u << 28 | (h-1) << 14 | (w-1));
    word(data, 1 | unsigned(c444) << 26 | (hdr ? 7u << 27 : 0));
    word(data, 2u << 16 | 3u << 28); word(data, 0);
    return data;
}
int main()
{
    std::vector<uint8_t> seen;
    for (auto extent : {std::pair<int,int>{130,134},{1920,1080},{2560,1440},{3440,1440},{3840,2160}})
        for (bool c444 : {false,true}) for (bool hdr : {false,true}) {
            auto data=frame(extent.first,extent.second,c444,hdr);
            auto valid=[&](const std::vector<uint8_t>& d) { return pyroWaveValidateFrame(d.data(),d.size(),extent.first,extent.second,c444,hdr,seen); };
            assert(valid(data));
            for (size_t n=0;n<data.size();n++) assert(!pyroWaveValidateFrame(data.data(),n,extent.first,extent.second,c444,hdr,seen));
            auto bad=data; bad.insert(bad.end(),data.begin(),data.begin()+8); assert(!valid(bad)); // Second sequence.
            bad=data; bad[11] ^= 0x10; assert(!valid(bad)); // Block from a different sequence.
            bad=data; bad[4]=2; assert(!valid(bad)); // Missing announced block.
            bad=data; bad.insert(bad.end(),data.begin()+8,data.end()); bad[4]=2; assert(!valid(bad)); // Duplicate block.
            bad=data; bad[7] ^= 0x10; assert(!valid(bad)); // Transfer does not match negotiation.
            bad=data; bad[7] ^= 0x08; assert(!valid(bad)); // Wrong primaries.
            bad=data; bad[7] ^= 0x20; assert(!valid(bad)); // Wrong matrix.
            bad=data; bad[10]=1; assert(!valid(bad)); // Short block.
            bad=data; bad[13]=0xff; bad[14]=0xff; bad[15]=0xff; assert(!valid(bad)); // Invalid index.
            assert(!pyroWaveValidateFrame(data.data(),data.size(),extent.first,extent.second,!c444,hdr,seen));
            assert(!pyroWaveValidateFrame(data.data(),data.size(),extent.first+32,extent.second,c444,hdr,seen));
        }
    auto odd=frame(131,133,true,true);
    assert(pyroWaveValidateFrame(odd.data(),odd.size(),131,133,true,true,seen));
    assert(!pyroWaveValidateFrame(odd.data(),odd.size(),131,133,false,true,seen));
    puts("Wire-v1 complete-frame/profile/dimension/sequence/duplicate/malformed tests passed");
}
