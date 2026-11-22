#pragma once

#include "echo/framework/canvas2d.hpp"
#include "platform/presentation/bitmap_font.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace echo::platform {
namespace bitmap_text_detail {

struct Glyph {
    char character;
    std::array<std::uint8_t, 7> rows;
};

inline constexpr Glyph kGlyphs[] = {
    // Internal bitmap symbols: cross, square, triangle, circle, D-pad, Options, touchpad.
    {'\x01', {17, 17, 10, 4, 10, 17, 17}}, {'\x02', {31, 17, 17, 17, 17, 17, 31}},
    {'\x03', {4, 4, 10, 10, 17, 17, 31}}, {'\x04', {14, 17, 17, 17, 17, 17, 14}},
    {'\x05', {4, 4, 31, 31, 31, 4, 4}}, {'\x06', {31, 0, 31, 0, 31, 0, 0}},
    {'\x07', {31, 17, 21, 21, 17, 31, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}}, {'%', {17, 2, 4, 8, 17, 0, 0}},
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}}, {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}}, {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {':', {0, 4, 4, 0, 4, 4, 0}}, {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'.', {0, 0, 0, 0, 0, 12, 12}}, {'/', {1, 2, 2, 4, 8, 8, 16}},
};

} // namespace bitmap_text_detail

template <typename Fill>
void drawBitmapText(int x, int y, std::string_view value, int scale, Fill&& fill) noexcept {
    using namespace bitmap_text_detail;
    std::size_t offset{};
    while (offset < value.size()) {
        std::uint32_t codePoint{};
        if (!nextUtf8CodePoint(value, offset, codePoint)) {
            ++offset;
            continue;
        }
        if (codePoint == ' ') {
            x += 6 * scale;
            continue;
        }

        char character{};
        enum class Accent { None, Acute, Caron, Ring } accent{Accent::None};
        if (codePoint < 0x80U) {
            character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(codePoint)));
        } else {
            switch (codePoint) {
            case 0x00C1: case 0x00E1: character = 'A'; accent = Accent::Acute; break;
            case 0x010C: case 0x010D: character = 'C'; accent = Accent::Caron; break;
            case 0x010E: case 0x010F: character = 'D'; accent = Accent::Caron; break;
            case 0x00C9: case 0x00E9: character = 'E'; accent = Accent::Acute; break;
            case 0x011A: case 0x011B: character = 'E'; accent = Accent::Caron; break;
            case 0x00CD: case 0x00ED: character = 'I'; accent = Accent::Acute; break;
            case 0x0147: case 0x0148: character = 'N'; accent = Accent::Caron; break;
            case 0x00D3: case 0x00F3: character = 'O'; accent = Accent::Acute; break;
            case 0x0158: case 0x0159: character = 'R'; accent = Accent::Caron; break;
            case 0x0160: case 0x0161: character = 'S'; accent = Accent::Caron; break;
            case 0x0164: case 0x0165: character = 'T'; accent = Accent::Caron; break;
            case 0x00DA: case 0x00FA: character = 'U'; accent = Accent::Acute; break;
            case 0x016E: case 0x016F: character = 'U'; accent = Accent::Ring; break;
            case 0x00DD: case 0x00FD: character = 'Y'; accent = Accent::Acute; break;
            case 0x017D: case 0x017E: character = 'Z'; accent = Accent::Caron; break;
            default: break;
            }
        }
        const auto glyph = std::find_if(std::begin(kGlyphs), std::end(kGlyphs),
                                        [character](const Glyph& item) {
                                            return item.character == character;
                                        });
        if (glyph != std::end(kGlyphs)) {
            if (accent == Accent::Acute) {
                rendering::Rect2D first{x + 3 * scale, y, scale, scale};
                rendering::Rect2D second{x + 2 * scale, y + scale, scale, scale};
                fill(first);
                fill(second);
            } else if (accent == Accent::Caron) {
                rendering::Rect2D left{x + scale, y, scale, scale};
                rendering::Rect2D right{x + 3 * scale, y, scale, scale};
                rendering::Rect2D middle{x + 2 * scale, y + scale, scale, scale};
                fill(left);
                fill(right);
                fill(middle);
            } else if (accent == Accent::Ring) {
                rendering::Rect2D top{x + 2 * scale, y, scale, scale};
                rendering::Rect2D left{x + scale, y + scale, scale, scale};
                rendering::Rect2D right{x + 3 * scale, y + scale, scale, scale};
                fill(top);
                fill(left);
                fill(right);
            }
            const int glyphOffset = accent == Accent::None ? 0 : 2;
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((glyph->rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0) {
                        continue;
                    }
                    rendering::Rect2D pixel{x + column * scale, y + (row + glyphOffset) * scale,
                                            scale, scale};
                    fill(pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

} // namespace echo::platform
