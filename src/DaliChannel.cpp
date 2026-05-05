#include "DaliChannel.h"
#include "OpenKNX.h"

DaliChannel::DaliChannel(Dali::Master &_daliMaster) : daliMaster(_daliMaster) {}

DaliChannel::~DaliChannel() {}

const std::string DaliChannel::name()
{
    if (_isStaircase)
        if (_isGroup)
            return "StaircaseChannel_G";
        else
            return "StaircaseChannel_A";

    if (_isGroup)
        return "StandardChannel_G";
    return "StandardChannel_A";
}

const bool DaliChannel::isConfigured()
{
    return _isConfigured;
}

const bool DaliChannel::isGroup()
{
    return _isGroup;
}

bool DaliChannel::hasError()
{
    return _errorState;
}

void DaliChannel::init(uint8_t channelIndex, bool ig)
{
    _channelIndex = channelIndex;
    _isGroup = ig;
}

// will be called once
// only if knx.configured == true
void DaliChannel::setup()
{
    if (_isGroup)
    {
        _isConfigured = ParamDGWG_deviceType != PT_groupType_none;
        if (!_isConfigured)
            return;

        _isStaircase = ParamDGWG_type;
        _min = 0;
        _max = 100;
        if (_isStaircase)
            interval = ParamDGWG_stairtime;
        _onDay = DaliHelper::percentToArc((float)ParamDGWG_onDay);
        _onNight = DaliHelper::percentToArc((float)ParamDGWG_onNight);
        _queryInterval = ParamDGWG_queryTime;
        _dimmStatusInterval = ParamDGWG_dimmStateInterval;
        if(ParamDGWG_hcl)
        {
            _hclCurve = ParamDGWG_hclCurve;
            _hclIsAlsoOn = ParamDGWG_hclStart;
        }
    }
    else
    {
        _isConfigured = ParamDGW_deviceType != PT_deviceType_none;
        if (!_isConfigured)
            return;

        _isStaircase = ParamDGW_type;
        _min = ParamDGW_min;
        _max = ParamDGW_max;
        if (_isStaircase)
            interval = ParamDGW_stairtime;
        _onDay = DaliHelper::percentToArc((float)ParamDGW_onDay);
        _onNight = DaliHelper::percentToArc((float)ParamDGW_onNight);
        _getError = ParamDGW_error;
        _queryInterval = ParamDGW_queryTime;
        _dimmStatusInterval = ParamDGW_dimmStateInterval;
        if(ParamDGW_hcl)
        {
            _hclCurve = ParamDGW_hclCurve;
            _hclIsAlsoOn = ParamDGW_hclStart;
        }
    }
    _min = DaliHelper::percentToArc(_min);
    _max = DaliHelper::percentToArc(_max);
    logDebugP("Min/Max %i/%i | D/N %i/%i (%.2f/%.2f) | TRH %is | Err %i | Q %is | Dimm %i", _min, _max, _onDay, _onNight, DaliHelper::arcToPercentFloat(_onDay), DaliHelper::arcToPercentFloat(_onNight), interval, _getError, _queryInterval, _dimmStatusInterval);
}

void DaliChannel::loop()
{
    if(_hclIsAutoMode != _hclLastState)
    {
        _hclLastState = _hclIsAutoMode;
        logDebugP("HCL Mode: %s", _hclIsAutoMode ? "Auto" : "Manu");
    }
}

void DaliChannel::loop1()
{
    if (!_isConfigured)
        return;

    loopStaircase();
    loopDimming();
    loopError();
    loopQueryLevel();
}

void DaliChannel::loopStaircase()
{
    if (_isStaircase && currentState)
    {
        if (millis() - startTime > interval * 1000)
        {
            logDebugP("Zeit abgelaufen");
            currentState = false;
            daliMaster.sendArc(_channelIndex, 0x00, _isGroup);
            setSwitchState(false);
        }
    }
}

