#include "DaliModule.h"
#include "OpenKNX/DateTime.h"

#ifdef ARDUINO_ARCH_ESP32
#include "NetworkModule.h"
#endif

uint32_t daliActivity = 0;

const std::string DaliModule::name()
{
    return "Dali";
}

// You can also give it a version
// will be displayed in Command Infos
const std::string DaliModule::version()
{
    return openknx.info.humanFirmwareVersion(true);
}

// will be called once
// only if knx.configured == true
void DaliModule::setup(bool conf)
{
    #ifdef ARDUINO_ARCH_ESP32
        openknxNetwork.webserver.addMenuItem("Dali Wiki", "https://github.com/OpenKNX/GW-REG1-Dali/wiki");
    #endif

    if (!conf)
        return;

    for (int i = 0; i < ChannelCount; i++)
    {
        channels[i].init(i, false);
        channels[i].setup();
    }

    for (int i = 0; i < GroupCount; i++)
    {
        groups[i].init(i, true);
        groups[i].setup();
    }

    for (int i = 0; i < HclCurveCount; i++)
    {
        curves[i].setup(i);
    }

#ifdef FUNC1_BUTTON_PIN
    openknx.func1Button.onShortClick([this] { 
        logDebugP("Func Button pressed short");
        uint8_t sett = ParamDGW_funcBtn;
        handleFunc(sett);
    });
    openknx.func1Button.onLongClick([this] { 
        logDebugP("Func Button pressed long");
        uint8_t sett = ParamDGW_funcBtnLong;
        handleFunc(sett);
    });
    openknx.func1Button.onDoubleClick([this] {
        logDebugP("Func Button pressed double");
        uint8_t sett = ParamDGW_funcBtnDbl;
        handleFunc(sett);
    });
#endif
}

#ifdef FUNC1_BUTTON_PIN
void DaliModule::handleFunc(uint8_t setting)
{
    switch (setting)
    {
    case PT_clickAction_on:
        logDebugP("Broadcast on");
        daliMaster.sendCommand(BroadcastAddress, Dali::Command::RECALL_MAX, true);
        _currentIdentifyDevice = 0;
        // openknx.info1Led.errorCode();
        break;
    case PT_clickAction_off:
        logDebugP("Broadcast off");
        daliMaster.sendCommand(BroadcastAddress, Dali::Command::OFF, true);
        _currentIdentifyDevice = 0;
        // openknx.info1Led.errorCode();
        break;
    case PT_clickAction_toggle:
        _currentToggleState = !_currentToggleState;
        logDebugP("Broadcast toggle %i", _currentToggleState);
        daliMaster.sendCommand(BroadcastAddress, _currentToggleState ? Dali::Command::RECALL_MAX : Dali::Command::OFF, true);
        _currentIdentifyDevice = 0;
        // openknx.info1Led.errorCode();
        break;
    case PT_clickAction_lock:
        logDebugP("Locking Device");
        _currentLockState = true;
        _currentIdentifyDevice = 0;
        // openknx.info1Led.errorCode();
        break;
    case PT_clickAction_unlock:
        logDebugP("Unlocking Device");
        _currentLockState = false;
        _currentIdentifyDevice = 0;
        // openknx.info1Led.errorCode();
        break;
    case PT_clickAction_lock_toggle:
        _currentLockState = !_currentLockState;
        logDebugP("Toggle Lock Device %i", _currentLockState);
        _currentIdentifyDevice = 0;
        // openknx.info1Led.errorCode();
        break;
    }
}
#endif

void DaliModule::setup1(bool conf) {
    pinMode(REG1_APP_PIN7, INPUT);
    daliMaster.init(REG1_APP_PIN6, REG1_APP_PIN7);
}

#ifdef DALI_NO_TIMER
bool __isr __time_critical_func(daliTimerInterruptCallback)(repeating_timer *t)
{
    DaliBus.timerISR();
    return true;
}
#endif

void DaliModule::loop(bool configured)
{
    OpenKNX::DateTime currentTime = openknx.time.getLocalTime();
    if (currentTime.minute != _lastTimeMinute)
    {
        _lastTimeMinute = currentTime.minute;
        for (int i = 0; i < HclCurveCount; i++)
            curves[i].loop();
    }

    if (_addressing.loop())
        return;

    loopBusState();

    // TODO remove if scan moved to core1
    if (!configured)
        return;

    if (!_gotInitData)
    {
        if (millis() > 1000)
            loopInitData();
        return;
    }

    for (int i = 0; i < ChannelCount; i++)
    {
        channels[i].loop();
        channels[i].loop1();
    }
    for (int i = 0; i < GroupCount; i++)
    {
        groups[i].loop();
        groups[i].loop1();
    }
}

void DaliModule::loop1(bool configured)
{
    daliMaster.process();
}

