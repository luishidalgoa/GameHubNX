#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gamehubnx {

// Reduce un bitmap RGBA por media de bloques hasta que su lado mayor quepa en
// maxSide. El factor es entero, asi que el otro lado queda algo menor que el
// tope. Si ya cabe, o no puede reducirse sin quedar vacio, no se toca.
inline void shrinkRgbaToFit(std::vector<uint8_t>& rgba, int& width, int& height,
                            int maxSide) {
    const int longEdge = std::max(width, height);
    if (maxSide <= 0 || width <= 0 || height <= 0 || longEdge <= maxSide)
        return;
    const int factor = (longEdge + maxSide - 1) / maxSide;
    const int dw = width / factor;
    const int dh = height / factor;
    if (dw <= 0 || dh <= 0)
        return;

    std::vector<uint8_t> out(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4);
    const uint32_t area = static_cast<uint32_t>(factor) * static_cast<uint32_t>(factor);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int fy = 0; fy < factor; ++fy) {
                const size_t row =
                    (static_cast<size_t>(y * factor + fy) * static_cast<size_t>(width) +
                     static_cast<size_t>(x * factor)) * 4;
                for (int fx = 0; fx < factor; ++fx)
                    for (size_t c = 0; c < 4; ++c)
                        sum[c] += rgba[row + static_cast<size_t>(fx) * 4 + c];
            }
            uint8_t* dst = out.data() + (static_cast<size_t>(y) * dw + x) * 4;
            for (size_t c = 0; c < 4; ++c)
                dst[c] = static_cast<uint8_t>(sum[c] / area);
        }
    }
    rgba.swap(out);
    width = dw;
    height = dh;
}

} // namespace gamehubnx
