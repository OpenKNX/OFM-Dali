#include "DaliAddressing.h"

DaliAddressing::DaliAddressing(Dali::Master &daliMaster) : _daliMaster(daliMaster)
{
}

const std::string DaliAddressing::logPrefix()
{
    return "DaliAddressing";
}

bool DaliAddressing::loop()
{
    if (_adrState != AddressingState::OFF)
    {
        loopAddressing();
        return true;
    }
    if (_assState != AssigningState::OFF)
    {
        loopAssigning();
        return true;
    }
    return false;
}

bool DaliAddressing::isAddressingIdle() const
{
    return _adrState == AddressingState::OFF;
}

bool DaliAddressing::isAssigningIdle() const
{
    return _assState == AssigningState::OFF;
}

void DaliAddressing::startScan(bool onlyUnaddressed, bool randomize, bool deleteAll, bool assignShort)
{
    _adrOnlyNew = onlyUnaddressed;
    _adrRandomize = randomize;
    _adrDeleteAll = deleteAll;
    _adrAssign = assignShort;

    _adrFound = 0;
    for (uint8_t i = 0; i < MaxDevices; i++)
        _addresses[i] = false;

    if (!_adrDeleteAll && _adrAssign)
        _adrState = AddressingState::SEARCHSHORT;
    else
        _adrState = AddressingState::INIT;
}

void DaliAddressing::startAssign(uint32_t longAddress, uint8_t shortAddress)
{
    _adrSearch = longAddress;
    _adrNew = shortAddress;
    _assState = AssigningState::INIT;
}

void DaliAddressing::resetToIdle()
{
    _adrState = AddressingState::OFF;
}

int DaliAddressing::foundCount() const
{
    return _adrFound;
}

const Ballast &DaliAddressing::ballast(uint8_t index) const
{
    return _ballasts[index];
}

DaliAddressing::AssigningResponse DaliAddressing::assignResponse() const
{
    return _assResponse;
}

void DaliAddressing::loopAddressing()
{
    switch (_adrState)
    {
    case AddressingState::INIT:
        stepAddressingInit();
        break;
    case AddressingState::WRITE_DTR:
        stepAddressingWriteDtr();
        break;
    case AddressingState::REMOVE_SHORT:
        stepAddressingRemoveShort();
        break;
    case AddressingState::REMOVE_SHORT2:
        stepAddressingRemoveShort2();
        break;
    case AddressingState::RANDOM:
        stepAddressingRandom();
        break;
    case AddressingState::RANDOMWAIT:
        stepAddressingRandomWait();
        break;
    case AddressingState::STARTSEARCH:
        stepAddressingStartSearch();
        break;
    case AddressingState::SEARCHHIGH:
        stepAddressingSearchHigh();
        break;
    case AddressingState::SEARCHMID:
        stepAddressingSearchMid();
        break;
    case AddressingState::SEARCHLOW:
        stepAddressingSearchLow();
        break;
    case AddressingState::COMPARE:
        stepAddressingCompare();
        break;
    case AddressingState::CHECKFOUND:
        stepAddressingCheckFound();
        break;
    case AddressingState::GETSHORT:
        stepAddressingGetShort();
        break;
    case AddressingState::PROGRAMSHORT:
        stepAddressingProgramShort();
        break;
    case AddressingState::VERIFYSHORT:
        stepAddressingVerifyShort();
        break;
    case AddressingState::VERIFYSHORTRESPONSE:
        stepAddressingVerifyShortResponse();
        break;
    case AddressingState::WITHDRAW:
        stepAddressingWithdraw();
        break;
    case AddressingState::TERMINATE:
        stepAddressingTerminate();
        break;
    case AddressingState::SEARCHSHORT:
        stepAddressingSearchShort();
        break;
    case AddressingState::CHECKSEARCHSHORT:
        stepAddressingCheckSearchShort();
        break;
    default:
        // AddressingState::INIT2 is never set.
        break;
    }
}