void DaliModule::loopInitData()
{
    DaliChannel& channel = channels[_initDataIndex];
    _initDataIndex++;

    if (channel.isConfigured())
    {
        if (_initDataIndex == 0)
            daliMaster.sendArc(BroadcastAddress, DaliHelper::percentToArc((uint8_t)10), true);

        uint16_t groupBits = 0;
        int16_t resp = getInfo(channel.channelIndex(), Dali::Command::QUERY_GROUPS_0_7);
        if (resp < 0)
        {
            logErrorP("Dali Error %i: Code %i", _initDataIndex - 1, resp);
            return;
        }
        groupBits = resp;

        resp = getInfo(channel.channelIndex(), Dali::Command::QUERY_GROUPS_8_15);
        if (resp < 0)
        {
            logErrorP("Dali Error %i: Code %i", _initDataIndex - 1, resp);
            return;
        }
        groupBits |= resp << 8;
        channel.setGroups(groupBits, this->groups);

        resp = getInfo(channel.channelIndex(), Dali::Command::QUERY_MIN_LEVEL);
        if (resp < 0)
        {
            logErrorP("Dali Error %i: Code %i", _initDataIndex - 1, resp);
            return;
        }
        else
        {
            channel.setMinArc(resp);
            logDebugP("CH%i set min to %i", _initDataIndex - 1, resp);
        }

        resp = getInfo(channel.channelIndex(), Dali::Command::QUERY_ACTUAL_LEVEL);
        if (resp < 0)
        {
            logErrorP("Dali Error %i: Code %i", _initDataIndex - 1, resp);
            return;
        }
        else
        {
            channel.setGroupState(DaliChannel::AllGroups, (uint8_t)resp);
        }
    }

    if (_initDataIndex >= ChannelCount)
    {
        _initDataIndex = 0;
        _gotInitData = true;
        _daliStateLast = 1;
        logInfoP("Finished init");
    }
}

void DaliModule::loopGroupState()
{
    if (_lastChangedGroup != 255)
    {
        if (_lastChangedGroup > GroupCount - 1)
        {
            _lastChangedGroup -= GroupCount;
            for (int i = 0; i < ChannelCount; i++)
                channels[i].setGroupState(_lastChangedGroup, _lastChangedValue == 1);
        }
        else
        {
            for (int i = 0; i < ChannelCount; i++)
                channels[i].setGroupState(_lastChangedGroup, _lastChangedValue);
        }

        _lastChangedGroup = 255;
    }
}

#ifdef INFO2_LED_PIN
void DaliModule::loopError()
{
    bool error = false;
    for (int i = 0; i < ChannelCount; i++)
    {
        if (channels[i].hasError())
        {
            error = true;
            break;
        }
    }
    // TODO
    // if (error)
    //     openknx.info2Led.on();
    // else
    //     openknx.info2Led.off();
}
#endif

int16_t DaliModule::getInfo(byte address, uint8_t command, uint8_t additional)
{
    _daliStateLast = millis();
    uint32_t respId = daliMaster.sendCommand(address, command | additional, false, true);
    Dali::Response resp = daliMaster.getResponse(respId);

    while (resp.state == Dali::ResponseState::WAITING || resp.state == Dali::ResponseState::SENT)
    {
        daliMaster.process();
        resp = daliMaster.getResponse(respId);

        if(resp.state == Dali::ResponseState::NO_ANSWER)
        {
            logErrorP("Got no response from channel %i", address);
            return -1;
        }

        if(resp.state == Dali::ResponseState::RECEIVED)
        {
            return (int16_t)(resp.frame.data & 0xFF);
        }

        if(resp.state == Dali::ResponseState::NOT_REGISTERED)
        {
            logErrorP("Response not registered");
            return -1;
        }
    }
    return -1;
}

void DaliModule::loopBusState()
{
    bool state = !digitalRead(REG1_APP_PIN7);
#ifdef INFO3_LED_PIN
    if (_lastBusState != state)
    {
        _lastBusState = state;

        if (state)
            openknx.ledFunctions.get(LED_FUNC_ID_DALI_BUSSTATE)->activity(daliActivity, true);
        else
            openknx.ledFunctions.get(LED_FUNC_ID_DALI_BUSSTATE)->off();
    }
#endif
    if (state != _daliBusStateToSet)
    {
        _daliBusStateToSet = state;
        _daliStateLast = millis();
        if (_daliStateLast == 0)
            _daliStateLast = 1;
    }
    else if (_daliStateLast != 0 && millis() - _daliStateLast > 1000)
    {
        _daliStateLast = 0;
        if (_daliBusState != _daliBusStateToSet)
        {
            _daliBusState = _daliBusStateToSet;
            if (_daliBusState)
                logInfoP("Dali Busspg. vorhanden");
            else
                logInfoP("Dali Busspg. nicht vorhanden");
        }
    }
}

bool DaliModule::getDaliBusState()
{
    return _daliBusState;
}

void DaliModule::showHelp()
{
    openknx.console.printHelpLine("scan", "Dali scan for EVGs");
    openknx.console.printHelpLine("arc", "Dali set Value for EVG, Group or Broadcast");
    openknx.console.printHelpLine("set", "Dali set EVG short address");
    openknx.console.printHelpLine("stepUp", "stepUp xyy => send x times StepUp to evg y");
    openknx.console.printHelpLine("stepDown", "stepDown xyy => send x times StepDown to evg y");
    openknx.console.printHelpLine("getLvl", "getLvl yy => get level of evg y");
}

bool DaliModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    if (diagnoseKo)
        return false;

    std::size_t pos = cmd.find(' ');
    std::string command;
    std::string arg;
    bool hasArg = false;
    if (pos != -1)
    {
        command = cmd.substr(0, pos);
        arg = cmd.substr(pos + 1, cmd.length() - pos - 1);
        hasArg = true;
    }
    else
    {
        command = cmd;
    }

    if (command == "scan")
    {
        cmdHandleScan(hasArg, arg);
        return true;
    }
    if (command == "arc")
    {
        cmdHandleArc(hasArg, arg);
        return true;
    }
    if (command == "set")
    {
        cmdHandleSet(hasArg, arg);
        return true;
    }
    if (command == "stepUp")
    {
        cmdHandleStepUp(hasArg, arg);
        return true;
    }
    if (command == "stepDown")
    {
        cmdHandleStepDown(hasArg, arg);
        return true;
    }

    if (command == "getLvl")
    {
        cmdHandleGetLvl(hasArg, arg);
        return true;
    }

    return false;
}

