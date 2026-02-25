#include "HclCurve.h"
#include "OpenKNX/DateTime.h"

const std::string HclCurve::logPrefix()
{
    std::string name = "HCL<";
    name += std::to_string(_channelIndex + 1);
    name += ">";
    return name;
}

const uint8_t HclCurve::channelIndex()
{
    return _channelIndex;
}

void HclCurve::setup(uint8_t channelIndex)
{
    _channelIndex = channelIndex;
    _type = ParamDGWH_type;
    _isConfigured = _type != PT_hclType_none;
    if(_isConfigured)
    {
        if(_type == PT_hclType_sun)
            logDebugP("Konfiguriert: Sonnenstand %i/%i K - %i/%i %%", ParamDGWH_min, ParamDGWH_max, ParamDGWH_briMin, ParamDGWH_briMax);
        else if(_type == PT_hclType_time)
            logDebugP("Konfiguriert: Zeittabelle");
    } else 
        logDebugP("Nicht Konfiguriert");

    _lastCheck = _channelIndex*3000;
}

void HclCurve::loop()
{
    if(!_isConfigured) return;

    OpenKNX::TimeOnly sunRise = openknx.sun.sunRiseLocalTime();
    OpenKNX::TimeOnly sunSet = openknx.sun.sunSetLocalTime();

    if((sunRise.hour == 0 && sunRise.minute == 0) || (sunSet.hour == 0 && sunSet.minute == 0))
    {
        logDebugP("Ungueltige Sonnenstandsdaten");
        return;
    }

    OpenKNX::DateTime currentTime = openknx.time.getLocalTime();
    logDebugP("Aktuelle Zeit: %i:%i:%i", currentTime.hour, currentTime.minute, currentTime.second);

    uint16_t minT = ParamDGWH_min;
    uint8_t minB = ParamDGWH_briMin;

    if(currentTime.hour < sunRise.hour || (currentTime.hour == sunRise.hour && currentTime.minute < sunRise.minute))
    {
        logDebugP("Vor Sonnenaufgang %i K (%i:%i)", minT, sunRise.hour, sunRise.minute);
        if(ParamDGWH_checkTemperature)
            KoDGWH_hcl_state.value(minT, Dpt(7, 600));
        if(ParamDGWH_checkBrightness)
            KoDGWH_bri_state.value(minB, Dpt(5, 1));
    } else if(currentTime.hour > sunSet.hour || (currentTime.hour == sunSet.hour && currentTime.minute > sunSet.minute)) {
        logDebugP("Nach Sonnenuntergang %i K (%i:%i)", minT, sunSet.hour, sunSet.minute);
        if(ParamDGWH_checkTemperature)
            KoDGWH_hcl_state.value(minT, Dpt(7, 600));
        if(ParamDGWH_checkBrightness)
            KoDGWH_bri_state.value(minB, Dpt(5, 1));
    } else {
        logDebugP("Dazwischen %i:%i - jetzt - %i:%i", sunRise.hour, sunRise.minute, sunSet.hour, sunSet.minute);
        uint16_t startMin = sunRise.hour*60 + sunRise.minute;
        uint16_t stopMin = sunSet.hour*60 + sunSet.minute;

        if(ParamDGWH_offsetRiseType == PT_offset_plus)
            startMin += ParamDGWH_offsetRiseMin;
        else if(ParamDGWH_offsetRiseType == PT_offset_minus)
            startMin -= ParamDGWH_offsetRiseMin;

        if(ParamDGWH_offsetSetType == PT_offset_plus)
            stopMin += ParamDGWH_offsetSetMin;
        else if(ParamDGWH_offsetSetType == PT_offset_minus)
            stopMin -= ParamDGWH_offsetSetMin;

        uint16_t currentMin = currentTime.hour*60 + currentTime.minute;
        //logDebugP("start %i | stop %i | curr %i", startMin, stopMin, currentMin);
        uint16_t response = 0;
        uint16_t maxT = ParamDGWH_max;
        uint8_t maxB = ParamDGWH_briMax;
        
        if(ParamDGWH_checkTemperature)
        {
            response = ColorHelper::getValueFromSun(currentMin - startMin, stopMin - startMin, minT, maxT);
            logDebugP("response: %i K", response);
            KoDGWH_hcl_state.value(response, Dpt(7, 600));
        }
        if(ParamDGWH_checkBrightness)
        {
            response = ColorHelper::getValueFromSun(currentMin - startMin, stopMin - startMin, minB, maxB);
            logDebugP("response: %i %", response);
            KoDGWH_bri_state.value(response, Dpt(5, 1));
        }
    }
}