void DaliAddressing::stepAddressingInit()
{
    _adrFound = 0;
    if (_adrOnlyNew)
        logInfoP("Searching unaddressed only");
    else
        logInfoP("Searching all");

    if (_adrRandomize)
        logInfoP("Do Randomize");
    else
        logInfoP("Don't Randomize");

    if (_adrDeleteAll)
        logInfoP("Delete all short addresses");
    else
        logInfoP("Keeping all short addresses");

    if (_adrAssign)
        logInfoP("Assigning short addresses");
    else
        logInfoP("Not assigning short addresses");

    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::INITIALISE, _adrOnlyNew ? 255 : 0);
    if (_adrDeleteAll)
        _adrState = AddressingState::WRITE_DTR;
    else
        _adrState = (_adrRandomize ? AddressingState::RANDOM : AddressingState::STARTSEARCH);
}

void DaliAddressing::stepAddressingWriteDtr()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SET_DTR, NoShortAddress);
    _adrState = AddressingState::REMOVE_SHORT;
}

void DaliAddressing::stepAddressingRemoveShort()
{
    _daliMaster.sendCommand(BroadcastAddress, Dali::Command::DTR_AS_SHORT, true);
    _adrState = AddressingState::REMOVE_SHORT2;
}

void DaliAddressing::stepAddressingRemoveShort2()
{
    _daliMaster.sendCommand(BroadcastAddress, Dali::Command::DTR_AS_SHORT, true);
    _adrState = (_adrRandomize ? AddressingState::RANDOM : AddressingState::STARTSEARCH);
}

void DaliAddressing::stepAddressingRandom()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::RANDOMISE);
    _adrState = AddressingState::RANDOMWAIT;
    _adrSearch = millis();
}

void DaliAddressing::stepAddressingRandomWait()
{
    if (millis() - _adrSearch > RandomizeWaitMs)
        _adrState = AddressingState::STARTSEARCH;
}

void DaliAddressing::stepAddressingStartSearch()
{
    _adrIterations = 0;
    _adrSearch = MaxSearchAddress;
    // STARTSEARCH always continues into SEARCHHIGH within the same tick.
    stepAddressingSearchHigh();
}

void DaliAddressing::stepAddressingSearchHigh()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SEARCHADDRH, (_adrSearch >> 16) & 0xFF, true);
    _adrState = AddressingState::SEARCHMID;
}

void DaliAddressing::stepAddressingSearchMid()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SEARCHADDRM, (_adrSearch >> 8) & 0xFF);
    _adrState = AddressingState::SEARCHLOW;
}

void DaliAddressing::stepAddressingSearchLow()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SEARCHADDRL, (_adrSearch) & 0xFF);
    _adrState = AddressingState::COMPARE;
}

void DaliAddressing::stepAddressingCompare()
{
    _adrResponse = _daliMaster.sendSpecialCommand(Dali::SpecialCommand::COMPARE, 0, true);
    _adrState = AddressingState::CHECKFOUND;
}

void DaliAddressing::stepAddressingCheckFound()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);

    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        if (_adrIterations >= SearchBitCount) // ballast found
        {
            logInfoP("Found ballast at %.6X", _adrSearch);
            _ballasts[_adrFound].high = (_adrSearch >> 16) & 0xFF;
            _ballasts[_adrFound].middle = (_adrSearch >> 8) & 0xFF;
            _ballasts[_adrFound].low = _adrSearch & 0xFF;
            if (_adrAssign)
            {
                _adrState = AddressingState::PROGRAMSHORT;
            }
            else
            {
                _adrState = AddressingState::GETSHORT;
                _adrResponse = _daliMaster.sendSpecialCommand(Dali::SpecialCommand::QUERY_SHORT, 0, true);
            }
        }
        else
        {
            _adrSearch -= (InitialSearchStep >> _adrIterations);
            _adrState = AddressingState::SEARCHHIGH;
        }
    }
    else if (_adrIterations == 0 || _adrIterations > SearchBitCount) // no device at all responded or error
        _adrState = AddressingState::TERMINATE;
    else if (_adrIterations == SearchBitCount)
    {                 // device responded before, but didn't now, so address is one higher
        _adrSearch++; // and for the device to act at upcoming commands, we need to send the actual address
        _adrState = AddressingState::SEARCHHIGH;
    }
    else
    { // there's a device that didn't respond anymore, increase address
        _adrSearch += (InitialSearchStep >> _adrIterations);
        _adrState = AddressingState::SEARCHHIGH;
    }
    _adrIterations++;
}

