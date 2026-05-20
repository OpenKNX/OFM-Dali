#pragma once

#include "Arduino.h"
#include "Dali/Master.h"
#include "Dali/Frame.h"
#include "Dali/Commands.h"
#include <array>

class DaliEVGBase
{
public:
    DaliEVGBase(Dali::Master &master, uint8_t address, uint8_t deviceType, bool isGroup = false,
                uint8_t minLevel = 0, uint8_t maxLevel = 254, uint8_t onLevel = 254,
                uint8_t fadeTime = 0, bool errorState = false, bool startOn = false,
                bool realDevicePresent = false);
    virtual ~DaliEVGBase();

    static void loop(uint32_t nowMs);
    static DaliEVGBase *getByAddress(uint8_t address);

    void attachToMaster();
    bool hasRealDevicePresent() const;
    void setRealDevicePresent(bool present);
    bool handleFrame(const Dali::Frame &frame);

    bool isOn() const;
    uint8_t getLevel() const;
    uint8_t getDeviceType() const;
    bool hasError() const;
    uint8_t getMinLevel() const;
    uint8_t getMaxLevel() const;
    uint8_t getOnLevel() const;
    uint8_t getNightOnLevel() const;
    uint8_t getFadeTime() const;
    uint16_t getGroupBits() const;

    void setErrorState(bool errorState);
    void setOnLevel(uint8_t arcLevel);
    void setOffLevel(uint8_t arcLevel);
    void setNightOnLevel(uint8_t arcLevel);
    void setFadeTime(uint8_t fadeTime);
    void setGroups(uint16_t groupBits);
    bool isMemberOfGroup(uint8_t group) const;
    
    // Time-based fade/transition support
    void update(uint32_t nowMs);
    void startFadeTo(uint8_t targetLevel, uint32_t nowMs);
    bool isFading() const;

protected:
    enum class FrameType { Unknown, Arc, Command, Special };

    struct ParsedFrame {
        FrameType type = FrameType::Unknown;
        bool isGroup = false;
        uint8_t address = 0;
        bool selector = false;
        uint8_t command = 0;
        uint8_t value = 0;
    };

    virtual bool handleDeviceCommand(uint8_t command, uint8_t value, const ParsedFrame &parsed);
    virtual bool handleDeviceExtendedCommand(uint8_t command, uint8_t value);
    virtual bool handleDeviceQuery(uint8_t query, const ParsedFrame &parsed);

    void respond(uint8_t value);
    void clearPendingDeviceType();

    bool matchesFrameTarget(const ParsedFrame &parsed) const;
    ParsedFrame parseFrame(const Dali::Frame &frame) const;
    bool handleArcCommand(uint8_t arcLevel);
    bool handleCommand(uint8_t command, uint8_t parameter, const ParsedFrame &parsed);
    bool handleSpecialCommand(uint8_t specialCommand, uint8_t value);
    bool handleQuery(uint8_t query, const ParsedFrame &parsed);

    static void registerInstance(uint8_t address, DaliEVGBase *instance);
    static void unregisterInstance(uint8_t address, DaliEVGBase *instance);

    static std::array<DaliEVGBase *, MAX_SHORT_ADDRESSES> instances;

    Dali::Master &daliMaster;
    uint8_t address;
    uint8_t deviceType;
    bool isGroupDevice;
    uint8_t minLevel;
    uint8_t maxLevel;
    uint8_t onLevel;
    uint8_t nightOnLevel;
    uint8_t fadeTime;
    bool errorState;
    bool onState;
    uint8_t currentLevel;
    uint8_t lastNonZeroLevel;
    uint16_t groupBits;
    uint8_t pendingDeviceType;
    std::array<uint8_t, 3> dtr;
    bool realDevicePresent;
    // Fade/transition state
    uint8_t fadeTargetLevel;
    bool fadeActive;
    uint32_t fadeLastMs;
    float fadeAccumulator;
    // Note: fadeTime (0..15) maps to a steps/sec table implemented in cpp
};