void DaliChannel::loopDimming()
{
    if (_dimmDirection != DimmDirection::None)
    {
        if(millis() - _dimmLast > DimmInterval)
        {
            if (_dimmDirection == DimmDirection::Up)
            {
                if (currentDimmType == DimmType::Brigthness)
                {
                    this->queryActualLevel();
                    daliMaster.sendCommand(_channelIndex, Dali::Command::UP, _isGroup);
                }

                *currentDimmValue = *currentDimmValue + 1;
                if (*currentDimmValue == 254)
                {
                    logDebugP("Dimm Stop at 254");
                    _dimmDirection = DimmDirection::None;
                    updateCurrentDimmValue();
                }else if (*currentDimmValue >= _max)
                {
                    logDebugP("Dimm Stop at max %i (%i)", _max, *currentDimmValue);
                    _dimmDirection = DimmDirection::None;
                    updateCurrentDimmValue();
                }
            }
            else if (_dimmDirection == DimmDirection::Down)
            {
                if (currentDimmType == DimmType::Brigthness)
                {
                    this->queryActualLevel();
                    daliMaster.sendCommand(_channelIndex, Dali::Command::DOWN, _isGroup);
                }

                *currentDimmValue = *currentDimmValue - 1;
                if (*currentDimmValue <= _min || *currentDimmValue == 0) {
                    logDebugP("Dimm Stop at: %i", *currentDimmValue);
                    updateCurrentDimmValue();
                    if (this->isDimmOffLocked()) {
                        logDebugP("Stop here because Dimm off is locked!");
                        _dimmDirection = DimmDirection::None;
                    } else {
                        logDebugP("Turn off device");
                        daliMaster.sendCommand(_channelIndex, Dali::Command::OFF, _isGroup);
                    }
                }
            }
            
            _dimmLast = millis();
        }

        if(_dimmStatusInterval != 0 && millis() - _dimmLastStatus > (_dimmStatusInterval*100))
        {
            _dimmLastStatus = millis();
            updateCurrentDimmValue();
        }
    }
}

