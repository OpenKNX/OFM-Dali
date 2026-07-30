#pragma once

#include <Arduino.h>

// Single source of truth for DALI protocol/config constants shared across
// DaliModule, DaliChannel and DaliAddressing.
namespace DaliConstants
{
    static constexpr uint8_t ChannelCount = 64;
    static constexpr uint8_t GroupCount = 16;
    static constexpr uint8_t HclCurveCount = 3;
    static constexpr uint8_t BroadcastAddress = 0xFF;
    static constexpr uint16_t AllGroups = 0xFFFF;
}