void DaliModule::cmdHandleStepUp(bool hasArg, std::string arg)
{
    if (!hasArg || arg.length() != 3)
    {
        logErrorP("Argument is invalid!");
        logIndentUp();
        logErrorP("stepUp xyy");
        logErrorP("x =  Count how often to send");
        logErrorP("yy = Address of device (only short address)");
        logIndentDown();
        return;
    }
    uint8_t value = std::stoi(arg.substr(0, 1));
    uint8_t addr = std::stoi(arg.substr(1, 2));
    for (int i = 0; i < value; i++)
        daliMaster.sendCommand(addr, Dali::Command::STEP_UP);
}

void DaliModule::cmdHandleStepDown(bool hasArg, std::string arg)
{
    if (!hasArg || arg.length() != 3)
    {
        logErrorP("Argument is invalid!");
        logIndentUp();
        logErrorP("stepDown xyy");
        logErrorP("x =  Count how often to send");
        logErrorP("yy = Address of device (only short address)");
        logIndentDown();
        return;
    }
    uint8_t value = std::stoi(arg.substr(0, 1));
    uint8_t addr = std::stoi(arg.substr(1, 2));
    for (int i = 0; i < value; i++)
        daliMaster.sendCommand(addr, Dali::Command::STEP_DOWN);
}

void DaliModule::cmdHandleGetLvl(bool hasArg, std::string arg)
{
    if (!hasArg || arg.length() != 2)
    {
        logErrorP("Argument is invalid!");
        logIndentUp();
        logErrorP("getLvl yy");
        logErrorP("yy = Address of device (only short address)");
        logIndentDown();
        return;
    }
    uint8_t addr = std::stoi(arg);
    if (addr > 63)
    {
        logErrorP("Short Address is invalid!");
        return;
    }
    int16_t resp = getInfo(addr, Dali::Command::QUERY_ACTUAL_LEVEL);
    if (resp >= 0)
        logInfoP("EVG %i has level %i = %.2f %%", addr, resp, DaliHelper::arcToPercentFloat((uint8_t)resp));
    else if (resp == -1)
        logErrorP("EVG %i antwortet nicht", addr);
    else
        logErrorP("Fehler beim Auslesen %i", resp);
}

void DaliModule::cmdHandleScan(bool hasArg, std::string arg)
{
    if (!hasArg || arg.length() != 4)
    {
        logErrorP("Argument is invalid!");
        logIndentUp();
        logErrorP("scan wxyz");
        logErrorP("w = 0 => all EVGs");
        logErrorP("    1 => only unaddressed");
        logErrorP("x = 0 => don't randomize");
        logErrorP("    1 => do randomize");
        logErrorP("y = 0 => don't delete shorts");
        logErrorP("    1 => delete all shortaddresses");
        logErrorP("z = 0 => don't assign address to unaddessed");
        logErrorP("    1 => assign address to unaddressed");
        logIndentDown();
        return;
    }
    logInfoP("Starting Scan manually");
    uint8_t resultLength = 254;
    uint8_t data[5] = {};
    uint8_t resultData[4] = {};
    data[1] = arg.at(0) == '1';
    data[2] = arg.at(1) == '1';
    data[3] = arg.at(2) == '1';
    data[4] = arg.at(3) == '1';

    funcHandleScan(data, resultData, resultLength);
}

void DaliModule::cmdHandleArc(bool hasArg, std::string arg)
{
    if (!hasArg || arg.length() != 6)
    {
        logErrorP("Argument is invalid! %i", arg.length());
        logIndentUp();
        logErrorP("arc XYYZZZ");
        logErrorP("X = B => Broadcast");
        logErrorP("    A => Short Address");
        logErrorP("    G => Group");
        logErrorP("Y = Address (00-63)");
        logErrorP("    Group   (00-15)");
        logErrorP("    Broadc. (00)");
        logErrorP("Z = Percent Value (000-100)");
        logIndentDown();
        return;
    }

    uint8_t value = std::stoi(arg.substr(3, 3));
    if (value > 100)
    {
        logErrorP("Value is invalid!");
        return;
    }
    value = DaliHelper::percentToArc(value);
    if (arg.at(0) == 'B')
    {
        logInfoP("Sending Arc %i to Broadcast", value);
        daliMaster.sendArc(BroadcastAddress, value, true);
    }
    else if (arg.at(0) == 'A')
    {
        uint8_t addr = std::stoi(arg.substr(1, 2));
        if (addr > 63)
        {
            logErrorP("Short Address is invalid!");
            return;
        }
        logInfoP("Sending Arc %i to EVG %i", value, addr);
        daliMaster.sendArc(addr, value);
    }
    else if (arg.at(0) == 'G')
    {
        uint8_t addr = std::stoi(arg.substr(1, 2));
        if (addr > 15)
        {
            logErrorP("Group is invalid!");
            return;
        }
        logInfoP("Sending Arc %i to Group %i", value, addr);
        daliMaster.sendArc(addr, value, true);
    }
    else
    {
        logErrorP("Argument X is invalid!");
    }
}

