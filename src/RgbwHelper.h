#pragma once
#include <stdint.h>

/*
 * Helper functions for KNX DPT 251.600 (DPT_Colour_RGBW) and the translation to DALI DT8.
 *
 * Telegram (6 bytes). The field order follows the definition in knx_master.xml (MSB first):
 *   Byte 0: red    (0..255, the ETS shows 0..100 %)
 *   Byte 1: green
 *   Byte 2: blue
 *   Byte 3: white
 *   Byte 4: reserved (0x00)
 *   Byte 5: bits 7..4 reserved, bit 3 = mR, bit 2 = mG, bit 1 = mB, bit 0 = mW  (1 = value valid)
 *
 * Deliberately without dependencies on Arduino or the KNX stack, so that the logic can be tested on a PC
 * (test/test_rgbw_helper.cpp).
 */
namespace RgbwHelper
{
    constexpr uint8_t DataLength = 6;

    constexpr uint8_t ValidRed = 0x08;
    constexpr uint8_t ValidGreen = 0x04;
    constexpr uint8_t ValidBlue = 0x02;
    constexpr uint8_t ValidWhite = 0x01;
    constexpr uint8_t ValidAll = 0x0F;

    /*
     * Takes over only the channels marked as valid in the telegram into color[0..3] (R, G, B, W).
     * Channels that are not marked keep their previous value.
     * Returns the validity mask (lower 4 bits). 0 = the telegram changes nothing.
     */
    inline uint8_t decode(const uint8_t *data, uint8_t *color)
    {
        const uint8_t valid = data[5] & ValidAll;
        if (valid & ValidRed)
            color[0] = data[0];
        if (valid & ValidGreen)
            color[1] = data[1];
        if (valid & ValidBlue)
            color[2] = data[2];
        if (valid & ValidWhite)
            color[3] = data[3];
        return valid;
    }

    /*
     * true if all four channels (R, G, B, W) are 0. This counts as "off".
     */
    inline bool isOff(const uint8_t *color)
    {
        return color[0] == 0 && color[1] == 0 && color[2] == 0 && color[3] == 0;
    }

    /*
     * Writes color[0..3] as a 6-byte telegram (e.g. for the status object). All channels are marked as valid.
     */
    inline void encode(const uint8_t *color, uint8_t *data)
    {
        data[0] = color[0];
        data[1] = color[1];
        data[2] = color[2];
        data[3] = color[3];
        data[4] = 0x00;
        data[5] = ValidAll;
    }

    /*
     * DALI level: for DT8, 255 means "MASK" (= do not change this channel), 254 is the maximum.
     */
    inline uint8_t daliLevel(uint8_t value)
    {
        return value == 255 ? 254 : value;
    }

    /*
     * Reverse of daliLevel(): DALI level (254 = maximum) -> KNX value (255 = 100 %).
     */
    inline uint8_t knxFromDaliLevel(uint8_t level)
    {
        return level == 254 ? 255 : level;
    }

    /*
     * Colour from the four report values (R, G, B, W) of the driver (QUERY_COLOUR_VALUE, selectors 233-236).
     * Usable only if no value is 255 (MASK = unknown) and at least one channel is lit: a driver that reports
     * only zeros has no colour (yet), and a colour of all zeros would be "off".
     * Returns true and writes color[0..3] if the colour can be taken over. Currently used only by the host test,
     * kept for a later colour query of the driver.
     */
    inline bool colourFromDriver(const uint8_t *report, uint8_t *color)
    {
        for (uint8_t i = 0; i < 4; i++)
            if (report[i] == 255)
                return false;
        if (report[0] == 0 && report[1] == 0 && report[2] == 0 && report[3] == 0)
            return false;
        for (uint8_t i = 0; i < 4; i++)
            color[i] = knxFromDaliLevel(report[i]);
        return true;
    }

    /*
     * Brightness from the RGBW value ("slider" mode): the largest channel is the brightness (255 = 100 %), the ratio
     * of the channels to each other is the colour mix. The DA4-M ignores the amplitude of colour values (it
     * normalises), so the brightness has to be sent as an own level.
     */
    inline uint8_t maxChannel(const uint8_t *color)
    {
        uint8_t m = color[0];
        for (uint8_t i = 1; i < 4; i++)
            if (color[i] > m)
                m = color[i];
        return m;
    }

    /*
     * Colour mix: color scaled so that the largest channel is 255 (all 0 stays 0).
     */
    inline void normalise(const uint8_t *color, uint8_t *mix)
    {
        const uint8_t m = maxChannel(color);
        for (uint8_t i = 0; i < 4; i++)
            mix[i] = m == 0 ? 0 : (uint8_t)(((uint16_t)color[i] * 255 + m / 2) / m);
    }

    /*
     * Reverse: scale the colour mix (largest channel 255) to the amplitude amp (= largest channel).
     */
    inline void scale(const uint8_t *mix, uint8_t amp, uint8_t *color)
    {
        for (uint8_t i = 0; i < 4; i++)
            color[i] = (uint8_t)(((uint16_t)mix[i] * amp + 127) / 255);
    }

    // Amplitude (0..255) <-> brightness in percent (0..100, linear)
    inline float percentFromAmplitude(uint8_t amp)
    {
        return amp * 100.0f / 255.0f;
    }

    inline uint8_t amplitudeFromPercent(float percent)
    {
        if (percent <= 0.0f)
            return 0;
        if (percent >= 100.0f)
            return 255;
        return (uint8_t)(percent * 2.55f + 0.5f);
    }
}
