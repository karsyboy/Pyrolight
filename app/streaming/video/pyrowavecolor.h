#pragma once
#include <libplacebo/colorspace.h>

// Wire-v1 carries normalized YCbCr, not low-bit integer codes in R16.
// Equal depths preserve the sampled magnitude while selecting the correct
// code-space neutral point and limited-range endpoints in libplacebo.
inline pl_color_repr pyroWaveColorRepresentation(bool hdr, bool limited, bool bt2020)
{
    pl_color_repr repr {};
    repr.bits.sample_depth = repr.bits.color_depth = hdr ? 10 : 8;
    repr.sys = bt2020 ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
    repr.levels = limited ? PL_COLOR_LEVELS_LIMITED : PL_COLOR_LEVELS_FULL;
    return repr;
}

// Wire-v1 SDR is the Granite scaler's sRGB output. The bitstream has only
// an SDR/PQ transfer bit; do not reinterpret its SDR pixels as gamma 2.4.
inline pl_color_transfer pyroWaveTransferFunction(bool hdr)
{
    return hdr ? PL_COLOR_TRC_PQ : PL_COLOR_TRC_SRGB;
}