void DaliModule::cmdHandleSet(bool hasArg, std::string arg)
{
    if (!hasArg || arg.length() != 8)
    {
        logErrorP("Argument is invalid! %i", arg.length());
        logIndentUp();
        logErrorP("set XXXXXXYY");
        logErrorP("X = Long Address  (000000-ffffff)");
        logErrorP("Y = Short Address (00-63; 99=unaddressed)");
        logIndentDown();
        return;
    }

    uint8_t resultLength = 254;
    uint8_t data[5] = {};
    uint8_t resultData[4] = {};
    data[1] = std::stoi(arg.substr(6, 2));
    if (data[1] > 63 && data[1] != 99)
    {
        logErrorP("Short Address is invalid!");
        return;
    }
    data[2] = std::stoi(arg.substr(0, 2), nullptr, 16);
    data[3] = std::stoi(arg.substr(2, 2), nullptr, 16);
    data[4] = std::stoi(arg.substr(4, 2), nullptr, 16);

    funcHandleAssign(data, resultData, resultLength);
}

void DaliModule::processInputKo(GroupObject &ko)
{
    // logDebugP("Received Ko %i", ko.asap());
    if (!_addressing.isAddressingIdle() || _currentLockState)
        return;

    int koNum = ko.asap();
    if (processChannelKo(koNum, ko))
        return;
    if (processGroupKo(koNum, ko))
        return;
    if (processHclKo(koNum, ko))
        return;
    processGlobalKo(koNum, ko);
}

bool DaliModule::processChannelKo(int koNum, GroupObject &ko)
{
    if (koNum < DGW_KoOffset || koNum >= DGW_KoOffset + DGW_KoBlockSize * ChannelCount)
        return false;

    int index = floor((koNum - DGW_KoOffset) / DGW_KoBlockSize);
    // logDebugP("For Channel %i", index);
    channels[index].processInputKo(ko);
    return true;
}

bool DaliModule::processGroupKo(int koNum, GroupObject &ko)
{
    if (koNum < DGWG_KoOffset || koNum >= DGWG_KoOffset + DGWG_KoBlockSize * GroupCount)
        return false;

    int index = floor((koNum - DGWG_KoOffset) / DGWG_KoBlockSize);
    int chanIndex = (koNum - DGWG_KoOffset) % DGWG_KoBlockSize;
    // logDebugP("For Group %i", index);
    groups[index].processInputKo(ko);

    if (chanIndex == DGWG_Koswitch_state)
    {
        _lastChangedGroup = index + GroupCount;
        uint8_t value = ko.value(DPT_Switch);
        _lastChangedValue = DaliHelper::percentToArc(value);
    }
    if (chanIndex == DGWG_Kodimm_state)
    {
        _lastChangedGroup = index;
        uint8_t value = ko.value(DPT_Switch);
        _lastChangedValue = DaliHelper::percentToArc(value);
    }

    return true;
}

bool DaliModule::processHclKo(int koNum, GroupObject &ko)
{
    if (koNum < DGWH_KoOffset || koNum >= DGWH_KoOffset + DGWH_KoBlockSize * HclCurveCount)
        return false;

    int index = floor((koNum - DGWH_KoOffset) / DGWH_KoBlockSize);
    int chanIndex = (koNum - DGWH_KoOffset) % DGWH_KoBlockSize;
    // logDebugP("For HCL %i - Ko %i", index, chanIndex);

    switch (chanIndex)
    {
    case DGWH_Kohcl_state:
    {
        uint16_t kelvin = ko.value(Dpt(7, 600));
        for (int i = 0; i < ChannelCount; i++)
            channels[i].setHcl(index, kelvin);
        for (int i = 0; i < GroupCount; i++)
            groups[i].setHcl(index, kelvin);
        break;
    }

    case DGWH_Kobri_state:
    {
        uint8_t brightness = ko.value(Dpt(5, 1));
        for (int i = 0; i < ChannelCount; i++)
            channels[i].setHcl(index, brightness);
        for (int i = 0; i < GroupCount; i++)
            groups[i].setHcl(index, brightness);
        break;
    }

    default:
        logDebugP("unhandled KO: %i", ko.asap());
    }
    return true;
}

void DaliModule::processGlobalKo(int koNum, GroupObject &ko)
{
    switch (koNum)
    {
        // broadcast switch
        case DGW_Kobroadcast_switch:
            koHandleSwitch(ko);
            break;

        // broadcast dimm absolute
        case DGW_Kobroadcast_dimm:
            koHandleDimm(ko);
            break;

        // Tag/Nacht Objekt
        case DGW_Kodaynight:
            koHandleDayNight(ko);
            break;

        // Set OnValue Day
        case DGW_KoonValue:
            koHandleOnValue(ko);
            break;

        case DGW_Koscenes:
            koHandleScene(ko);
            break;

        default:
            logDebugP("unhandled KO: %i", ko.asap());
            break;
    }
}

void DaliModule::koHandleSwitch(GroupObject &ko)
{
    bool value = ko.value(DPT_Switch);
    logDebugP("Broadcast Switch %i", value);
    daliMaster.sendArc(BroadcastAddress, value ? 0xFE : 0x00, true);

    broadcastGroupState(value);
    logDebugP("Broadcast Switch set");
}