void DaliAddressing::stepAddressingGetShort()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);
    uint8_t responseAddr = NoShortAddress;

    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        if (response.frame.flags & DALI_FRAME_ERROR)
        {
            responseAddr = NoShortAddress;
        }
        else
        {
            responseAddr = response.frame.data & 0xFF;
        }
    }
    else
    {
        responseAddr = NoShortAddress;
    }

    if (responseAddr == NoShortAddress)
    {
        logInfoP(" -> has no Short Address");
    }
    else
    {
        logInfoP(" -> has Short Address %i", responseAddr >> 1);
        responseAddr = responseAddr >> 1;
    }

    _ballasts[_adrFound].address = responseAddr;
    _adrFound++;
    _adrState = AddressingState::WITHDRAW;
}

void DaliAddressing::stepAddressingProgramShort()
{
    _adrNew = 0;
    while (_addresses[_adrNew] == true)
        _adrNew++;
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::PROGRAMSHORT, (_adrNew << 1) | 1, true);
    _ballasts[_adrFound].address = _adrNew;
    _addresses[_adrNew] = true;
    _adrFound++;
    _adrState = AddressingState::VERIFYSHORT;
}

void DaliAddressing::stepAddressingVerifyShort()
{
    _adrResponse = _daliMaster.sendSpecialCommand(Dali::SpecialCommand::VERIFYSHORT, (_adrNew << 1) | 1, true);
    _adrState = AddressingState::VERIFYSHORTRESPONSE;
}

void DaliAddressing::stepAddressingVerifyShortResponse()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);
    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        if (response.frame.flags & DALI_FRAME_ERROR || (response.frame.data & 0xFF) != 0xFF)
        {
            logErrorP(" -> error setting address %i", _adrNew);
            _adrState = AddressingState::TERMINATE;
        }
        else
        {
            logInfoP(" -> new address %i", _adrNew);
            _adrState = AddressingState::WITHDRAW;
        }
    }
    else
    {
        logErrorP(" -> error setting address %i", _adrNew);
        _adrState = AddressingState::TERMINATE;
    }
}

void DaliAddressing::stepAddressingWithdraw()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::WITHDRAW);
    _adrState = AddressingState::STARTSEARCH;
}

void DaliAddressing::stepAddressingTerminate()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::TERMINATE);
    _adrState = AddressingState::OFF;
    logInfoP("Found %i ballasts", _adrFound);
}

void DaliAddressing::stepAddressingSearchShort()
{
    _adrResponse = _daliMaster.sendCommand(_adrFound, Dali::Command::QUERY_ACTUAL_LEVEL, false, true);
    _adrState = AddressingState::CHECKSEARCHSHORT;
}

void DaliAddressing::stepAddressingCheckSearchShort()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);
    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        _addresses[_adrFound] = true;
        _adrFound++;
        _adrState = _adrFound < MaxDevices ? AddressingState::SEARCHSHORT : AddressingState::INIT;
    }
    else
    {
        _addresses[_adrFound] = false;
        _adrFound++;
        _adrState = _adrFound < MaxDevices ? AddressingState::SEARCHSHORT : AddressingState::INIT;
    }
}

void DaliAddressing::loopAssigning()
{
    // if (dali->busIsIdle())
    // { // wait until bus is idle
    switch (_assState)
    {
    case AssigningState::INIT:
        stepAssigningInit();
        break;
    case AssigningState::QUERY:
        stepAssigningQuery();
        break;
    case AssigningState::CHECKQUERY:
        stepAssigningCheckQuery();
        break;
    case AssigningState::STARTSEARCH:
        stepAssigningStartSearch();
        break;
    case AssigningState::COMPARE:
        stepAssigningCompare();
        break;
    case AssigningState::WITHDRAW:
        stepAssigningWithdraw();
        break;
    case AssigningState::CHECKFOUND:
        stepAssigningCheckFound();
        break;
    case AssigningState::PROGRAMSHORT:
        stepAssigningProgramShort();
        break;
    case AssigningState::VERIFYSHORT:
        stepAssigningVerifyShort();
        break;
    case AssigningState::VERIFYSHORTRESPONSE:
        stepAssigningVerifyShortResponse();
        break;
    case AssigningState::TERMINATE:
        stepAssigningTerminate();
        break;
    }
    //}
}

