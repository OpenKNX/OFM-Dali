#pragma once

#include "Dali/Master.h"
#include "Arduino.h"
#include "Dali/Frame.h"
#include "Dali/Commands.h"
#include <array>
#include <vector>

// Interval used for UP/DOWN rate steps (milliseconds)
static constexpr uint32_t UPDOWN_FADE_INTERVAL_MS = 200;

class DaliEVGBase
{
public:
    DaliEVGBase(Dali::Master &master, uint8_t address, uint8_t deviceType, bool isGroup = false,
                uint8_t minLevel = 0, uint8_t maxLevel = 254, uint8_t onLevel = 254,
                uint8_t fadeTime = 0, uint8_t fadeRate = 0, bool errorState = false, bool startOn = false,
                bool realDevicePresent = false);
    virtual ~DaliEVGBase();

    static void loop(uint32_t nowMs);
    static DaliEVGBase *getByAddress(uint8_t address);

    void attachToMaster();
    static void registerMasterMonitor(Dali::Master *master);
    static void handleFrameStatic(const Dali::Frame &frame, Dali::Master *master);
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
    uint8_t getFadeRate() const;
    uint16_t getGroupBits() const;

    void setErrorState(bool errorState);
    void setOnLevel(uint8_t arcLevel);
    void setOffLevel(uint8_t arcLevel);
    void setNightOnLevel(uint8_t arcLevel);
    void setFadeTime(uint8_t fadeTime);
    void setFadeRate(uint8_t fadeRate);
    void setUpdateRate(uint8_t updateRate);
    void setGroups(uint16_t groupBits);
    bool isMemberOfGroup(uint8_t group) const;
    bool isSceneActive(uint8_t scene) const;
    uint8_t getSceneLevel(uint8_t scene) const;
    void setSceneLevel(uint8_t scene, uint8_t level);
    void setSceneActive(uint8_t scene, bool active);
    void setCurrentLevel(uint8_t level);
    
    // Time-based fade/transition support
    void update(uint32_t nowMs);
    void startFadeTo(uint8_t targetLevel, uint32_t nowMs);
    bool isFading() const;
    void debugOutput() const;
    virtual void debugOutputParams() const;
    void debugOutputIfDue(bool force = false);

    enum class FrameType { Unknown, Arc, Command, Special };

protected:

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

    static ParsedFrame parseFrameStatic(const Dali::Frame &frame);

    static void registerInstance(uint8_t address, DaliEVGBase *instance);
    static void unregisterInstance(uint8_t address, DaliEVGBase *instance);

    static constexpr size_t MAX_SHORT_ADDRESSES = 64;
    static std::array<DaliEVGBase *, MAX_SHORT_ADDRESSES> instances;
    static std::vector<Dali::Master *> registeredMasters;

    Dali::Master &daliMaster;
    uint8_t address;
    uint8_t deviceType;
    bool isGroupDevice;
    uint8_t minLevel;
    uint8_t maxLevel;
    uint8_t onLevel;
    uint8_t nightOnLevel;
    uint8_t fadeTime;
    uint8_t fadeRate;
    uint8_t updateRate;
    bool errorState;
    bool onState;
    uint8_t currentLevel;
    uint8_t lastNonZeroLevel;
    uint16_t groupBits;
    uint8_t pendingDeviceType;
    std::array<uint8_t, 3> dtr;
    bool realDevicePresent;
    static constexpr size_t SCENE_COUNT = 16;
    std::array<uint8_t, SCENE_COUNT> sceneLevels;
    uint16_t sceneActiveMask;
    // Fade/transition state
    uint8_t fadeTargetLevel;
    bool fadeActive;
    uint32_t fadeLastMs;
    float fadeAccumulator;
    // Optional override: when >0, update() uses this steps/sec rate
    float fadeStepsPerSecOverride;
    // Debug output throttling state
    mutable uint32_t lastDebugOutputMs;
    // Note: fadeTime (0..15) maps to a steps/sec table implemented in cpp
};