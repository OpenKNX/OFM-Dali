#pragma once

#include "OpenKNX.h"
#include "Dali/Master.h"
#include "Dali/Commands.h"
#include "colorhelper.h"
#include "DaliHelper.h"
#include "DaliConstants.h"

#define DimmInterval 200
#define DimmStatusInterval 500

class DaliChannel : public OpenKNX::Channel
{
	public:
        static constexpr uint16_t AllGroups = DaliConstants::AllGroups;

        DaliChannel(Dali::Master &master);
        ~DaliChannel();

		void loop() override;
		void loop1();
		void setup() override;
		void processInputKo(GroupObject &ko) override;
		const bool isConfigured();
		const bool isGroup();
		bool hasError();

		void init(uint8_t channelIndex, bool isGroup);
		void setOnValue(uint8_t value);
		void setGroups(uint16_t groupBits, DaliChannel* groups);
		void setDimmRef(uint8_t ref);
		void setGroupState(uint16_t group, bool state);
		void setGroupState(uint16_t group, uint8_t value);
		void setMinArc(uint8_t max);
		void setMaxArc(uint8_t max);
		void setHcl(uint8_t curve, uint16_t temp);
		void setHcl(uint8_t curve, uint8_t bri);
		uint8_t getMin();
		uint8_t getMax();
		uint16_t getGroups();

		bool isNight = false;
		const std::string name() override;
		// void writeFlash() override;
		// void readFlash(const uint8_t* data, const uint16_t size) override;
		// uint16_t flashSize() override;

	private:
        static constexpr uint8_t GroupCount = DaliConstants::GroupCount;
        static constexpr uint8_t HclDisabled = 255;
        static constexpr uint8_t NoDimmReference = 255;

        enum class DimmDirection : uint8_t {
			Down,
			Up,
			None
		};
		enum class DimmType : uint8_t {
			Brigthness,
			Color
		};
		enum class ColorChannel : uint8_t {
			Red = 0,
			Green = 1,
			Blue = 2
		};

		Dali::Master &daliMaster;

		//Relative dimming
		DimmDirection _dimmDirection = DimmDirection::None;
		uint8_t _dimmStep = 0;
		unsigned long _dimmLast = 0;
		unsigned long _dimmLastStatus = 0;
		uint8_t *currentDimmValue;
		DimmType currentDimmType;
		uint8_t _dimmStatusInterval = 0;
		uint8_t _dimmReferenceAddress = NoDimmReference;
		//Staircase light
		unsigned long startTime = 0;
		uint interval = 0;
		//Initial values
		uint8_t _min = 0;
		uint8_t _max = 0;
		uint8_t _onDay = 100;
		uint8_t _onNight = 10;
		bool _isStaircase = false;
		bool _isGroup = false;
		bool _isConfigured = false;
		//HCL
		uint8_t _hclCurve = HclDisabled;
		uint16_t _hclCurrentTemp = 0;
		uint8_t _hclCurrentBri = 0;
		bool _hclIsAlsoOn = false;
		bool _hclIsAutoMode = true;
		bool _hclLastState = true; //TODO remove
		//Reading EVG error state
		bool _getError = false;
		bool _errorState = false;
		uint32_t _errorResp = 0;
		unsigned long _lastError = 40000;
		//Current state
		bool currentState = false;
		//Current brightness
		uint8_t currentStep = 0;
		//Current lock state
		bool currentIsLocked = false;
		//Current color
		uint8_t currentColor[4];

		//Querying current state
		uint32_t _queryId = 0;
		uint16_t _queryInterval = 0;
		unsigned long _lastValueQuery = 0;

		//Turn-on with last value
		uint8_t _lastDayValue = 100;
		uint8_t _lastNightValue = 10;

		//Group membership
		uint16_t _groups = 0;

		void loopError();
		void loopDimming();
		void loopStaircase();
		void loopQueryLevel();
		uint16_t calcKoNumber(int asap);
		void setSwitchState(bool value, bool isSwitchCommand = true);
		void setDimmState(uint8_t value, bool isDimmCommand = true, bool isLastCommand = false);
		void updateCurrentDimmValue();
		void sendColor();
		void sendColorAsRGB(uint8_t r, uint8_t g, uint8_t b);
		void sendColorAsXY(uint8_t r, uint8_t g, uint8_t b);
		void sendKoStateOnChange(uint16_t koNr, const KNXValue &value, const Dpt &type);
		void setTemperature(uint16_t value);
		void setBrightness(uint8_t value);
		bool isDimmOnLocked();
		bool isDimmOffLocked();
		void queryActualLevel();
		
		void koHandleSwitch(GroupObject &ko);
		void koHandleDimmRel(GroupObject &ko);
		void koHandleDimmAbs(GroupObject &ko);
		void koHandleLock(GroupObject & ko);
		void koHandleColor(GroupObject &ko);
		void handleColorHSV(GroupObject &ko);
		void handleColorRGB(GroupObject &ko);
		void handleColorTW(GroupObject &ko);
		void handleColorXYY(GroupObject &ko);
		void koHandleColorRel(GroupObject &ko, ColorChannel channel);
		void koHandleColorAbs(GroupObject &ko, ColorChannel channel);
		void koHandleHclCurve(GroupObject &ko);
		void koHandleScene(GroupObject &ko);

		void handleSwitchNormal(GroupObject &ko);
		void handleSwitchStaircase(GroupObject &ko);
};