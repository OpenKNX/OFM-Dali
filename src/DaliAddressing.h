#pragma once

#include <Arduino.h>
#include "OpenKNX.h"
#include "Dali/Master.h"
#include "Dali/Commands.h"
#include "Ballast.hpp"
#include "DaliConstants.h"

class DaliAddressing
{
    public:
        static constexpr uint8_t MaxDevices = DaliConstants::ChannelCount;
        static constexpr uint8_t BroadcastAddress = DaliConstants::BroadcastAddress;
        static constexpr uint8_t NoShortAddress = 255;
        static constexpr uint32_t MaxSearchAddress = 0xFFFFFF;
        static constexpr uint8_t SearchBitCount = 24;
        static constexpr uint32_t InitialSearchStep = 0x800000;
        static constexpr unsigned long RandomizeWaitMs = 100;

        enum class AssigningResponse : uint8_t {
            AR_SUCCESS,
            NOT_FREE,
            NO_RESPONSE,
            NO_RESPONSE_LONG,
            FAILED
        };

        explicit DaliAddressing(Dali::Master &daliMaster);

        // Returns true if a scan or assign workflow is currently running.
        bool loop();

        bool isAddressingIdle() const;
        bool isAssigningIdle() const;

        void startScan(bool onlyUnaddressed, bool randomize, bool deleteAll, bool assignShort);
        void startAssign(uint32_t longAddress, uint8_t shortAddress);
        void resetToIdle();

        int foundCount() const;
        const Ballast &ballast(uint8_t index) const;
        AssigningResponse assignResponse() const;

    private:
        enum class AddressingState : uint8_t {
            OFF,
            INIT,
            INIT2,
            WRITE_DTR,
            REMOVE_SHORT,
            REMOVE_SHORT2,
            RANDOM,
            RANDOMWAIT,
            STARTSEARCH,
            SEARCHHIGH,
            SEARCHMID,
            SEARCHLOW,
            COMPARE,
            GETSHORT,
            CHECKFOUND,
            PROGRAMSHORT,
            VERIFYSHORT,
            VERIFYSHORTRESPONSE,
            WITHDRAW,
            TERMINATE,
            SEARCHSHORT,
            CHECKSEARCHSHORT
        };
        enum class AssigningState : uint8_t {
            OFF,
            INIT,
            QUERY,
            CHECKQUERY,
            STARTSEARCH,
            COMPARE,
            CHECKFOUND,
            WITHDRAW,
            PROGRAMSHORT,
            VERIFYSHORT,
            VERIFYSHORTRESPONSE,
            TERMINATE
        };

        void loopAddressing();
        void loopAssigning();

        // logInfoP/logErrorP/logDebugP require this method to be in scope.
        const std::string logPrefix();

        // One method per AddressingState.
        void stepAddressingInit();
        void stepAddressingWriteDtr();
        void stepAddressingRemoveShort();
        void stepAddressingRemoveShort2();
        void stepAddressingRandom();
        void stepAddressingRandomWait();
        void stepAddressingStartSearch();
        void stepAddressingSearchHigh();
        void stepAddressingSearchMid();
        void stepAddressingSearchLow();
        void stepAddressingCompare();
        void stepAddressingCheckFound();
        void stepAddressingGetShort();
        void stepAddressingProgramShort();
        void stepAddressingVerifyShort();
        void stepAddressingVerifyShortResponse();
        void stepAddressingWithdraw();
        void stepAddressingTerminate();
        void stepAddressingSearchShort();
        void stepAddressingCheckSearchShort();

        // One method per AssigningState.
        void stepAssigningInit();
        void stepAssigningQuery();
        void stepAssigningCheckQuery();
        void stepAssigningStartSearch();
        void stepAssigningCompare();
        void stepAssigningWithdraw();
        void stepAssigningCheckFound();
        void stepAssigningProgramShort();
        void stepAssigningVerifyShort();
        void stepAssigningVerifyShortResponse();
        void stepAssigningTerminate();

        Dali::Master &_daliMaster;

        AddressingState _adrState = AddressingState::OFF;
        AssigningState _assState = AssigningState::OFF;
        AssigningResponse _assResponse = AssigningResponse::AR_SUCCESS;

        Ballast _ballasts[MaxDevices];
        bool _addresses[MaxDevices];

        int _adrFound = 0;
        uint8_t _adrNew = 0;
        byte _adrIterations = 0;
        unsigned long _adrSearch = 0;
        bool _adrAssign = false;
        bool _adrOnlyNew = false;
        bool _adrRandomize = false;
        bool _adrDeleteAll = false;
        uint32_t _adrResponse = 0;
};
