#pragma once

#include <Arduino.h>
#include "OpenKNX.h"
#include "Dali/Master.h"
#include "Dali/Commands.h"
#include "DaliChannel.h"
#include "DaliAddressing.h"
#include "DaliConstants.h"
#include "Ballast.hpp"
#include "HclCurve.h"

#ifndef DALI_WAIT_RANDOMIZE
#define DALI_WAIT_RANDOMIZE 1000
#endif
#ifndef DALI_WAIT_SEARCH
#define DALI_WAIT_SEARCH 300
#endif

#define DGWS_CountNumber 64

#define LED_FUNC_ID_DALI_BUSSTATE 500

typedef void (*EventHandlerChangedGroupFuncPtr)(uint8_t index, uint8_t value);

class DaliModule : public OpenKNX::Module
{
	public:
		static constexpr uint8_t ChannelCount = DaliConstants::ChannelCount;
		static constexpr uint8_t GroupCount = DaliConstants::GroupCount;
		static constexpr uint8_t HclCurveCount = DaliConstants::HclCurveCount;
		static constexpr uint8_t BroadcastAddress = DaliConstants::BroadcastAddress;
		static constexpr uint16_t DisabledDptValue = 0xFFFF;

		void loop(bool configured) override;
		void loop1(bool configured);
		void setup(bool conf) override;
		void setup1(bool conf);
		bool processCommand(const std::string cmd, bool diagnoseKo) override;
		void processInputKo(GroupObject &ko) override;
		void showHelp() override;

		const std::string name() override;
		const std::string version() override;

		bool getDaliBusState();

		bool processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength) override;
		bool processFunctionPropertyState(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength) override;

		Dali::Master daliMaster;
		
	private:
		void loopBusState();
		void loopInitData();
		void loopGroupState();
#ifdef INFO2_LED_PIN
		void loopError();
#endif
#ifdef FUNC1_BUTTON_PIN
		void handleFunc(uint8_t setting);
		bool _currentToggleState = false;
		uint8_t _currentIdentifyDevice = 0;
#endif
		bool _currentLockState = false;
		int16_t getInfo(byte address, uint8_t command, uint8_t additional = 0);
	
		DaliAddressing _addressing{daliMaster};
		uint8_t _initDataIndex = 0;
		uint8_t _lastBusState = 2;


		uint8_t _lastChangedGroup = 255;
		uint8_t _lastChangedValue = 0;
		uint8_t _lastTimeMinute = 255;

		bool _gotInitData = false;
		bool _daliBusState = true;
		bool _daliBusStateToSet = true;
		unsigned long _daliStateLast = 1;
		// DaliChannel has a Dali::Master& member, so each element must be
		// explicitly initialized with `daliMaster` - it cannot be default-constructed.
		DaliChannel channels[ChannelCount] {daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster};
		DaliChannel groups[GroupCount] {daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster,daliMaster};
		HclCurve curves[HclCurveCount];
		#ifdef DALI_NO_TIMER
		struct repeating_timer _timer;
		#endif

		void koHandleSwitch(GroupObject & ko);
		void koHandleDimm(GroupObject & ko);
		void koHandleDayNight(GroupObject & ko);
		void koHandleOnValue(GroupObject & ko);
		void koHandleScene(GroupObject & ko);

		bool processChannelKo(int koNum, GroupObject &ko);
		bool processGroupKo(int koNum, GroupObject &ko);
		bool processHclKo(int koNum, GroupObject &ko);
		void processGlobalKo(int koNum, GroupObject &ko);

		void broadcastGroupState(bool value);
		void broadcastGroupState(uint8_t value);
		void setAllOnValue(uint8_t value);
		void setAllNightMode(bool value);

		void funcHandleType(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void funcHandleScan(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void funcHandleAssign(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void funcHandleEvgWrite(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void writeEvgDtrByte(uint8_t address, uint8_t value, uint8_t command);
		uint8_t readEvgLevel(uint8_t address, uint8_t command, const char *label, uint8_t errorBit, uint8_t &errorByte);
		uint8_t readEvgRaw(uint8_t address, uint8_t command, const char *label, uint8_t errorBit, uint8_t errorFallback, uint8_t &errorByte);
		void funcHandleEvgRead(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void funcHandleSetScene(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void funcHandleGetScene(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void funcHandleIdentify(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);

		void cmdHandleScan(bool hasArg, std::string arg);
		void cmdHandleArc(bool hasArg, std::string arg);
		void cmdHandleSet(bool hasArg, std::string arg);
		void cmdHandleStepUp(bool hasArg, std::string arg);
		void cmdHandleStepDown(bool hasArg, std::string arg);
		void cmdHandleGetLvl(bool hasArg, std::string arg);

		void stateHandleAssign(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void stateHandleScanAndAddress(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
		void stateHandleFoundEVGs(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
};

extern DaliModule openknxDaliModule;