void DaliModule::koHandleDimm(GroupObject &ko)
{
    uint8_t value = ko.value(Dpt(5, 1));
    logDebugP("Broadcast Dimm %i", value);
    value = ((253 / 3) * (log10(value) + 1)) + 1;
    value++;
    daliMaster.sendArc(BroadcastAddress, value, true);

    broadcastGroupState(value);
}

void DaliModule::koHandleDayNight(GroupObject &ko)
{
    bool value = ko.value(DPT_Switch);
    if (ParamDGW_daynight)
        value = !value;
    logDebugP("Broadcast Day/Night %i", value);

    setAllNightMode(value);
}

void DaliModule::koHandleOnValue(GroupObject &ko)
{
    uint8_t value = ko.value(Dpt(5, 1));
    logDebugP("KO OnValue: %i", value);
    value = DaliHelper::percentToArc(value);

    setAllOnValue(value);
}

void DaliModule::broadcastGroupState(bool value)
{
    for (int i = 0; i < ChannelCount; i++)
        if (channels[i].isConfigured())
            channels[i].setGroupState(DaliChannel::AllGroups, value);
    for (int i = 0; i < GroupCount; i++)
        if (groups[i].isConfigured())
            groups[i].setGroupState(DaliChannel::AllGroups, value);
}

void DaliModule::broadcastGroupState(uint8_t value)
{
    for (int i = 0; i < ChannelCount; i++)
        if (channels[i].isConfigured())
            channels[i].setGroupState(DaliChannel::AllGroups, value);
    for (int i = 0; i < GroupCount; i++)
        if (groups[i].isConfigured())
            groups[i].setGroupState(DaliChannel::AllGroups, value);
}

void DaliModule::setAllOnValue(uint8_t value)
{
    for (int i = 0; i < ChannelCount; i++)
        channels[i].setOnValue(value);
    for (int i = 0; i < GroupCount; i++)
        groups[i].setOnValue(value);
}

void DaliModule::setAllNightMode(bool value)
{
    for (int i = 0; i < ChannelCount; i++)
        channels[i].isNight = value;
    for (int i = 0; i < GroupCount; i++)
        groups[i].isNight = value;
}

void DaliModule::koHandleScene(GroupObject &ko)
{
    uint8_t knxSceneNumber = ko.value(DPT_SceneNumber);
    bool save = ko.value(DPT_SceneControl);
    logDebugP("KO Scene: %u, save: %u", knxSceneNumber, save);

    for (int i = 0; i < DGWS_CountNumber; i++)
    {
        uint8_t _channelIndex = i;
        uint8_t dest = ParamDGWS_type;
        if (dest == PT_scenetype_none)
            continue;
        if((ParamDGWS_numberKnx - 1) != knxSceneNumber)
            continue;

        logDebugP("Found scene number: %u, save: %u", i, save);
        if (save && !ParamDGWS_save)
        {
            logDebugP("Save not allowed. Skip!");
            continue;
        }
        uint8_t daliScene = ParamDGWS_numberDali;
        uint8_t daliAddr = 0;
        bool isGroup = false;
        switch (dest)
        {
            case PT_scenetype_address:
            {
                daliAddr = ParamDGWS_address;
                logDebugP("send dali scene: %u to Address: %i", i, daliAddr);
                break;
            }
            case PT_scenetype_group:
            {
                daliAddr = ParamDGWS_group;
                logDebugP("send dali scene: %u to Group: %i", i, daliAddr);
                isGroup = true;
                break;
            }
            case PT_scenetype_broadcast:
            {
                daliAddr = 0xFF;
                isGroup = true;
                logDebugP("send dali scene: %u to Broadcast", i);
                break;
            }
            default:
                // this should not happen, because of the check above, but just in case
                logErrorP("Invalid scene type configuration. Skip! scene %u", i);
                continue;
        }
        if (save)
        {
            daliMaster.sendCommand(daliAddr, Dali::Command::ARC_TO_DTR, isGroup);
            daliMaster.sendCommand(daliAddr, Dali::Command::DTR_AS_SCENE | daliScene, isGroup);
        }
        else
        {
            daliMaster.sendCommand(daliAddr, Dali::Command::GO_TO_SCENE | daliScene, isGroup);
        }
    }
}

bool DaliModule::processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    if (objectIndex != 160 || propertyId != 1)
        return false;

    switch (data[0])
    {
    case 2:
        funcHandleType(data, resultData, resultLength);
        return true;

    case 3:
        funcHandleScan(data, resultData, resultLength);
        return true;

    case 4:
        funcHandleAssign(data, resultData, resultLength);
        return true;

        // case 5:
        //     funcHandleAddress(data, resultData, resultLength);
        //     return true;

    case 10:
        funcHandleEvgWrite(data, resultData, resultLength);
        return true;

    case 11:
        funcHandleEvgRead(data, resultData, resultLength);
        return true;

    case 12:
        funcHandleSetScene(data, resultData, resultLength);
        return true;

    case 13:
        funcHandleGetScene(data, resultData, resultLength);
        return true;

    case 14:
        funcHandleIdentify(data, resultData, resultLength);
        return true;
    }

    return false;
}