void DaliChannel::loopError()
{
    if (!_isGroup && _getError)
    {
        if (millis() - _lastError > 60000)
        {
            _errorResp = daliMaster.sendCommand(_channelIndex, Dali::Command::QUERY_STATUS, false, true);
            _lastError = millis();
            logDebugP("EVG abfragen %i", _errorResp);
            return;
        }
        if(_errorResp == 0)
            return; // we did not send a query

        Dali::Response response = daliMaster.getResponse(_errorResp);
        if(response.state == Dali::ResponseState::NOT_REGISTERED)
        {
            // There is no response we can wait for
            _errorResp = 0;
            return;
        }
        if(response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
            return; // no answer yet
        
        if(response.state == Dali::ResponseState::NO_ANSWER)
        {
            logErrorP("EVG hat nicht geantwortet");
            _errorState = true;
        }
        else
        {
            logDebugP("EVG hat geantwortet");
            _errorState = false;
        }

        bool val = knx.getGroupObject(calcKoNumber(DGW_Koerror)).value(Dpt(1, 1));
        _errorState = val != 0;
        if (val != _errorState)
            knx.getGroupObject(calcKoNumber(DGW_Koerror)).value((val != 0), DPT_Switch);

        if(_errorState)
        {
            logErrorP("EVG hat ein Fehler");
        }
        _errorResp = 0;
    }
}

void DaliChannel::loopQueryLevel()
{
    if(_queryInterval > 0 && _queryId == 0 && (millis() - _lastValueQuery) > (_queryInterval*1000))
    {
        logDebugP("Query actual level");
        _lastValueQuery = millis();
        if(_lastValueQuery == 0) _lastValueQuery++;

        this->queryActualLevel();
        logDebugP("id: %i", _queryId);
        return;
    }
    if(_queryId != 0)
    {
        Dali::Response response = daliMaster.getResponse(_queryId);
        if(response.state == Dali::ResponseState::NOT_REGISTERED)
        {
            // There is no response we can wait for
            _queryId = 0;
            return;
        }
        if(response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
            return; // no answer yet
        if(response.state == Dali::ResponseState::NO_ANSWER)
        {
            logErrorP("EVG hat nicht geantwortet");
            _queryId = 0;
            return;
        }
        else
        {
            _queryId = 0;
            if(response.frame.flags & DALI_FRAME_ERROR)
            {
                logErrorP("EVG hat ein Fehler");
                return;
            } else {
                uint8_t data = response.frame.data & 0xFF;
                logDebugP("Got new actual level %i%%-%i", DaliHelper::arcToPercent(data), data);
                setDimmState(data, false, true);

                if(currentDimmType == DimmType::Brigthness)
                    *currentDimmValue = data;
            }
        }
    }
}

uint16_t DaliChannel::calcKoNumber(int asap)
{
    if (_isGroup)
        return asap + (DGWG_KoBlockSize * _channelIndex) + DGWG_KoOffset;

    return asap + (DGW_KoBlockSize * _channelIndex) + DGW_KoOffset;
}

void DaliChannel::processInputKo(GroupObject &ko)
{
    int chanIndex = 0;
    if (_isGroup)
    {
        chanIndex = (ko.asap() - DGWG_KoOffset) % DGWG_KoBlockSize;
        //logDebugP("Got GROUP KO %i", chanIndex);
    }
    else
    {
        chanIndex = (ko.asap() - DGW_KoOffset) % DGW_KoBlockSize;
        //logDebugP("Got SHORT KO %i", chanIndex);
    }

    switch (chanIndex)
    {
    // Schalten
    case DGW_Koswitch:
        koHandleSwitch(ko);
        break;

    // Schalten Status
    // case 1

    // Dimmen relativ
    case DGW_Kodimm_relative:
        koHandleDimmRel(ko);
        break;

    // Dimmen Absolut
    case DGW_Kodimm_absolute:
        koHandleDimmAbs(ko);
        break;

    // Dimmen Status
    // case 4

    // Sperren
    case DGW_Kolock:
        koHandleLock(ko);
        break;

    case DGW_Kocolor:
        koHandleColor(ko);
        break;

    // Farbe Status
    // case 7

    // Farbe Rot Dimmen relativ
    case DGW_Kocolor_red_relative:
        koHandleColorRel(ko, 0);
        break;

    // Farbe Rot Dimmen absolut
    case DGW_Kocolor_red_absolute:
        koHandleColorAbs(ko, 0);
        break;

    // Farbe Rot Status
    // case 10

    // Farbe Grün Dimmen relativ
    case DGW_Kocolor_green_relative:
        koHandleColorRel(ko, 1);
        break;

    // Farbe Grün Dimmen absolut
    case DGW_Kocolor_green_absolute:
        koHandleColorAbs(ko, 1);
        break;

    // Farbe Grün Status
    // case 13

    // Farbe Blau Dimmen relativ
    case DGW_Kocolor_blue_relative:
        koHandleColorRel(ko, 2);
        break;

    // Farbe Blau Dimmen absolut
    case DGW_Kocolor_blue_absolute:
        koHandleColorAbs(ko, 2);
        break;

    // Farbe Blau Status
    // case 16

    case DGW_Kohcl_curve:
        koHandleHclCurve(ko);
        break;

    case DGW_Koscene:
        koHandleScene(ko);
        break;
        
    // Error
    // case 19
    }
}

void DaliChannel::koHandleHclCurve(GroupObject &ko)
{
    uint8_t curve = ko.value(Dpt(5,1));
    if(curve > 2)
    {
        logErrorP("Setzen der HCL Kurve ignoriert, da zu hoch: %i, max %i", curve, 2);
        return;
    }
    _hclCurve = curve;
}

void DaliChannel::koHandleScene(GroupObject &ko)
{
    uint8_t number = ko.value(Dpt(17,1));
    logDebugP("Szene KNX %i to DALI %i", number + 1, number);
    if(number > 15)
    {
        logErrorP("Szene ignoriert, da zu hoch: %i, max 15", number);
        return;
    }

    daliMaster.sendCommand(_channelIndex, Dali::Command::GO_TO_SCENE | number, _isGroup);
}

void DaliChannel::koHandleColorRel(GroupObject &ko, uint8_t index)
{
    logDebugP("Farbe relativ %i", index);
    if (currentIsLocked)
    {
        logErrorP("is locked");
        return;
    }

    uint8_t dimmLock = _isGroup ? ParamDGWG_dimmLock : ParamDGW_dimmLock;
    if(dimmLock == PT_dimmLock_noBoth || dimmLock == PT_dimmLock_noOn)
    {
        logDebugP("ignored due settings");
        return;
    }

    if(_isGroup ? ParamDGWG_hcl_manu_col : ParamDGW_hcl_manu_col)
        _hclIsAutoMode = false;

    _dimmStep = ko.value(Dpt(3, 7, 1));

    if (_dimmStep == 0)
    {
        logDebugP("Dimm Stop");
        _dimmDirection = DimmDirection::None;
        _dimmLast = 0;
        updateCurrentDimmValue();
        return;
    }

    currentDimmValue = &currentStep;
    currentDimmType = DimmType::Color;
    _dimmLastStatus = millis();
    _dimmDirection = ko.value(Dpt(3, 7, 0)) ? DimmDirection::Up : DimmDirection::Down;
    if (_dimmDirection == DimmDirection::Up)
    {
        logDebugP("Dimm Up Start %i/%i", currentStep, *currentDimmValue);
    }
    else if (_dimmDirection == DimmDirection::Down)
    {
        logDebugP("Dimm Down Start %i/%i", currentStep, *currentDimmValue);
    }
}

void DaliChannel::koHandleColorAbs(GroupObject &ko, uint8_t index)
{
    logDebugP("Farbe absolut %i", index);
    if (currentIsLocked)
    {
        logErrorP("is locked");
        return;
    }

    if(_isGroup ? ParamDGW_hcl_manu_col : ParamDGW_hcl_manu_col)
        _hclIsAutoMode = false;
        
    logDebugP("AutoSwitchConfig %i %i", ParamDGW_hcl_manu_col, _hclIsAutoMode);

    currentColor[index] = ko.value(Dpt(5, 4));
    updateCurrentDimmValue();
    sendColor();
    logDebugP("AutoSwitchConfig2 %i %i", ParamDGW_hcl_manu_col, _hclIsAutoMode);
}

void DaliChannel::koHandleSwitch(GroupObject &ko)
{
    logDebugP("Schalten");
    if (currentIsLocked)
    {
        logErrorP("is locked");
        return;
    }

    if (_isStaircase)
        handleSwitchStaircase(ko);
    else
        handleSwitchNormal(ko);
}

void DaliChannel::handleSwitchNormal(GroupObject &ko)
{
    bool value = ko.value(DPT_Switch);
    if (value)
    {
        uint8_t onValue = isNight ? _onNight : _onDay;
        if (onValue == 0)
            onValue = isNight ? _lastNightValue : _lastDayValue;
    
        bool hclBriActivated = false;
        if(_hclCurve != 255 && _hclIsAutoMode)
        {
            uint8_t _channelIndex = _hclCurve;
            hclBriActivated = ParamDGWH_checkBrightness;
            if(hclBriActivated)
            {
                onValue = DaliHelper::percentToArc(_hclCurrentBri);
                logDebugP("Einschalten HCL");
            }
        }
        if(!hclBriActivated) {
            logDebugP(isNight ? "Einschalten Nacht" : "Einschalten Tag");
        }
        daliMaster.sendArc(_channelIndex, onValue, _isGroup);

        bool hclTempActivated = false;
        if(_hclCurve != 255 && _hclIsAutoMode)
        {
            uint8_t _channelIndex = _hclCurve;
            hclTempActivated = ParamDGWH_checkTemperature;
            if(hclTempActivated)
            {
                logDebugP("Setze Temperatur auf %iK", _hclCurrentTemp);
                setTemperature(_hclCurrentTemp);
            }
        }
        setDimmState(onValue, true);
    }
    else
    {
        logDebugP("Ausschalten");
        daliMaster.sendArc(_channelIndex, 0x00, _isGroup);
        setSwitchState(false);
    }
    currentState = value;
}

void DaliChannel::handleSwitchStaircase(GroupObject &ko)
{
    if (ko.value(DPT_Switch))
    {
        if (currentState)
        {
            logDebugP("ist bereits an");

            bool retrigger = _isGroup ? ParamDGWG_manuoff : ParamDGW_manuoff;
            if (retrigger)
            {
                logDebugP("wurde nachgetriggert");
                startTime = millis();
                return;
            }
            return;
        }
        currentState = true;
        startTime = millis();
        logDebugP("interval %i", interval);

        uint8_t onValue = isNight ? _onNight : _onDay;
        if (onValue == 0)
            onValue = isNight ? _lastNightValue : _lastDayValue;
        if(_hclCurve != 255 && _hclIsAutoMode)
        {
            onValue = DaliHelper::percentToArc(_hclCurrentBri);
            logDebugP("Einschalten HCL");
        } else {
            logDebugP(isNight ? "Einschalten Nacht" : "Einschalten Tag");
        }
        daliMaster.sendArc(_channelIndex, onValue, _isGroup);
        if(_hclCurve != 255 && _hclIsAutoMode)
            setTemperature(_hclCurrentTemp);
        setDimmState(DaliHelper::percentToArc(onValue));
    }
    else
    {
        bool manuOff = _isGroup ? ParamDGWG_manuoff : ParamDGW_manuoff;
        if (!manuOff)
        {
            logErrorP("no manuel off");
            return;
        }

        logDebugP("Ausschalten");
        daliMaster.sendArc(_channelIndex, 0x00, _isGroup);
        setSwitchState(false);
        currentState = false;
    }
}

void DaliChannel::koHandleDimmRel(GroupObject &ko)
{
    logDebugP("Dimmen relativ");
    if (currentIsLocked)
    {
        logErrorP("is locked");
        return;
    }    

    if(_isGroup ? ParamDGWG_hcl_manu_bri : ParamDGW_hcl_manu_bri)
        _hclIsAutoMode = false;

    _dimmStep = ko.value(Dpt(3, 7, 1));

    if (_dimmStep == 0)
    {
        logDebugP("Dimm Stop");
        _dimmDirection = DimmDirection::None;
        _dimmLast = 0;
        updateCurrentDimmValue();
        return;
    }

    currentDimmValue = &currentStep;
    currentDimmType = DimmType::Brigthness;
    _dimmLastStatus = millis();
    _dimmDirection = ko.value(Dpt(3, 7, 0)) ? DimmDirection::Up : DimmDirection::Down;
    if (_dimmDirection == DimmDirection::Up)
    {
        if(!currentState )
        {
            if (this->isDimmOnLocked())
            {
                logDebugP("ignored because Dimm On is locked!");
                _dimmDirection = DimmDirection::None;
                _dimmLast = 0;
            }
            else 
            {
                daliMaster.sendCommand(_channelIndex, Dali::Command::RECALL_MIN, _isGroup, true);
                *currentDimmValue = _min;
                currentState = true;
                this->queryActualLevel();
                logDebugP("starting with min %i", _min);
                _dimmLast = millis();
            }
        }
        logDebugP("Dimm Up Start %i/%i", currentStep, *currentDimmValue);
    }
    else if (_dimmDirection == DimmDirection::Down)
    {
        logDebugP("Dimm Down Start %i/%i", currentStep, *currentDimmValue);
    }
}

void DaliChannel::koHandleDimmAbs(GroupObject &ko)
{
    logDebugP("Dimmen absolut");
    if (currentIsLocked)
    {
        logErrorP("is locked");
        return;
    }

    if(_isGroup ? ParamDGWG_hcl_manu_bri : ParamDGW_hcl_manu_bri)
        _hclIsAutoMode = false;

    uint8_t value = ko.value(Dpt(5, 1));
    logDebugP("Dimmen Absolut auf %i%%", value);
    uint8_t arc = DaliHelper::percentToArc(value);
    daliMaster.sendArc(_channelIndex, arc, _isGroup);
    setDimmState(arc, true, true);
}

void DaliChannel::koHandleLock(GroupObject &ko)
{
    bool value = ko.value(Dpt(1, 1));

    if (_isGroup)
    {
        if (ParamDGWG_locknegate)
            value = !value;
    }
    else
    {
        if (ParamDGW_locknegate)
            value = !value;
    }

    if (currentIsLocked == value)
        return;
    currentIsLocked = value;
    uint8_t behave;
    uint8_t behavevalue;
    if (currentIsLocked)
    {
        logDebugP("Sperren");
        behave = ParamDGW_lockbehave;
        behavevalue = ParamDGW_lockvalue;
    }
    else
    {
        logDebugP("Entsperren");
        behave = ParamDGW_unlockbehave;
        behavevalue = ParamDGW_unlockvalue;
    }

    switch (behave)
    {
    // nothing
    case PT_lock_no:
        logDebugP("Nichts");
        return;

    // Ausschalten
    case PT_lock_off:
    {
        logDebugP("Ein");
        behavevalue = isNight ? _onNight : _onDay;
        break;
    }

    // Einschalten
    case PT_lock_on:
    {
        logDebugP("Aus");
        behavevalue = 0;
        break;
    }

    // Fester Wert
    case PT_lock_value:
    {
        logDebugP("Wert");
        break;
    }
    }
    logDebugP("%i - %i", behavevalue, DaliHelper::percentToArc(behavevalue));
    daliMaster.sendArc(_channelIndex, behavevalue, _isGroup);
    setDimmState(DaliHelper::percentToArc(behavevalue));
}

void DaliChannel::koHandleColor(GroupObject &ko)
{
    logDebugP("Farbe absolut");
    if (currentIsLocked)
    {
        logErrorP("is locked");
        return;
    }

    if(_isGroup ? ParamDGWG_hcl_manu_col : ParamDGW_hcl_manu_col)
        _hclIsAutoMode = false;

    logDebugP("AutoConf %i %i %i", _isGroup, _isGroup ? ParamDGWG_hcl_manu_col : ParamDGW_hcl_manu_col, _hclIsAutoMode);

    uint8_t colorType = _isGroup ? ParamDGWG_colorType : ParamDGW_colorType;

    switch(colorType)
    {
        //HSV
        case PT_colorType_HSV:
        {
            uint32_t value = ko.value(Dpt(232, 600));
            logDebugP("Got Color: %X", value);

            currentColor[0] = (value >> 16) & 0xFF;
            currentColor[1] = (value >> 8) & 0xFF;
            currentColor[2] = value & 0xFF;

            sendKoStateOnChange(DGW_Kocolor_rgb_state, value, Dpt(232, 600));
            sendKoStateOnChange(DGW_Kocolor_red_state, currentColor[0], Dpt(5, 4));
            sendKoStateOnChange(DGW_Kocolor_green_state, currentColor[1], Dpt(5, 4));
            sendKoStateOnChange(DGW_Kodimm_state, currentColor[2], Dpt(5, 4));

            sendColor();
            break;
        }

        //RGB
        case PT_colorType_RGB:
        {
            uint32_t value = ko.value(Dpt(232, 600));
            logDebugP("Got Color: %X", value);

            currentColor[0] = (value >> 16) & 0xFF;
            currentColor[1] = (value >> 8) & 0xFF;
            currentColor[2] = value & 0xFF;

            sendKoStateOnChange(DGW_Kocolor_rgb_state, value, Dpt(232, 600));
            sendKoStateOnChange(DGW_Kocolor_red_state, currentColor[0], Dpt(5, 4));
            sendKoStateOnChange(DGW_Kocolor_green_state, currentColor[1], Dpt(5, 4));
            sendKoStateOnChange(DGW_Kocolor_blue_state, currentColor[2], Dpt(5, 4));

            sendColor();
            break;
        }

        //TW
        case PT_colorType_TW:
        {
            uint16_t kelvin = ko.value(Dpt(7, 600));
            setTemperature(kelvin);
            break;
        }

        //xyY
        case PT_colorType_XYY:
        {
            uint8_t* data = ko.valueRef();
            /*
                <UnsignedInteger Id="DPST-242-600_F-1" Width="16" Name="x-axis" Unit="None" />
                <UnsignedInteger Id="DPST-242-600_F-2" Width="16" Name="y-axis" Unit="None" />
                <UnsignedInteger Id="DPST-242-600_F-3" Width="8" Name="brightness" Unit="%" />
                <Reserved Width="6" />
                <Bit Id="DPST-242-600_F-10" Cleared="invalid" Set="valid" Name="Validity xy" />
                <Bit Id="DPST-242-600_F-11" Cleared="invalid" Set="valid" Name="Validity brightness" />
            */
            uint16_t x = data[0] << 8 | data[1];
            uint16_t y = data[2] << 8 | data[3];
            uint8_t b = data[4];
            bool xyIgnore = _isGroup ? ParamDGWG_xyIgnore : ParamDGW_xyIgnore;
            if(xyIgnore)
            {
                pushWord(x, currentColor);
                pushWord(y, currentColor + 2);
                
                logDebugP("Got Color: %.4f %.4f", x / 65535.0, y / 65535.0);
            } else {
                ColorHelper::xyyToRGB(x, y, b, currentColor[0], currentColor[1], currentColor[2]);

                uint32_t value = currentColor[0] << 16 | currentColor[1] << 8 | currentColor[2];
                logDebugP("Got Color: %X", value);
            }


            sendColor();
            
            // use 7.600 to send 2 byte unsigned int
            sendKoStateOnChange(DGW_Kocolor_rgb_state, data, Dpt(7, 600));
            break;
        }
    }

    if(!currentState)
        setDimmState(_onDay, true, true); // TODO get real
    
    logDebugP("AutoConf %i %i %i", _isGroup, _isGroup ? ParamDGW_hcl_manu_col : ParamDGWG_hcl_manu_col, _hclIsAutoMode);
}

void DaliChannel::sendKoStateOnChange(uint16_t koNr, const KNXValue &value, const Dpt &type)
{
    GroupObject &ko = knx.getGroupObject(calcKoNumber(koNr));
    if(ko.valueNoSendCompare(value, type))
        ko.objectWritten();
}

void DaliChannel::setTemperature(uint16_t value)
{
    logDebugP("Set Kelvin: %i K", value);
    uint16_t mirek = 1000000.0 / value;
    //TODO check the colorType and then set RGB or TW or do nothing if it is no color Device
    daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, mirek & 0xFF);
    daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR1, (mirek >> 8) & 0xFF);
    daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::SET_TEMP_COLOUR_TEMPERATURE, _isGroup);
    daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::ACTIVATE, _isGroup);
    sendKoStateOnChange(DGW_Kocolor_rgb_state, value, Dpt(7, 600));
}

