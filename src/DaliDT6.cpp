#include "DaliDT6.h"

DaliDT6::DaliDT6(Dali::Master &master, uint8_t address, bool isGroup,
                                 uint8_t minLevel, uint8_t maxLevel, uint8_t onLevel,
                                 uint8_t fadeTime, uint8_t fadeRate, bool errorState, uint8_t fastFadeTime)
        : DaliEVGBase(master, address, 6, isGroup, minLevel, maxLevel, onLevel, fadeTime, fadeRate, errorState, false),
            fastFadeTime(fastFadeTime)
{
}

uint8_t DaliDT6::getFastFadeTime() const
{
    return fastFadeTime;
}

void DaliDT6::debugOutputParams() const
{
    DaliEVGBase::debugOutputParams();
    printf("DaliDT6 fastFadeTime=%u\n", fastFadeTime);
}

bool DaliDT6::handleDeviceExtendedCommand(uint8_t command, uint8_t value)
{
    if (pendingDeviceType != getDeviceType()) {
        return false;
    }

    switch (command) {
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::SET_FAST_FADE_TIME):
            fastFadeTime = value;
            return true;
        default:
            break;
    }

    return false;
}

bool DaliDT6::handleDeviceQuery(uint8_t query, const ParsedFrame &parsed)
{
    if (pendingDeviceType != getDeviceType()) {
        return false;
    }

    switch (query) {
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_DIMMING_CURVE):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_FEATURES):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_FAILURE_STATUS):
            respond(hasError() ? 1 : 0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_LOAD_DECREASE):
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_LOAD_INCREASE):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_THERMAL_SHUTDOWN):
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_THERMAL_OVERLOAD):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_REFERENCE_RUNNING):
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_REFERENCE_MEASUREMENT_FAILED):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_FAST_FADE_TIME):
            respond(fastFadeTime);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_MIN_FAST_FADE_TIME):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT6::QUERY_CONTROL_GEAR_TYPE):
            respond(getDeviceType());
            return true;
        default:
            break;
    }

    return false;
}