void DaliModule::funcHandleType(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    int16_t resp = getInfo(data[1], Dali::Command::QUERY_DEVICE_TYPE);
    if (resp < 0)
    {
        logErrorP("Dali Error (DT): Code %i", resp);
        resultData[0] = 0x01;
        resultLength = 1;
        return;
    }
    logDebugP("Resp: %.2X", resp);

    uint8_t deviceType = resp;
    if (resp == 255) // means evg supports several devicetypes
    {
        while (true)
        {
            resp = getInfo(data[1], Dali::Command::QUERY_NEXT_DEVTYPE);
            if (resp < 0)
            {
                logErrorP("Dali Error (NDT): Code %i", resp);
                resultData[0] = 0x01;
                resultLength = 1;
                return;
            }
            logDebugP("Resp: %.2X", resp);
            if (resp == 254)
                break;
            else if (resp < 20)
                deviceType = resp;
        }
    }

    resultData[0] = 0x00;
    resultData[1] = deviceType;

    // The device type numbers defined in PT_deviceType_* and used in knxprod are all one higher than the types specified in Dali (62386-102 Annex B)
    if ((deviceType + 1) == PT_deviceType_DT8)
    {
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::ENABLE_DT, 0x08);
        resp = getInfo(data[1], Dali::ExtendedCommandDT8::QUERY_COLOUR_TYPE_FEATURES);
        if (resp < 0)
        {
            logErrorP("Dali Error (CT): Code %i", resp);
            resultData[0] = 0x02;
            resultLength = 1;
            return;
        }
        resultData[2] = resp;
        resultLength = 3;
    }
    else
    {
        resultLength = 2;
    }
}

void DaliModule::funcHandleScan(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Starting Scan %i %i %i %i", data[1], data[2], data[3], data[4]);

    _addressing.startScan(data[1] == 1, data[2] == 1, data[3] == 1, data[4] == 1);

    resultLength = 0;
}

void DaliModule::funcHandleAssign(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Starting assigning address");

    uint32_t longAddress = (data[2] << 16) | (data[3] << 8) | data[4];
    logInfoP("Long  Addr %X", longAddress);

    uint8_t shortAddress = data[1];
    if (shortAddress == 99)
    {
        shortAddress = 255;
        logInfoP("Removing Short Addr");
    }
    else
    {
        logInfoP("Short Addr %i", shortAddress);
    }

    _addressing.startAssign(longAddress, shortAddress);
    resultLength = 0;
}

void DaliModule::writeEvgDtrByte(uint8_t address, uint8_t value, uint8_t command)
{
    daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, value);
    daliMaster.sendCommand(address, command);
}

void DaliModule::funcHandleEvgWrite(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    // Index == raw 4-bit DALI fade time / fade rate value, text is only used for logging.
    static constexpr const char *const FadeTimeLabels[16] = {
        "inactive", "0.7s", "1.0s", "1.4s", "2.0s", "2.8s", "4.0s", "5.7s",
        "8.0s", "11.3s", "16.0s", "22.6s", "32.0s", "45.3s", "64.0s", "90.5s"};
    static constexpr const char *const FadeRateLabels[16] = {
        "unknown", "358 steps/s", "253 steps/s", "179 steps/s", "127 steps/s",
        "89.4 steps/s", "63.3 steps/s", "44.7 steps/s", "31.6 steps/s",
        "22.4 steps/s", "15.8 steps/s", "11.2 steps/s", "7.9 steps/s",
        "5.6 steps/s", "4.0 steps/s", "2.8 steps/s"};

    uint8_t address = data[1];
    DaliChannel &channel = channels[address];
    logInfoP("Starting setting up EVG %i", address);
    logIndentUp();

    uint16_t tempValue = 0;
    popWord(tempValue, data + 2);
    uint8_t minArc = DaliHelper::percentToArc(ColorHelper::getFloat(tempValue) * 100);
    logDebugP("set min %3.2f%%", ColorHelper::getFloat(tempValue) * 100);
    writeEvgDtrByte(address, minArc, Dali::Command::DTR_AS_MIN);
    channel.setMinArc(minArc);

    popWord(tempValue, data + 4);
    uint8_t maxArc = DaliHelper::percentToArc(ColorHelper::getFloat(tempValue) * 100);
    logDebugP("set max %3.2f%%", ColorHelper::getFloat(tempValue) * 100);
    writeEvgDtrByte(address, maxArc, Dali::Command::DTR_AS_MAX);
    channel.setMaxArc(maxArc);

    popWord(tempValue, data + 6);
    uint8_t powerArc = (tempValue == DisabledDptValue) ? 255 : DaliHelper::percentToArc(ColorHelper::getFloat(tempValue) * 100);
    if (tempValue == DisabledDptValue)
        logDebugP("set power disabled");
    else
        logDebugP("set power %3.2f", ColorHelper::getFloat(tempValue) * 100);
    writeEvgDtrByte(address, powerArc, Dali::Command::DTR_AS_POWER_ON);

    popWord(tempValue, data + 8);
    uint8_t failArc = (tempValue == DisabledDptValue) ? 255 : DaliHelper::percentToArc(ColorHelper::getFloat(tempValue) * 100);
    if (tempValue == DisabledDptValue)
        logDebugP("set fail disabled");
    else
        logDebugP("set fail %3.2f", ColorHelper::getFloat(tempValue) * 100);
    writeEvgDtrByte(address, failArc, Dali::Command::DTR_AS_FAIL);

    // TODO maybe add a function for setting things from dtr
    uint8_t fadeTimeIndex = (data[10] >> 4) & 0xF;
    logDebugP("set fade time %s", FadeTimeLabels[fadeTimeIndex]);
    writeEvgDtrByte(address, fadeTimeIndex, Dali::Command::DTR_AS_FADE_TIME);

    uint8_t fadeRateIndex = data[10] & 0xF;
    logDebugP("set fade rate %s", FadeRateLabels[fadeRateIndex]);
    writeEvgDtrByte(address, fadeRateIndex, Dali::Command::DTR_AS_FADE_RATE);

    // 1byte free

    uint16_t groupBits = data[12];
    groupBits |= data[13] << 8;
    channels[address].setGroups(groupBits, this->groups);

    for (int i = 0; i < GroupCount; i++)
    {
        if ((groupBits >> i) & 0x1)
        {
            logDebugP("add to Group %i", i);
            daliMaster.sendCommand(address, Dali::Command::ADD_TO_GROUP | i);
        }
        else
        {
            logDebugP("remove from Group %i", i);
            daliMaster.sendCommand(address, Dali::Command::REMOVE_FROM_GROUP | i);
        }
    }
    logIndentDown();

    resultLength = 0;
}