void DaliChannel::setBrightness(uint8_t value)
{
    logDebugP("Set Brightness: %i %%", value);
    daliMaster.sendArc(_channelIndex, DaliHelper::percentToArc(value), _isGroup);
    sendKoStateOnChange(DGW_Kodimm_state, value, Dpt(5, 1));
}

bool DaliChannel::isDimmOnLocked() {
    uint8_t dimmLock = _isGroup ? ParamDGWG_dimmLock : ParamDGW_dimmLock;
    return dimmLock == PT_dimmLock_noBoth || dimmLock == PT_dimmLock_noOn;
}

bool DaliChannel::isDimmOffLocked() {
    uint8_t dimmLock = _isGroup ? ParamDGWG_dimmLock : ParamDGW_dimmLock;
    return dimmLock == PT_dimmLock_noBoth || dimmLock == PT_dimmLock_noOff;
}

void DaliChannel::queryActualLevel()
{
    _queryId = daliMaster.sendCommand(_isGroup ? _dimmReferenceAddress : _channelIndex, Dali::Command::QUERY_ACTUAL_LEVEL, false, true);
}

void DaliChannel::sendColor()
{
    // TODO senden nur alle 100? ms
    uint8_t r, g, b;
    uint8_t colorType = _isGroup ? ParamDGWG_colorType : ParamDGW_colorType;
    if (colorType == PT_colorType_HSV)
    {
        ColorHelper::hsvToRGB(currentColor[0], currentColor[1], currentColor[2], r, g, b);
    }
    else
    {
        r = currentColor[0];
        g = currentColor[1];
        b = currentColor[2];
    }

    logDebugP("RGB: %i %i %i", r, g, b);
    if (r == 255) r--;
    if (g == 255) g--;
    if (b == 255) b--;
    logDebugP("Send: %i %i %i", r, g, b);

    uint8_t sendType = _isGroup ? ParamDGWG_colorSpace : ParamDGW_colorSpace;
    switch (sendType)
    {
    // send as rgb
    case PT_colorSpace_rgb:
    {
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, r);
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR1, g);
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR2, b);
        daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::SET_TEMP_RGB_LEVEL, _isGroup);
        daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::ACTIVATE, _isGroup);
        break;
    }

    // send as xy
    case PT_colorSpace_xy:
    {
        uint16_t x;
        uint16_t y;
        
        bool xyIgnore = _isGroup ? ParamDGWG_xyIgnore : ParamDGW_xyIgnore;
        if(xyIgnore)
        {
            popWord(x, currentColor);
            popWord(y, currentColor + 2);
        } else {
            ColorHelper::rgbToXY(r, g, b, x, y);
        }

        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, x & 0xFF);
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR1, (x >> 8) & 0xFF);
        daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::SET_COORDINATE_X, _isGroup);

        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, y & 0xFF);
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR1, (y >> 8) & 0xFF);
        daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::SET_COORDINATE_Y, _isGroup);

        daliMaster.sendExtendedCommand(_channelIndex, 0x08, Dali::ExtendedCommandDT8::ACTIVATE, _isGroup);
        break;
    }
    }
}

