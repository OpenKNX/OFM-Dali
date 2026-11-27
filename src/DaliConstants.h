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

    // Function Property command codes dispatched in DaliModule::processFunctionProperty (must match FUNC_* in DaliModule.script.js).
    enum class FunctionId : uint8_t
    {
        ReadDeviceType = 2,
        Scan = 3,
        AssignAddress = 4,
        EvgWrite = 10,
        EvgRead = 11,
        SetScene = 12,
        GetScene = 13,
        Identify = 14,
        ReadInfo = 15,
    };

    // Bit flags for the errorByte shared by funcHandleEvgRead's helpers and readDimmingCurve (must match ERR_* in DaliModule.script.js).
    struct EvgReadErrorBit
    {
        static constexpr uint8_t MIN = 0b1;
        static constexpr uint8_t MAX = 0b10;
        static constexpr uint8_t POWER = 0b100;
        static constexpr uint8_t FAILURE = 0b1000;
        static constexpr uint8_t FADE = 0b10000;
        static constexpr uint8_t DIMM_CURVE = 0b100000;
        static constexpr uint8_t GROUPS_LOW = 0b1000000;
        static constexpr uint8_t GROUPS_HIGH = 0b10000000;
    };

    // Bit flags for funcHandleReadInfo's errorByte (must match ERR_INFO_* in DaliModule.script.js).
    struct InfoErrorBit
    {
        static constexpr uint8_t MEMORY_READ_FAILED = 0b1;
        static constexpr uint8_t LEGACY_BANK0 = 0b10;
        static constexpr uint8_t PHYS_MIN_FAILED = 0b100;
    };

    // Memory Bank 0 (IEC 62386-102 section 9.10.6) layout used by DaliModule::funcHandleReadInfo.
    // GTIN, firmware version, serial number and hardware version are contiguous.
    struct MemoryBank0Address
    {
        static constexpr uint8_t GTIN_START = 0x03;
        static constexpr uint8_t HW_VERSION_END = 0x14;
    };
    
    struct MemoryBank0ResultOffset
    {
        // GTIN(6) + Firmware version(2) + Serial number(8) + Hardware version(2) = 18 bytes.
        static constexpr uint8_t BLOCK_LENGTH = MemoryBank0Address::HW_VERSION_END - MemoryBank0Address::GTIN_START + 1;
        static constexpr uint8_t PART102_VERSION = 1 + BLOCK_LENGTH;
        static constexpr uint8_t PHYS_MIN_LEVEL = PART102_VERSION + 1;
        static constexpr uint8_t ERROR = PHYS_MIN_LEVEL + 1;
    };
}