uint8_t DaliModule::readEvgLevel(uint8_t address, uint8_t command, const char *label, uint8_t errorBit, uint8_t &errorByte)
{
    int16_t resp = getInfo(address, command);
    if (resp < 0)
    {
        logErrorP("Dali Error (%s): Code %i", label, resp);
        errorByte |= errorBit;
        resp = 0xFF;
    }
    logDebugP("%s: %.2X / %.2f", label, resp, DaliHelper::arcToPercentFloat(resp));
    return (uint8_t)resp;
}

uint8_t DaliModule::readEvgRaw(uint8_t address, uint8_t command, const char *label, uint8_t errorBit, uint8_t errorFallback, uint8_t &errorByte)
{
    int16_t resp = getInfo(address, command);
    if (resp < 0)
    {
        logErrorP("Dali Error (%s): Code %i", label, resp);
        errorByte |= errorBit;
        resp = errorFallback;
    }
    logDebugP("%s: %.2X", label, resp);
    return (uint8_t)resp;
}

void DaliModule::funcHandleEvgRead(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Starting reading EVG settings");

    uint8_t address = data[1];
    resultData[0] = 0x00;

    uint8_t errorByte = 0;

    resultData[1] = readEvgLevel(address, Dali::Command::QUERY_MIN_LEVEL, "MIN", 0b1, errorByte);
    resultData[2] = readEvgLevel(address, Dali::Command::QUERY_MAX_LEVEL, "MAX", 0b10, errorByte);
    resultData[3] = readEvgLevel(address, Dali::Command::QUERY_POWER_ON_LEVEL, "POWER", 0b100, errorByte);
    resultData[4] = readEvgLevel(address, Dali::Command::QUERY_FAIL_LEVEL, "FAILURE", 0b1000, errorByte);
    resultData[5] = readEvgRaw(address, Dali::Command::QUERY_FADE_SPEEDS, "FAID", 0b10000, 0xFF, errorByte);

    // 1byte free

    resultData[7] = readEvgRaw(address, Dali::Command::QUERY_GROUPS_0_7, "GROUPS0-7", 0b1000000, 0, errorByte);
    resultData[8] = readEvgRaw(address, Dali::Command::QUERY_GROUPS_8_15, "GROUPS8-15", 0b10000000, 0, errorByte);

    resultData[9] = errorByte;
    resultLength = 10;
}

void DaliModule::funcHandleSetScene(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    /*
    data = [
        12,
        context.Channel,
        0, //scene number
        0, //enabled
        parseInt(device.getParameterByName("deviceType").value),
        parseInt(device.getParameterByName("colorType").value),
        0, value 0-100; 255=disabled
        0, r / tw
        0, g / tw
        0  b
    ];
    */
    uint8_t addr = data[1] & 0b111111;

    // scene is enabled
    if (data[3])
    {
        logDebugP("Scene %i bri %i", data[2], data[6]);
        logIndentUp();

        // deviceType is Color
        if (data[4] == PT_deviceType_DT8)
        {
            // colorType is TunableWhite
            if (data[5] == PT_colorType_TW)
            {
                uint16_t kelvin;
                popWord(kelvin, data + 8);
                logDebugP("Temp %i", kelvin);
                uint16_t mirek = 1000000.0 / kelvin;
                logDebugP("mirek %i", mirek);
                daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, mirek & 0xFF);
                daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR1, (mirek >> 8) & 0xFF);
                daliMaster.sendExtendedCommand(addr, 0x08, Dali::ExtendedCommandDT8::SET_TEMP_COLOUR_TEMPERATURE);
            }
            else
            { // it is RGB
                daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, data[8]);
                daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR1, data[9]);
                daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR2, data[10]);
                daliMaster.sendExtendedCommand(addr, 0x08, Dali::ExtendedCommandDT8::SET_TEMP_RGB_LEVEL);
                logDebugP("RGB %.2X%.2X%.2X", data[8], data[9], data[10]);
            }
        }

        uint16_t tempValue = 0;
        popWord(tempValue, data + 6);
        if (tempValue == DisabledDptValue)
            logDebugP("bri disabled");
        else
            logDebugP("bri %.2f%%", ColorHelper::getFloat(tempValue) * 100);

        daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, (tempValue == DisabledDptValue) ? 255 : DaliHelper::percentToArc(ColorHelper::getFloat(tempValue) * 100));
        daliMaster.sendCommand(addr, Dali::Command::DTR_AS_SCENE | data[2]);
        logIndentDown();
    }
    else
    {
        daliMaster.sendCommand(addr, Dali::Command::REMOVE_FROM_SCENE | data[2]);
        logDebugP("Scene %i disabled", data[2]);
    }

    resultLength = 0;
}

