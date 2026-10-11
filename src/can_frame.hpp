#pragma once

#include <cstdint>

struct CanFrame {
    uint32_t id = 0;
    uint8_t length = 0;
    uint8_t data[8] = {};
    bool remote = false;
    bool extended = false;
};

constexpr bool isValidStandardCanFrame(uint32_t id, const uint8_t* data, uint8_t length)
{
    return id <= 0x7FFu && data != nullptr && length <= 8u;
}