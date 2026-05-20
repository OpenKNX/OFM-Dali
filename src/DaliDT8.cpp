#include "DaliDT8.h"

DaliDT8::DaliDT8(Dali::Master &master, uint8_t address, bool isGroup,
                 uint8_t minLevel, uint8_t maxLevel, uint8_t onLevel,
                 uint8_t fadeTime, bool errorState)
    : DaliEVGBase(master, address, 8, isGroup, minLevel, maxLevel, onLevel, fadeTime, errorState, false),
      coordinateX(0),
      coordinateY(0),
      colourTemperature(0),
      rgbLevel{0, 0, 0},
      colourActive(false),
      lastExtendedCommand(0)
{
}

uint16_t DaliDT8::getCoordinateX() const
{
    return coordinateX;
}

uint16_t DaliDT8::getCoordinateY() const
{
    return coordinateY;
}

uint16_t DaliDT8::getColourTemperature() const
{
    return colourTemperature;
}

const std::array<uint8_t, 3> &DaliDT8::getRgbLevel() const
{
    return rgbLevel;
}

bool DaliDT8::isColourActive() const
{
    return colourActive;
}

bool DaliDT8::handleDeviceExtendedCommand(uint8_t command, uint8_t value)
{
    if (pendingDeviceType != getDeviceType()) {
        return false;
    }

    lastExtendedCommand = command;

    switch (command) {
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::SET_COORDINATE_X):
            coordinateX = (uint16_t)dtr[0] | ((uint16_t)dtr[1] << 8);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::SET_COORDINATE_Y):
            coordinateY = (uint16_t)dtr[0] | ((uint16_t)dtr[1] << 8);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::SET_TEMP_COLOUR_TEMPERATURE):
            colourTemperature = (uint16_t)dtr[0] | ((uint16_t)dtr[1] << 8);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::SET_TEMP_RGB_LEVEL):
            rgbLevel[0] = dtr[0];
            rgbLevel[1] = dtr[1];
            rgbLevel[2] = dtr[2];
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::ACTIVATE):
            colourActive = true;
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::STEP_UP_COORDINATE_X):
            coordinateX = coordinateX + 1;
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::STEP_DOWN_COORDINATE_X):
            coordinateX = coordinateX > 0 ? coordinateX - 1 : 0;
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::STEP_UP_COORDINATE_Y):
            coordinateY = coordinateY + 1;
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::STEP_DOWN_COORDINATE_Y):
            coordinateY = coordinateY > 0 ? coordinateY - 1 : 0;
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::SET_TEMP_PRIMARY_LEVEL):
            currentLevel = dtr[0];
            onState = currentLevel > 0;
            if (onState) {
                lastNonZeroLevel = currentLevel;
            }
            return true;
        default:
            break;
    }

    return false;
}

bool DaliDT8::handleDeviceQuery(uint8_t query, const ParsedFrame &parsed)
{
    if (pendingDeviceType != getDeviceType()) {
        return false;
    }

    switch (query) {
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::QUERY_GEAR_FEATURES):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::QUERY_COLOUR_STATUS):
            respond(colourActive ? 1 : 0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::QUERY_COLOUR_TYPE_FEATURES):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::QUERY_COLOUR_VALUE):
            respond((uint8_t)(colourTemperature & 0xFF));
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::QUERY_COLOUR_RGBWAF_CONTROL):
            respond(0);
            return true;
        case static_cast<uint8_t>(Dali::ExtendedCommandDT8::QUERY_COLOUR_ASSIGNED_COLOUR):
            respond(0);
            return true;
        default:
            break;
    }

    return false;
}
