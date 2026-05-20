#pragma once

#include "DaliEVGBase.h"
#include <array>

class DaliDT8 : public DaliEVGBase
{
public:
    DaliDT8(Dali::Master &master, uint8_t address, bool isGroup = false,
            uint8_t minLevel = 0, uint8_t maxLevel = 254, uint8_t onLevel = 254,
            uint8_t fadeTime = 0, bool errorState = false);

    uint16_t getCoordinateX() const;
    uint16_t getCoordinateY() const;
    uint16_t getColourTemperature() const;
    const std::array<uint8_t, 3> &getRgbLevel() const;
    bool isColourActive() const;

protected:
    bool handleDeviceExtendedCommand(uint8_t command, uint8_t value) override;
    bool handleDeviceQuery(uint8_t query, const ParsedFrame &parsed) override;

private:
    uint16_t coordinateX;
    uint16_t coordinateY;
    uint16_t colourTemperature;
    std::array<uint8_t, 3> rgbLevel;
    bool colourActive;
    uint8_t lastExtendedCommand;
};