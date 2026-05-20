#pragma once

#include "DaliEVGBase.h"

class DaliDT6 : public DaliEVGBase
{
public:
        DaliDT6(Dali::Master &master, uint8_t address, bool isGroup = false,
            uint8_t minLevel = 0, uint8_t maxLevel = 254, uint8_t onLevel = 254,
            uint8_t fadeTime = 0, uint8_t fadeRate = 0, bool errorState = false, uint8_t fastFadeTime = 0);

    uint8_t getFastFadeTime() const;

protected:
    bool handleDeviceExtendedCommand(uint8_t command, uint8_t value) override;
    bool handleDeviceQuery(uint8_t query, const ParsedFrame &parsed) override;

private:
    uint8_t fastFadeTime;
};