void DaliModule::funcHandleGetScene(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    /*
    data = [
        13,
        context.Channel,
        0, //scene number
        parseInt(device.getParameterByName("deviceType").value),
        parseInt(device.getParameterByName("colorType").value)
    ];
    */
    logDebugP("Scene %i", data[2]);
    uint8_t value = getInfo(data[1], Dali::Command::QUERY_SCENE_LEVEL | data[2]);
    logDebugP("Value %i", value);

    resultData[0] = value;

    if (value != 0xFF && data[3] == PT_deviceType_DT8)
    {
        // colorType is TunableWhite
        if (data[4] == PT_colorType_TW)
        {
            resultLength = 3;
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, 0xE2);
            // TODO rework to use getExtendedInfo ore something
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::ENABLE_DT, 0x08);
            uint16_t mirek = getInfo(data[1], Dali::ExtendedCommandDT8::QUERY_COLOUR_VALUE) << 8;
            mirek |= getInfo(data[1], Dali::Command::QUERY_DTR);
            logDebugP("mirek %i", mirek);

            uint16_t kelvin = 1000000.0 / mirek;

            resultData[1] = (kelvin >> 8) & 0xFF;
            resultData[2] = kelvin & 0xFF;
            logDebugP("Scene %i: %.1f%% TEMP=%iK", data[2], DaliHelper::arcToPercentFloat(value), kelvin);
        }
        else
        { // it is RGB
            resultLength = 4;
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, 0xE9);
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::ENABLE_DT, 0x08);
            uint8_t colorVal = getInfo(data[1], Dali::ExtendedCommandDT8::QUERY_COLOUR_VALUE); // TODO this works?
            resultData[1] = colorVal;
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, 0xEA);
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::ENABLE_DT, 0x08);
            colorVal = getInfo(data[1], Dali::ExtendedCommandDT8::QUERY_COLOUR_VALUE);
            resultData[2] = colorVal;
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, 0xEB);
            daliMaster.sendSpecialCommand(Dali::SpecialCommand::ENABLE_DT, 0x08);
            colorVal = getInfo(data[1], Dali::ExtendedCommandDT8::QUERY_COLOUR_VALUE);
            resultData[3] = colorVal;
            logDebugP("Scene %i: %.1f%% RGB=%.2X%.2X%.2X", data[2], DaliHelper::arcToPercentFloat(value), resultData[1], resultData[2], resultData[3]);
        }
    }
    else
    {
        resultLength = 1;
        logDebugP("Scene %i: %.1f%%", data[2], DaliHelper::arcToPercentFloat(value));
    }
}

void DaliModule::funcHandleIdentify(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    bool isGroupAddress = data[2] == 1;
    uint8_t address = data[1];
    bool isStopCommand = data[3];
    logDebugP("Got identify for %s: %u. Stop: %u", isGroupAddress ? "group" : "address", address, isStopCommand);
    
    if (isStopCommand)
    {
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::TERMINATE, 0);
        daliMaster.sendCommand(BroadcastAddress, Dali::Command::OFF, true);
    }
    else
    {
        daliMaster.sendSpecialCommand(Dali::SpecialCommand::INITIALISE, 0);
        daliMaster.sendCommand(BroadcastAddress, Dali::Command::OFF, true);
        daliMaster.sendCommand(address, Dali::Command::RECALL_MAX, isGroupAddress);
    }
    resultLength = 0;
}

bool DaliModule::processFunctionPropertyState(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    if (objectIndex != 160 || propertyId != 1)
        return false;

    switch (data[0])
    {
    case 3:
    case 5:
        stateHandleScanAndAddress(data, resultData, resultLength);
        return true;

    case 4:
        stateHandleAssign(data, resultData, resultLength);
        return true;

    case 7:
        stateHandleFoundEVGs(data, resultData, resultLength);
        return true;
    }
    return false;
}

void DaliModule::stateHandleAssign(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    resultData[0] = (uint8_t)_addressing.isAddressingIdle(); // ? AssigningState::Success : AssigningState::Working);
    resultData[1] = (uint8_t)_addressing.assignResponse();
    resultLength = 2;
}

void DaliModule::stateHandleScanAndAddress(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    resultData[0] = _addressing.isAddressingIdle();
    if (data[0] == 3)
    {
        resultData[1] = _addressing.foundCount();
        resultLength = 2;
    }
    else
    {
        resultLength = 1;
    }
}

void DaliModule::stateHandleFoundEVGs(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    if (data[1] == 254)
    {
        // delete[] ballasts;
        // delete[] addresses;
        resultLength = 0;
        _addressing.resetToIdle();
        // _assState = AssigningState::None;
        return;
    }

    resultData[0] = data[1] < _addressing.foundCount();
    if (data[1] < _addressing.foundCount())
    {
        const Ballast &foundBallast = _addressing.ballast(data[1]);
        resultData[1] = foundBallast.high;
        resultData[2] = foundBallast.middle;
        resultData[3] = foundBallast.low;
        resultData[4] = foundBallast.address;
        resultLength = 5;
    }
    else
    {
        resultLength = 1;
    }
}

DaliModule openknxDaliModule;