void DaliChannel::setSwitchState(bool value, bool isSwitchCommand)
{
    if (isSwitchCommand)
    {
        uint8_t toSet = 0;
        if (value)
        {
            toSet = DaliHelper::percentToArc(isNight ? _onNight : _onDay);
        }
        else
        {
            toSet = 0;
        }

        setDimmState(toSet);
        currentStep = toSet;
    }

    //logDebugP("AutoConfSwitch %i %i %i", value, ParamDGW_hcl_auto_off, _hclIsAutoMode);
    if(!value && (_isGroup ? ParamDGWG_hcl_auto_off : ParamDGW_hcl_auto_off))
        _hclIsAutoMode = true;

    // logDebugP("AutoConfSwitch %i %i %i", value, ParamDGW_hcl_auto_off, _hclIsAutoMode);

    // bool currentState = knx.getGroupObject(calcKoNumber(_isGroup ? DGWG_Koswitch_state : DGW_Koswitch_state)).value(DPT_Switch);
    // if (value == currentState)
    //     return;
    knx.getGroupObject(calcKoNumber(_isGroup ? DGWG_Koswitch_state : DGW_Koswitch_state)).value(value, DPT_Switch);
}

void DaliChannel::setDimmState(uint8_t value, bool isDimmCommand, bool isLastCommand)
{
    if (_dimmDirection == DimmDirection::None && value > 0)
    {
        if (isNight)
            _lastNightValue = value;
        else
            _lastDayValue = value;
    }

    if (isDimmCommand)
    {
        setSwitchState(value > 0, false);
        currentStep = value;
    }

    float perc = DaliHelper::arcToPercentFloat(value);

    GroupObject& ko = knx.getGroupObject(calcKoNumber(DGW_Kodimm_state));
    if(ko.valueNoSendCompare(perc, Dpt(5, 1)))
    {
        logDebugP("SetDimmState %.1f/%i", perc, value);
        ko.objectWritten();
    }
}

