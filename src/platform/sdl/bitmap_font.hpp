#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace echo::platform {

[[nodiscard]] inline bool nextUtf8CodePoint(std::string_view text, std::size_t& offset,
                                            std::uint32_t& codePoint) noexcept {
    if (offset >= text.size()) {
        return false;
    }
    const auto first = static_cast<std::uint8_t>(text[offset]);
    if (first < 0x80U) {
        codePoint = first;
        ++offset;
        return true;
    }

    std::size_t length{};
    std::uint32_t value{};
    if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        value = first & 0x1FU;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        value = first & 0x0FU;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        value = first & 0x07U;
    } else {
        return false;
    }
    if (length > text.size() - offset) {
        return false;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto continuation = static_cast<std::uint8_t>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            return false;
        }
        value = (value << 6U) | (continuation & 0x3FU);
    }
    const std::uint32_t minimum = length == 2 ? 0x80U : length == 3 ? 0x800U : 0x10000U;
    if (value < minimum || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
        return false;
    }
    offset += length;
    codePoint = value;
    return true;
}

[[nodiscard]] inline bool isBitmapDialogueCodePoint(std::uint32_t value) noexcept {
    if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') || value == ' ' || value == '+' ||
        value == '%' || value == ':' || value == '-' || value == '.' || value == '/') {
        return true;
    }
    switch (value) {
    case 0x00C1: case 0x00E1: case 0x010C: case 0x010D: case 0x010E: case 0x010F:
    case 0x00C9: case 0x00E9: case 0x011A: case 0x011B: case 0x00CD: case 0x00ED:
    case 0x0147: case 0x0148: case 0x00D3: case 0x00F3: case 0x0158: case 0x0159:
    case 0x0160: case 0x0161: case 0x0164: case 0x0165: case 0x00DA: case 0x00FA:
    case 0x016E: case 0x016F: case 0x00DD: case 0x00FD: case 0x017D: case 0x017E:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool supportsBitmapText(std::string_view text,
                                             std::size_t maximumCharacters) noexcept {
    std::size_t offset{};
    std::size_t characters{};
    while (offset < text.size()) {
        std::uint32_t codePoint{};
        if (!nextUtf8CodePoint(text, offset, codePoint) ||
            !isBitmapDialogueCodePoint(codePoint) || ++characters > maximumCharacters) {
            return false;
        }
    }
    return true;
}

} // namespace echo::platform