void DaliAddressing::stepAssigningInit()
{
    _adrFound = 0;
    _adrAssign = false;
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::INITIALISE);
    _assState = AssigningState::QUERY;
}

void DaliAddressing::stepAssigningQuery()
{
    _adrResponse = _daliMaster.sendCommand(_adrNew, Dali::Command::QUERY_ACTUAL_LEVEL, false, true);
    _assState = AssigningState::CHECKQUERY;
}

void DaliAddressing::stepAssigningCheckQuery()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);
    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        logInfoP("Short Address is in use");
        _assState = AssigningState::OFF;
        _assResponse = AssigningResponse::NOT_FREE;
    }
    else
    {
        logInfoP("Short Address is free");
        _adrSearch--;
        _assState = AssigningState::STARTSEARCH;
    }
}

void DaliAddressing::stepAssigningStartSearch()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SEARCHADDRH, (_adrSearch >> 16) & 0xFF);
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SEARCHADDRM, (_adrSearch >> 8) & 0xFF);
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::SEARCHADDRL, (_adrSearch) & 0xFF);
    _assState = AssigningState::COMPARE;
}

void DaliAddressing::stepAssigningCompare()
{
    if (_adrAssign)
    {
        _adrResponse = _daliMaster.sendSpecialCommand(Dali::SpecialCommand::COMPARE, 0, true);
        _assState = AssigningState::CHECKFOUND;
    }
    else
    {
        _daliMaster.sendSpecialCommand(Dali::SpecialCommand::COMPARE);
        _assState = AssigningState::WITHDRAW;
    }
}

void DaliAddressing::stepAssigningWithdraw()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::WITHDRAW);
    _adrSearch++;
    _adrAssign = true;
    _assState = AssigningState::STARTSEARCH;
}

void DaliAddressing::stepAssigningCheckFound()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);
    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        logInfoP("Long Address does exist");
        _assState = AssigningState::PROGRAMSHORT;
    }
    else
    {
        logInfoP("Long Address does not exist");
        _assState = AssigningState::OFF;
        _assResponse = AssigningResponse::NO_RESPONSE_LONG;
    }
}

void DaliAddressing::stepAssigningProgramShort()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::PROGRAMSHORT, (_adrNew << 1) | 1);
    _ballasts[_adrFound].address = _adrNew;
    _assState = AssigningState::VERIFYSHORT;
}

void DaliAddressing::stepAssigningVerifyShort()
{
    _adrResponse = _daliMaster.sendSpecialCommand(Dali::SpecialCommand::VERIFYSHORT, (_adrNew << 1) | 1, true);
    _assState = AssigningState::VERIFYSHORTRESPONSE;
}

void DaliAddressing::stepAssigningVerifyShortResponse()
{
    Dali::Response response = _daliMaster.getResponse(_adrResponse);
    if (response.state == Dali::ResponseState::SENT || response.state == Dali::ResponseState::WAITING)
    {
        // no answer yet
        return;
    }
    else if (response.state == Dali::ResponseState::RECEIVED)
    {
        logInfoP(" -> new address %i", _adrNew);
        _assResponse = AssigningResponse::AR_SUCCESS;
    }
    else
    {
        // error, stop commissioning
        logErrorP(" -> error setting address %i", _adrNew);
        _assResponse = AssigningResponse::FAILED;
    }
    _assState = AssigningState::TERMINATE;
}

void DaliAddressing::stepAssigningTerminate()
{
    _daliMaster.sendSpecialCommand(Dali::SpecialCommand::TERMINATE);
    _assState = AssigningState::OFF;
}