void DaliChannel::updateCurrentDimmValue()
{
    if (currentDimmType == DimmType::Color)
        sendColor();

    switch (currentDimmType)
    {
        case DimmType::Brigthness:
        {
            logDebugP("current dimm val %i", *currentDimmValue);
            setDimmState(*currentDimmValue, true, false);
            break;
        }

        case DimmType::Color:
        {
            uint32_t value = currentColor[2];
            value |= currentColor[1] << 8;
            value |= currentColor[0] << 16;
            GroupObject& ko = knx.getGroupObject(calcKoNumber(DGW_Kocolor_rgb_state));
            if(ko.valueNoSendCompare(value, Dpt(232, 600)))
                ko.objectWritten();
            
            ko = knx.getGroupObject(calcKoNumber(DGW_Kocolor_red_state));
            if(ko.valueNoSendCompare(currentColor[0], Dpt(5, 1)))
                ko.objectWritten();
                
            ko = knx.getGroupObject(calcKoNumber(DGW_Kocolor_green_state));
            if(ko.valueNoSendCompare(currentColor[1], Dpt(5, 1)))
                ko.objectWritten();
                
            ko = knx.getGroupObject(calcKoNumber(DGW_Kocolor_blue_state));
            if(ko.valueNoSendCompare(currentColor[2], Dpt(5, 1)))
                ko.objectWritten();
            break;
        }
    }
}

