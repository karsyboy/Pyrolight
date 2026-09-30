#include <initializer_list>
#include "streaming/video/pyrowavecolor.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    assert(pyroWaveTransferFunction(false) == PL_COLOR_TRC_SRGB);
    assert(pyroWaveTransferFunction(true) == PL_COLOR_TRC_PQ);
    for (bool hdr : {false, true}) for (bool limited : {false, true}) {
        const int depth = hdr ? 10 : 8;
        const float max = (1 << depth) - 1;
        const float scale = 1 << (depth - 8);
        auto repr = pyroWaveColorRepresentation(hdr, limited, hdr);
        assert(pl_color_repr_normalize(&repr) == 1.0f);
        auto transform = pl_color_repr_decode(&repr, nullptr);
        for (float gray : {0.0f, 0.005f, 0.18f, 0.5f, 1.0f}) {
            const float y = limited ? (16 * scale + gray * 219 * scale) / max : gray;
            float rgb[] = {y, (1 << (depth - 1)) / max, (1 << (depth - 1)) / max};
            pl_transform3x3_apply(&transform, rgb);
            for (float c : rgb) assert(std::abs(c - gray) < 1e-6f);
        }
        // Primaries and secondaries through the actual libplacebo inverse matrix.
        const float kr = hdr ? 0.2627f : 0.2126f, kb = hdr ? 0.0593f : 0.0722f;
        for (int color = 0; color < 8; color++) {
            float r = !!(color & 1), g = !!(color & 2), b = !!(color & 4);
            float y = kr * r + (1 - kr - kb) * g + kb * b;
            float cb = (b - y) / (2 * (1 - kb)), cr = (r - y) / (2 * (1 - kr));
            float rgb[] = {limited ? (16 * scale + y * 219 * scale) / max : y,
                (1 << (depth - 1)) / max + cb * (limited ? 224 * scale / max : 1),
                (1 << (depth - 1)) / max + cr * (limited ? 224 * scale / max : 1)};
            pl_transform3x3_apply(&transform, rgb);
            // libplacebo uses the full-range chroma excursion 2^N/(2^N-1).
            // Its inverse differs from the scaler by at most one source code.
            const float tolerance = limited ? 2e-6f : 1.0f / max;
            assert(std::abs(rgb[0] - r) < tolerance);
            assert(std::abs(rgb[1] - g) < tolerance);
            assert(std::abs(rgb[2] - b) < tolerance);
        }
        // R16 storage rounds normalized floats; it does not store unshifted code/65535.
        float neutral = (1 << (depth - 1)) / max;
        float rgb[] = {0.5f, std::round(neutral * 65535) / 65535, std::round(neutral * 65535) / 65535};
        pl_transform3x3_apply(&transform, rgb);
        if (!limited) for (float c : rgb) assert(std::abs(c - 0.5f) < 2e-5f);
    }
    puts("Normalized R16: 8/10-bit, 709/2020, full/limited neutral/ramp/primary values passed");
}