void DaliChannel::setOnValue(uint8_t value)
{
    _onDay = value;
}

void DaliChannel::setGroups(uint16_t groupBits, DaliChannel* groups)
{
    _groups = groupBits;

    // Set Reference Address to get current value from evg
    // when relative dimming a group
    if(!_isGroup)
    {
        for(int i = 0; i < 16; i++)
        {
            if((groupBits >> i) & 1)
            {
                groups[i].setDeimmRef(_channelIndex);
            }
        }
    }
}

void DaliChannel::setDeimmRef(uint8_t ref)
{
    if(_isGroup && _dimmReferenceAddress == 255)
    {
        logDebugP("Set Dimmref for %u to %u", _channelIndex, ref);
        _dimmReferenceAddress = ref;
    }
}

void DaliChannel::setGroupState(uint16_t group, bool state)
{
    if (group == 0xFFFF || _groups & 1 << group)
        setSwitchState(state, false);
}

void DaliChannel::setGroupState(uint16_t group, uint8_t value)
{
    if (group == 0xFFFF || _groups & (1 << group))
        setDimmState(value, true, true);
}

void DaliChannel::setMinArc(uint8_t min)
{
    _min = min;
}

void DaliChannel::setMaxArc(uint8_t max)
{
    _max = max;
}

void DaliChannel::setHcl(uint8_t curve, uint16_t value)
{
    //TODO add ComObject for selecting curve
    if(_hclCurve == 255) return;
    logDebugP("Temp curve %i | state %i | isAlsoOn %i", _hclCurve, currentState, _hclIsAlsoOn);
    if(!_hclIsAutoMode)
    {
        logDebugP("Ignored because mode is manu");
        return;
    }

    if(curve == _hclCurve)
        _hclCurrentTemp = value;

    if(curve == _hclCurve && currentState)
    {
        if(_hclIsAlsoOn)
        {
            logDebugP("Setting HCL");
            setTemperature(value);
        } else {
            logDebugP("Ignored. Only apply on turning on");
        }
    }
}

void DaliChannel::setHcl(uint8_t curve, uint8_t value)
{
    if(_hclCurve == 255) return;
    logDebugP("Bri curve %i | state %i | isAlsoOn %i", _hclCurve, currentState, _hclIsAlsoOn);
    if(!_hclIsAutoMode)
    {
        logDebugP("Ignored because mode is manu");
        return;
    }

    if(curve == _hclCurve)
        _hclCurrentBri = value;

    if(curve == _hclCurve && currentState)
    {
        _hclCurrentBri = value;
        if(_hclIsAlsoOn)
        {
            logDebugP("Setting HCL");
            setBrightness(value);
        } else {
            logDebugP("Ignored. Only apply on turning on");
        }
    }
}

uint8_t DaliChannel::getMin()
{
    return DaliHelper::arcToPercent(_min);
}

uint8_t DaliChannel::getMax()
{
    return DaliHelper::arcToPercent(_max);
}

uint16_t DaliChannel::getGroups()
{
    return _groups;
}