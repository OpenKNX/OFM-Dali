#include "DaliEVGBase.h"
#include "DaliHelper.h"
#include <cstdio>
#include <vector>
#include <algorithm>

static const char *frameTypeName(DaliEVGBase::FrameType type)
{
    switch (type) {
        case DaliEVGBase::FrameType::Unknown: return "Unknown";
        case DaliEVGBase::FrameType::Arc: return "Arc";
        case DaliEVGBase::FrameType::Command: return "Command";
        case DaliEVGBase::FrameType::Special: return "Special";
    }
    return "Invalid";
}

static constexpr uint8_t DALI_BROADCAST_ADDRESS = 0x3F;

static constexpr float FADE_STEPS_PER_SEC[16] = {
    45.3f, 32.0f, 22.6f, 16.0f,
    11.2f, 7.9f, 5.6f, 3.9f,
    2.8f, 2.0f, 1.4f, 1.0f,
    0.7f, 0.5f, 0.35f, 0.25f
};

// Fade duration table (seconds for a full-scale fade) for fadeTime values 0..15
// fadeTime==0 means instant change, so the first entry is 0.0f.
static constexpr float FADE_DURATION_SECONDS[16] = {
    0.0f, 0.7f, 1.0f, 1.4f,
    2.0f, 2.8f, 4.0f, 5.7f,
    8.0f, 11.3f, 16.0f, 22.6f,
    32.0f, 45.3f, 64.0f, 90.5f
};

std::array<DaliEVGBase *, DaliEVGBase::MAX_SHORT_ADDRESSES> DaliEVGBase::instances = {};

DaliEVGBase::DaliEVGBase(Dali::Master &master, uint8_t address, uint8_t deviceType, bool isGroup,
                                                 uint8_t minLevel, uint8_t maxLevel, uint8_t onLevel,
                                                 uint8_t fadeTime, uint8_t fadeRate, bool errorState, bool startOn,
                                                 DaliChannel *realDevicePtr)
    : daliMaster(master),
      address(address),
      deviceType(deviceType),
      isGroupDevice(isGroup),
      minLevel(minLevel),
      maxLevel(maxLevel),
      onLevel(onLevel),
      nightOnLevel(onLevel),
      fadeTime(fadeTime),
      fadeRate(fadeRate),
      updateRate(0),
      errorState(errorState),
      onState(startOn),
      currentLevel(startOn ? onLevel : 0),
      lastNonZeroLevel(startOn ? onLevel : (onLevel > 0 ? onLevel : 1)),
      groupBits(0),
      pendingDeviceType(0),
      dtr{0, 0, 0},
      realDevicePtr(realDevicePtr),
      currentFrameRef(0),
      currentQueryType(0),
      pendingResponseRef(0),
      pendingResponseQuery(0),
      sceneLevels{},
      sceneActiveMask(0),
      fadeTargetLevel(currentLevel),
      fadeActive(false),
      fadeLastMs(0),
      fadeAccumulator(0.0f),
      fadeStepsPerSecOverride(0.0f),
      lastDebugOutputMs(0),
      initDataState(InitDataState::OFF),
      initDataStartMs(0),
      currentSceneQueryIndex(0),
      syncGroupBits(0),
      initDataPendingRef(0),
      initDataPendingQuery(0)
{
    registerInstance(address, this);
}

DaliEVGBase::~DaliEVGBase()
{
    unregisterInstance(address, this);
}

const std::string DaliEVGBase::logPrefix() const
{
    return openknx.logger.buildPrefix("DaliEVGBase", address + 1);
}


void DaliEVGBase::attachToMaster()
{
    // register a single monitor per DALI master that dispatches to registered EVG instances
    registerMasterMonitor(&daliMaster);
}

std::vector<Dali::Master *> DaliEVGBase::registeredMasters = {};

void DaliEVGBase::registerMasterMonitor(Dali::Master *master)
{
    if (master == nullptr) return;
    // already registered?
    if (std::find(registeredMasters.begin(), registeredMasters.end(), master) != registeredMasters.end()) {
        return;
    }

    Dali::Master *masterPtr = master;
    master->registerMonitor([masterPtr](Dali::Frame frame) {
        DaliEVGBase::handleFrameStatic(frame, masterPtr);
    });

    registeredMasters.push_back(masterPtr);
}

DaliEVGBase::ParsedFrame DaliEVGBase::parseFrameStatic(const Dali::Frame &frame)
{
    ParsedFrame parsed;
    if (frame.size != 16) {
        return parsed;
    }

    uint16_t data = (uint16_t)(frame.data & 0xFFFF);
    parsed.isGroup = ((data >> 15) & 0x01) != 0;
    parsed.address = (uint8_t)((data >> 9) & 0x3F);
    parsed.selector = ((data >> 8) & 0x01) != 0;
    parsed.value = (uint8_t)(data & 0xFF);

    if (!parsed.selector) {
        parsed.type = FrameType::Arc;
        parsed.command = parsed.value;
        return parsed;
    }

    if (parsed.address >= 64) {
        parsed.type = FrameType::Special;
        parsed.command = parsed.address;
        parsed.value = (uint8_t)(data & 0xFF);
        return parsed;
    }

    parsed.type = FrameType::Command;
    parsed.command = parsed.value;
    return parsed;
}

void DaliEVGBase::handleFrameStatic(const Dali::Frame &frame, Dali::Master *master)
{
    (void)master; // currently unused but kept for future per-master logic

    printf("DaliEVGBase::handleFrame size=%u flags=0x%02X\n", frame.size, frame.flags);

    if (frame.size == 0) return;
    if (frame.flags & DALI_FRAME_ERROR) return;
    if (frame.size == 8) {
        handleBackwardFrame(frame);
        return;
    }

    ParsedFrame parsed = parseFrameStatic(frame);
    printf("DaliEVGBase::handleFrame parsed frame type=%s addr=%u group=%u sel=%u cmd=0x%02X val=0x%02X\n",
           frameTypeName(parsed.type),
           parsed.address,
           parsed.isGroup ? 1u : 0u,
           parsed.selector ? 1u : 0u,
           parsed.command,
           parsed.value);


    if (parsed.type == FrameType::Unknown) return;

    if (parsed.type == FrameType::Special) {
        // deliver to all instances
        for (auto *inst : instances) {
            if (inst != nullptr) inst->handleSpecialCommand(parsed.command, parsed.value);
        }
        return;
    }

    // For non-special frames: route based on address/group
    if (parsed.isGroup) {
        if (parsed.address == DALI_BROADCAST_ADDRESS) {
            // broadcast to all
            for (auto *inst : instances) {
                if (inst == nullptr) continue;
                inst->currentFrameRef = frame.ref;
                inst->currentQueryType = (parsed.type == FrameType::Command) ? parsed.command : 0;
                if (parsed.type == FrameType::Arc) {
                    inst->startFadeTo(parsed.value, 0);
                    inst->currentFrameRef = 0;
                    inst->currentQueryType = 0;
                    continue;
                }
                if (inst->pendingDeviceType != 0) {
                    bool handled = inst->handleDeviceExtendedCommand(parsed.command, parsed.value);
                    inst->clearPendingDeviceType();
                    inst->currentFrameRef = 0;
                    inst->currentQueryType = 0;
                    if (handled) continue;
                }
                inst->handleCommand(parsed.command, parsed.value, parsed);
                inst->currentFrameRef = 0;
                inst->currentQueryType = 0;
            }
            return;
        }

        // group address (0..15)
        if (parsed.address < 16) {
            for (auto *inst : instances) {
                if (inst == nullptr) continue;
                if (!inst->isMemberOfGroup(parsed.address)) continue;
                inst->currentFrameRef = frame.ref;
                inst->currentQueryType = (parsed.type == FrameType::Command) ? parsed.command : 0;
                if (parsed.type == FrameType::Arc) {
                    inst->startFadeTo(parsed.value, 0);
                    inst->currentFrameRef = 0;
                    inst->currentQueryType = 0;
                    continue;
                }
                if (inst->pendingDeviceType != 0) {
                    bool handled = inst->handleDeviceExtendedCommand(parsed.command, parsed.value);
                    inst->clearPendingDeviceType();
                    inst->currentFrameRef = 0;
                    inst->currentQueryType = 0;
                    if (handled) continue;
                }
                inst->handleCommand(parsed.command, parsed.value, parsed);
                inst->currentFrameRef = 0;
                inst->currentQueryType = 0;
            }
            return;
        }
    }

    // individual short address or broadcast
    if (parsed.address < instances.size()) {
        DaliEVGBase *inst = instances[parsed.address];
        if (inst != nullptr) {
            inst->currentFrameRef = frame.ref;
            inst->currentQueryType = (parsed.type == FrameType::Command) ? parsed.command : 0;
            if (parsed.type == FrameType::Arc) {
                inst->startFadeTo(parsed.value, 0);
                inst->currentFrameRef = 0;
                inst->currentQueryType = 0;
                return;
            }
            if (inst->pendingDeviceType != 0) {
                bool handled = inst->handleDeviceExtendedCommand(parsed.command, parsed.value);
                inst->clearPendingDeviceType();
                inst->currentFrameRef = 0;
                inst->currentQueryType = 0;
                if (handled) return;
            }
            inst->handleCommand(parsed.command, parsed.value, parsed);
            inst->currentFrameRef = 0;
            inst->currentQueryType = 0;
            return;
        }
    }

    // If we reach here and address was broadcast, deliver to all as fallback
    if (parsed.address == DALI_BROADCAST_ADDRESS) {
        for (auto *inst : instances) {
            if (inst == nullptr) continue;
            inst->currentFrameRef = frame.ref;
            inst->currentQueryType = (parsed.type == FrameType::Command) ? parsed.command : 0;
            if (parsed.type == FrameType::Arc) {
                inst->startFadeTo(parsed.value, 0);
                inst->currentFrameRef = 0;
                inst->currentQueryType = 0;
                continue;
            }
            if (inst->pendingDeviceType != 0) {
                bool handled = inst->handleDeviceExtendedCommand(parsed.command, parsed.value);
                inst->clearPendingDeviceType();
                inst->currentFrameRef = 0;
                inst->currentQueryType = 0;
                if (handled) continue;
            }
            inst->handleCommand(parsed.command, parsed.value, parsed);
            inst->currentFrameRef = 0;
            inst->currentQueryType = 0;
        }
    }
}

void DaliEVGBase::handleBackwardFrame(const Dali::Frame &frame)
{
    uint8_t response = static_cast<uint8_t>(frame.data & 0xFF);
    if (frame.ref != 0) {
        for (auto *inst : instances) {
            if (inst == nullptr) continue;
            if (inst->pendingResponseQuery == 0) continue;
            if (inst->pendingResponseRef != frame.ref) continue;
            inst->processRealDeviceResponse(response);
            return;
        }
        return;
    }

    DaliEVGBase *pendingInst = nullptr;
    int pendingCount = 0;
    for (auto *inst : instances) {
        if (inst == nullptr) continue;
        if (inst->pendingResponseQuery == 0) continue;
        if (inst->pendingResponseRef != 0) continue;
        pendingInst = inst;
        pendingCount++;
        if (pendingCount > 1) break;
    }
    if (pendingCount == 1 && pendingInst != nullptr) {
        pendingInst->processRealDeviceResponse(response);
    }
}

void DaliEVGBase::loop(uint32_t nowMs)
{
    for (auto *instance : instances) {
        if (instance != nullptr) {
            instance->update(nowMs);
            instance->loopInitData();
        }
    }
}

void DaliEVGBase::awaitRealDeviceResponse(uint8_t query, unsigned long ref)
{
    pendingResponseQuery = query;
    pendingResponseRef = ref;
}

void DaliEVGBase::clearPendingRealDeviceResponse()
{
    pendingResponseQuery = 0;
    pendingResponseRef = 0;
}

void DaliEVGBase::processRealDeviceResponse(uint8_t response)
{
    printf("DaliEVGBase[%u] real device response query=0x%02X response=0x%02X\n", address, pendingResponseQuery, response);
    switch (pendingResponseQuery) {
        case static_cast<uint8_t>(Dali::Command::QUERY_STATUS): {
            errorState = (response & 0x20) != 0;
            bool newOnState = (response & 0x10) != 0;
            if (newOnState && currentLevel == 0) {
                currentLevel = lastNonZeroLevel;
            }
            onState = newOnState;
            if (!onState) {
                currentLevel = 0;
            }
            debugOutputIfDue(true);
            break;
        }
        case static_cast<uint8_t>(Dali::Command::QUERY_ACTUAL_LEVEL): {
            currentLevel = response;
            onState = currentLevel > 0;
            if (onState) lastNonZeroLevel = currentLevel;
            debugOutputIfDue(true);
            break;
        }
        case static_cast<uint8_t>(Dali::Command::QUERY_MAX_LEVEL):
            maxLevel = response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_MIN_LEVEL):
            minLevel = response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_POWER_ON_LEVEL):
            onLevel = response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_0_7):
            groupBits = (groupBits & 0xFF00) | response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_8_15):
            groupBits = (groupBits & 0x00FF) | (static_cast<uint16_t>(response) << 8);
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_DTR):
            dtr[0] = response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_DEVICE_TYPE):
            deviceType = response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_DTR1):
            dtr[1] = response;
            break;
        case static_cast<uint8_t>(Dali::Command::QUERY_DTR2):
            dtr[2] = response;
            break;
        default:
            if (pendingResponseQuery >= static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL)
                && pendingResponseQuery <= static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL) + 15) {
                uint8_t scene = pendingResponseQuery - static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL);
                if (scene < sceneLevels.size()) {
                    sceneLevels[scene] = response;
                }
            }
            break;
    }
    clearPendingRealDeviceResponse();
}

DaliEVGBase *DaliEVGBase::getByAddress(uint8_t address)
{
    return address < instances.size() ? instances[address] : nullptr;
}

void DaliEVGBase::registerInstance(uint8_t address, DaliEVGBase *instance)
{
    if (address < instances.size()) {
        instances[address] = instance;
    }
}

void DaliEVGBase::unregisterInstance(uint8_t address, DaliEVGBase *instance)
{
    if (address < instances.size() && instances[address] == instance) {
        instances[address] = nullptr;
    }
}


bool DaliEVGBase::isOn() const
{
    return onState;
}

uint8_t DaliEVGBase::getLevel() const
{
    return currentLevel;
}

uint8_t DaliEVGBase::getDeviceType() const
{
    return deviceType;
}

bool DaliEVGBase::hasError() const
{
    return errorState;
}

uint8_t DaliEVGBase::getMinLevel() const
{
    return minLevel;
}

uint8_t DaliEVGBase::getMaxLevel() const
{
    return maxLevel;
}

uint8_t DaliEVGBase::getOnLevel() const
{
    return onLevel;
}

uint8_t DaliEVGBase::getNightOnLevel() const
{
    return nightOnLevel;
}

uint8_t DaliEVGBase::getFadeTime() const
{
    return fadeTime;
}

uint8_t DaliEVGBase::getFadeRate() const
{
    return fadeRate;
}

uint16_t DaliEVGBase::getGroupBits() const
{
    return groupBits;
}

void DaliEVGBase::debugOutput() const
{
    printf("DaliEVGBase addr=%u currentValue=%u onState=%u\n",
           address, currentLevel, onState ? 1u : 0u);
}

uint16_t DaliEVGBase::calcKoNumber(int asap)
{
    return asap + (DGW_KoBlockSize * address) + DGW_KoOffset;
}

void DaliEVGBase::debugOutputIfDue(bool force)
{
    if (!force) {
        if (updateRate == 0) {
            return;
        }
        uint32_t intervalMs = static_cast<uint32_t>(updateRate) * 100u;
        uint32_t nowMs = static_cast<uint32_t>(millis());
        if (nowMs - lastDebugOutputMs < intervalMs) {
            return;
        }
        lastDebugOutputMs = nowMs;
    } else {
        lastDebugOutputMs = static_cast<uint32_t>(millis());
    }

    if (realDevicePtr != nullptr) {
        realDevicePtr->sendSwitchState(onState);
        realDevicePtr->sendDimmState(currentLevel);
    }
    // float perc = DaliHelper::arcToPercentFloat(currentLevel);

    // GroupObject& ko = knx.getGroupObject(calcKoNumber(DGW_Kodimm_state));
    // if(ko.valueNoSendCompare(perc, Dpt(5, 1)))
    // {
    //     logDebugP("SetDimmState %.1f/%i", perc, currentLevel);
    //     ko.objectWritten();
    // }

    // ko = knx.getGroupObject(calcKoNumber(DGW_Koswitch_state));
    // bool currentState = ko.value(DPT_Switch);
    // if (onState == currentState && ko.initialized())
    //     return;
    // ko.value(onState, DPT_Switch);

    // printf("DaliEVGBase addr=%u currentValue=%u onState=%u\n",
    //        address, currentLevel, onState ? 1u : 0u);
}

void DaliEVGBase::debugOutputParams() const
{
    printf("DaliEVGBase addr=%u type=%u isGroup=%u min=%u max=%u onLevel=%u nightOnLevel=%u fadeTime=%u fadeRate=%u updateRate=%u errorState=%u onState=%u currentValue=%u lastNonZeroLevel=%u groupBits=0x%04X sceneMask=0x%04X realDevicePresent=%u dtr=[%u,%u,%u]\n",
           address,
           deviceType,
           isGroupDevice ? 1u : 0u,
           minLevel,
           maxLevel,
           onLevel,
           nightOnLevel,
           fadeTime,
           fadeRate,
           updateRate,
           errorState ? 1u : 0u,
           onState ? 1u : 0u,
           currentLevel,
           lastNonZeroLevel,
           (unsigned)groupBits,
           (unsigned)sceneActiveMask,
           realDevicePtr ? 1u : 0u,
           dtr[0], dtr[1], dtr[2]);
}

void DaliEVGBase::setErrorState(bool error)
{
    errorState = error;
}

void DaliEVGBase::setOnLevel(uint8_t arcLevel)
{
    onLevel = arcLevel;
    if (onState && currentLevel > 0) {
        currentLevel = arcLevel;
        debugOutputIfDue(true);
    }
}

void DaliEVGBase::setOffLevel(uint8_t arcLevel)
{
    minLevel = arcLevel;
    if (!onState) {
        currentLevel = arcLevel;
        debugOutputIfDue(true);
    }
}

void DaliEVGBase::setNightOnLevel(uint8_t arcLevel)
{
    nightOnLevel = arcLevel;
}

void DaliEVGBase::setFadeTime(uint8_t fadeTimeValue)
{
    fadeTime = fadeTimeValue;
}

void DaliEVGBase::setFadeRate(uint8_t fadeRateValue)
{
    fadeRate = fadeRateValue;
}

void DaliEVGBase::setUpdateRate(uint8_t updateRateValue)
{
    updateRate = updateRateValue;
}

void DaliEVGBase::setGroups(uint16_t groupBitsValue)
{
    groupBits = groupBitsValue;
}

bool DaliEVGBase::isSceneActive(uint8_t scene) const
{
    return scene < SCENE_COUNT && ((sceneActiveMask >> scene) & 1u) != 0;
}

uint8_t DaliEVGBase::getSceneLevel(uint8_t scene) const
{
    return scene < SCENE_COUNT ? sceneLevels[scene] : 0;
}

void DaliEVGBase::setSceneLevel(uint8_t scene, uint8_t level)
{
    if (scene < SCENE_COUNT) {
        sceneLevels[scene] = level;
    }
}

void DaliEVGBase::setSceneActive(uint8_t scene, bool active)
{
    if (scene >= SCENE_COUNT) {
        return;
    }
    if (active) {
        sceneActiveMask |= (1u << scene);
    } else {
        sceneActiveMask &= ~(1u << scene);
    }
}

void DaliEVGBase::setCurrentLevel(uint8_t level)
{
    currentLevel = level;
    onState = currentLevel > 0;
    if (onState) {
        lastNonZeroLevel = currentLevel;
    }
}

void DaliEVGBase::startFadeTo(uint8_t targetLevel, uint32_t nowMs)
{
    printf("DaliEVGBase[%u].startFadeTo: called with targetLevel=%u nowMs=%u\n", address, targetLevel, nowMs);
    // Clear any rate-based override when starting a normal fade
    fadeStepsPerSecOverride = 0.0f;
    fadeTargetLevel = targetLevel;
    if (fadeTargetLevel == currentLevel) {
        fadeActive = false;
        return;
    }

    if (fadeTime == 0) {
        // fadeTime 0 means instant change
        currentLevel = fadeTargetLevel;
        fadeActive = false;
        onState = currentLevel > 0;
        if (onState) {
            lastNonZeroLevel = currentLevel;
        }
        debugOutputIfDue(true);
        return;
    }

    fadeActive = true;
    // If nowMs == 0 the caller didn't provide a timestamp; mark last time as 0
    fadeLastMs = nowMs;
    fadeAccumulator = 0.0f;
    printf("DaliEVGBase[%u].startFadeTo: started fade to %u for %02.1f\n", address, targetLevel, FADE_DURATION_SECONDS[fadeTime]);
}

bool DaliEVGBase::isFading() const
{
    return fadeActive;
}

void DaliEVGBase::update(uint32_t nowMs)
{
    if (!fadeActive) return;

    // If fadeLastMs is 0, initialize it to nowMs to avoid huge elapsed
    if (fadeLastMs == 0) {
        fadeLastMs = nowMs;
        return;
    }

    uint32_t elapsedMs = nowMs - fadeLastMs;
    if (elapsedMs == 0) return;

    float durationSeconds = 1.0f;
    if (fadeTime < 16) {
        durationSeconds = FADE_DURATION_SECONDS[fadeTime];
    }

    // Convert fadeTime duration to an effective level step rate unless overridden
    // by a rate-based fade (e.g. UP/DOWN). Use full-scale range 0..254 for the fade duration.
    float stepsPerSec = 0.0f;
    if (fadeStepsPerSecOverride > 0.0f) {
        stepsPerSec = fadeStepsPerSecOverride;
    } else {
        stepsPerSec = (durationSeconds > 0.0f) ? (254.0f / durationSeconds) : 254.0f;
    }
    float steps = stepsPerSec * (elapsedMs / 1000.0f);
    fadeAccumulator += steps;
    int32_t stepCount = (int32_t)floor(fadeAccumulator);
    if (stepCount == 0) {
        // not enough accumulated steps yet
        fadeLastMs = nowMs;
        return;
    }

    // consume steps
    fadeAccumulator -= (float)stepCount;
    fadeLastMs = nowMs;

    uint8_t previousLevel = currentLevel;
    bool previousOnState = onState;

    // Determine direction
    if (fadeTargetLevel > currentLevel) {
        uint32_t move = (uint32_t)stepCount;
        uint32_t diff = (uint32_t)fadeTargetLevel - currentLevel;
        if (move >= diff) {
            currentLevel = fadeTargetLevel;
            fadeActive = false;
            // clear override when fade completes
            fadeStepsPerSecOverride = 0.0f;
        } else {
            currentLevel += (uint8_t)move;
        }
    } else if (fadeTargetLevel < currentLevel) {
        uint32_t move = (uint32_t)stepCount;
        uint32_t diff = (uint32_t)currentLevel - fadeTargetLevel;
        if (move >= diff) {
            currentLevel = fadeTargetLevel;
            fadeActive = false;
            // clear override when fade completes
            fadeStepsPerSecOverride = 0.0f;
        } else {
            currentLevel -= (uint8_t)move;
        }
    } else {
        fadeActive = false;
    }

    onState = currentLevel > 0;
    if (onState) lastNonZeroLevel = currentLevel;

    if (currentLevel != previousLevel || onState != previousOnState) {
        debugOutputIfDue(!fadeActive);
    }
}

bool DaliEVGBase::isMemberOfGroup(uint8_t group) const
{
    if (group >= 16) {
        return false;
    }
    return (groupBits & (1u << group)) != 0;
}

void DaliEVGBase::clearPendingDeviceType()
{
    pendingDeviceType = 0;
}

bool DaliEVGBase::hasRealDevicePresent() const
{
    return realDevicePtr != nullptr;
}

void DaliEVGBase::setRealDevicePtr(DaliChannel *realDevicePtr)
{
    this->realDevicePtr = realDevicePtr;
}

bool DaliEVGBase::matchesFrameTarget(const ParsedFrame &parsed) const
{
    if (parsed.type == FrameType::Special) {
        return parsed.command == static_cast<uint8_t>(Dali::SpecialCommand::ENABLE_DT)
            || parsed.command == static_cast<uint8_t>(Dali::SpecialCommand::SET_DTR)
            || parsed.command == static_cast<uint8_t>(Dali::SpecialCommand::SET_DTR1)
            || parsed.command == static_cast<uint8_t>(Dali::SpecialCommand::SET_DTR2);
    }

    if (parsed.isGroup) {
        if (parsed.address == DALI_BROADCAST_ADDRESS) {
            return true;
        }
        return (parsed.address < 16) && isMemberOfGroup(parsed.address);
    }

    return parsed.address == address || parsed.address == DALI_BROADCAST_ADDRESS;
}

DaliEVGBase::ParsedFrame DaliEVGBase::parseFrame(const Dali::Frame &frame) const
{
    ParsedFrame parsed;

    if (frame.size != 16) {
        return parsed;
    }

    uint16_t data = (uint16_t)(frame.data & 0xFFFF);
    parsed.isGroup = ((data >> 15) & 0x01) != 0;
    parsed.address = (uint8_t)((data >> 9) & 0x3F);
    parsed.selector = ((data >> 8) & 0x01) != 0;
    parsed.value = (uint8_t)(data & 0xFF);

    if (!parsed.selector) {
        parsed.type = FrameType::Arc;
        parsed.command = parsed.value;
        return parsed;
    }

    if (parsed.address >= 32) {
        parsed.type = FrameType::Special;
        parsed.command = parsed.address;
        parsed.value = (uint8_t)(data & 0xFF);
        return parsed;
    }

    parsed.type = FrameType::Command;
    parsed.command = parsed.value;
    return parsed;
}

bool DaliEVGBase::handleArcCommand(uint8_t arcLevel)
{
    printf("DaliEVGBase[%u] handleArcCommand arcLevel=0x%02X\n", address, arcLevel);
    // keep backward compatibility: start fade with now=0 (caller should call update with real time)
    startFadeTo(arcLevel, 0);
    return true;
}

bool DaliEVGBase::handleCommand(uint8_t command, uint8_t parameter, const ParsedFrame &parsed)
{
    printf("DaliEVGBase[%u] handleCommand command=0x%02X parameter=0x%02X frameType=%s\n",
           address,
           command,
           parameter,
           frameTypeName(parsed.type));
    if (command == static_cast<uint8_t>(Dali::Command::OFF)) {
        // start fade to 0
        startFadeTo(0, 0);
        return true;
    }
    if (command == static_cast<uint8_t>(Dali::Command::RECALL_MAX)) {
        startFadeTo(maxLevel, 0);
        lastNonZeroLevel = maxLevel > 0 ? maxLevel : onLevel;
        return true;
    }
    if (command == static_cast<uint8_t>(Dali::Command::RECALL_MIN)) {
        startFadeTo(minLevel, 0);
        return true;
    }
    if (command == static_cast<uint8_t>(Dali::Command::GO_TO_LAST)) {
        startFadeTo(lastNonZeroLevel, 0);
        return true;
    }
    if (command == static_cast<uint8_t>(Dali::Command::ON_AND_STEP_UP)) {
        if (!onState) {
            currentLevel = onLevel;
            onState = true;
        }
        if (currentLevel + 10 > maxLevel) {
            currentLevel = maxLevel;
        } else {
            currentLevel += 10;
        }
        lastNonZeroLevel = currentLevel;
        return true;
    }
    if (command == static_cast<uint8_t>(Dali::Command::UP)) {
        // Start/continue a rate-based fade for 200 ms using fadeRate mapping
        float stepsPerSec = FADE_STEPS_PER_SEC[0];
        if (fadeRate < 16) stepsPerSec = FADE_STEPS_PER_SEC[fadeRate];
        // amount to move during 200 ms
        int delta = (int)ceil(stepsPerSec * (UPDOWN_FADE_INTERVAL_MS * 0.001f));
        if (delta < 1) delta = 1;
        uint8_t target = currentLevel + (uint8_t)delta;
        if (target > maxLevel) target = maxLevel;
        if (target != currentLevel) {
            fadeTargetLevel = target;
            fadeActive = true;
            fadeStepsPerSecOverride = stepsPerSec;
            // initialize timing for update()
            fadeLastMs = 0;
            fadeAccumulator = 0.0f;
        }
        onState = currentLevel > 0;
        if (onState) lastNonZeroLevel = currentLevel;
        return true;
    }
    if (command == static_cast<uint8_t>(Dali::Command::DOWN)) {
        // Start/continue a rate-based fade for 200 ms using fadeRate mapping
        float stepsPerSec = FADE_STEPS_PER_SEC[0];
        if (fadeRate < 16) stepsPerSec = FADE_STEPS_PER_SEC[fadeRate];
        int delta = (int)ceil(stepsPerSec * (UPDOWN_FADE_INTERVAL_MS * 0.001f));
        if (delta < 1) delta = 1;
        uint8_t target = (currentLevel <= (uint8_t)delta) ? 0 : (currentLevel - (uint8_t)delta);
        if (target != currentLevel) {
            fadeTargetLevel = target;
            fadeActive = true;
            fadeStepsPerSecOverride = stepsPerSec;
            fadeLastMs = 0;
            fadeAccumulator = 0.0f;
        }
        if (target == 0) onState = false;
        return true;
    }
    if (command >= static_cast<uint8_t>(Dali::Command::DTR_AS_SCENE)
        && command <= static_cast<uint8_t>(Dali::Command::DTR_AS_SCENE) + 15) {
        uint8_t scene = command - static_cast<uint8_t>(Dali::Command::DTR_AS_SCENE);
        setSceneLevel(scene, dtr[0]);
        setSceneActive(scene, true);
        printf("DaliEVGBase[%u] setScene %u level=0x%02X\n", address, scene, getSceneLevel(scene));
        return true;
    }

    if (command >= static_cast<uint8_t>(Dali::Command::REMOVE_FROM_SCENE)
        && command <= static_cast<uint8_t>(Dali::Command::REMOVE_FROM_SCENE) + 15) {
        uint8_t scene = command - static_cast<uint8_t>(Dali::Command::REMOVE_FROM_SCENE);
        setSceneActive(scene, false);
        printf("DaliEVGBase[%u] removeScene %u\n", address, scene);
        return true;
    }

    if (command >= static_cast<uint8_t>(Dali::Command::GO_TO_SCENE)
        && command <= static_cast<uint8_t>(Dali::Command::GO_TO_SCENE) + 15) {
        uint8_t scene = command - static_cast<uint8_t>(Dali::Command::GO_TO_SCENE);
        if (!isSceneActive(scene)) {
            return false;
        }
        uint8_t sceneLevel = getSceneLevel(scene);
        startFadeTo(sceneLevel, 0);
        if (sceneLevel > 0) {
            lastNonZeroLevel = sceneLevel;
        }
        return true;
    }

    if (parameter == static_cast<uint8_t>(Dali::Command::QUERY_DEVICE_TYPE)) {
        respond(deviceType);
        return true;
    }

    if (handleQuery(command, parsed)) {
        return true;
    }

    if (handleDeviceCommand(command, parameter, parsed)) {
        return true;
    }

    return false;
}

bool DaliEVGBase::handleDeviceCommand(uint8_t command, uint8_t value, const ParsedFrame &parsed)
{
    printf("DaliEVGBase[%u] handleDeviceCommand command=0x%02X value=0x%02X\n", address, command, value);
    // Default device-specific commands are not supported in the base class.
    return false;
}

bool DaliEVGBase::handleSpecialCommand(uint8_t specialCommand, uint8_t value)
{
    printf("DaliEVGBase[%u] handleSpecialCommand special=0x%02X value=0x%02X\n", address, specialCommand, value);
    if (specialCommand == static_cast<uint8_t>(Dali::SpecialCommand::ENABLE_DT)) {
        pendingDeviceType = value;
        return true;
    }

    if (specialCommand == static_cast<uint8_t>(Dali::SpecialCommand::SET_DTR)) {
        dtr[0] = value;
        return true;
    }

    if (specialCommand == static_cast<uint8_t>(Dali::SpecialCommand::SET_DTR1)) {
        dtr[1] = value;
        return true;
    }

    if (specialCommand == static_cast<uint8_t>(Dali::SpecialCommand::SET_DTR2)) {
        dtr[2] = value;
        return true;
    }

    return false;
}

bool DaliEVGBase::handleQuery(uint8_t query, const ParsedFrame &parsed)
{
    printf("DaliEVGBase[%u] handleQuery query=0x%02X\n", address, query);
    switch (query) {
        case static_cast<uint8_t>(Dali::Command::QUERY_STATUS): {
            uint8_t status = 0;
            if (errorState) {
                status |= 0x20;
            }
            if (onState) {
                status |= 0x10;
            }
            respond(status);
            return true;
        }
        case static_cast<uint8_t>(Dali::Command::QUERY_ACTUAL_LEVEL):
            respond(currentLevel);
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_MAX_LEVEL):
            respond(maxLevel);
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_MIN_LEVEL):
            respond(minLevel);
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_POWER_ON_LEVEL):
            respond(onLevel);
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_0_7):
            respond((uint8_t)(groupBits & 0xFF));
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_8_15):
            respond((uint8_t)((groupBits >> 8) & 0xFF));
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_DTR):
            respond(dtr[0]);
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_DTR1):
            respond(dtr[1]);
            return true;
        case static_cast<uint8_t>(Dali::Command::QUERY_DTR2):
            respond(dtr[2]);
            return true;
        default:
            break;
    }

    if (query >= static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL)
        && query <= static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL) + 15) {
        uint8_t scene = query - static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL);
        respond(getSceneLevel(scene));
        return true;
    }

    if (handleDeviceQuery(query, parsed)) {
        return true;
    }

    return false;
}

bool DaliEVGBase::handleDeviceExtendedCommand(uint8_t command, uint8_t value)
{
    printf("DaliEVGBase[%u] handleDeviceExtendedCommand command=0x%02X value=0x%02X\n", address, command, value);
    // Extended device commands are not handled by the generic base implementation.
    return false;
}

bool DaliEVGBase::handleDeviceQuery(uint8_t query, const ParsedFrame &parsed)
{
    printf("DaliEVGBase[%u] handleDeviceQuery query=0x%02X\n", address, query);
    return false;
}

void DaliEVGBase::respond(uint8_t value)
{
    if (realDevicePtr != nullptr) {
        if (currentQueryType != 0) {
            awaitRealDeviceResponse(currentQueryType, currentFrameRef);
        }
        return;
    }

    Dali::Frame response;
    response.data = value;
    response.size = 8;
    response.flags = DALI_FRAME_BACKWARD;
    daliMaster.sendRaw(response);
}

void DaliEVGBase::startSyncWithRealDevice()
{
    if (realDevicePtr == nullptr) return;
    
    initDataState = InitDataState::QUERY_MIN_LEVEL;
    initDataStartMs = millis() + 10000;
    initDataNextMs = initDataStartMs;
    currentSceneQueryIndex = 0;
    syncGroupBits = 0;
    printf("DaliEVGBase[%u] starting synchronization with real device\n", address);
}

bool DaliEVGBase::isSyncInProgress() const
{
    return initDataState != InitDataState::OFF && initDataState != InitDataState::DONE;
}

void DaliEVGBase::loopInitData()
{
    if (realDevicePtr == nullptr) return;
    if (initDataState == InitDataState::OFF || initDataState == InitDataState::DONE) return;
    
    uint32_t nowMs = millis();

    if (nowMs - initDataStartMs > INIT_TIMEOUT_MS) {
        printf("DaliEVGBase[%u] sync timeout\n", address);
        initDataState = InitDataState::DONE;
        initDataPendingRef = 0;
        initDataPendingQuery = 0;
        return;
    }
    
    // Step 1: Check if there's a pending response waiting
    if (initDataPendingRef != 0) {
        Dali::Response resp = daliMaster.getResponse(initDataPendingRef);
        
        if (resp.state == Dali::ResponseState::WAITING || resp.state == Dali::ResponseState::SENT) {
            // Response not ready yet, try again next time
            return;
        }
        
        if (resp.state == Dali::ResponseState::NO_ANSWER || resp.state == Dali::ResponseState::NOT_REGISTERED) {
            printf("DaliEVGBase[%u] sync error: no response for query 0x%02X\n", address, initDataPendingQuery);
            initDataState = InitDataState::DONE;
            initDataPendingRef = 0;
            initDataPendingQuery = 0;
            return;
        }
        
        if (resp.state == Dali::ResponseState::RECEIVED) {
            uint8_t responseValue = resp.frame.data & 0xFF;
            printf("DaliEVGBase[%u] sync received response query=0x%02X value=0x%02X\n", address, initDataPendingQuery, responseValue);
            
            // Process the response and transition to next state
            if (handleInitDataResponse(initDataPendingQuery, responseValue)) {
                initDataPendingRef = 0;
                initDataPendingQuery = 0;
            } else {
                // Error in response handling
                initDataState = InitDataState::DONE;
                initDataPendingRef = 0;
                initDataPendingQuery = 0;
            }
            return;
        }
        
        return;
    }
    
    if (nowMs < initDataNextMs) {
        return;
    }
    initDataNextMs = nowMs + 200;

    // Step 2: No pending response, send the next command for this state
    uint8_t queryCmd = 0;
    
    switch (initDataState) {
        case InitDataState::QUERY_MIN_LEVEL:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_MIN_LEVEL);
            break;
        case InitDataState::QUERY_MAX_LEVEL:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_MAX_LEVEL);
            break;
        case InitDataState::QUERY_POWER_ON_LEVEL:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_POWER_ON_LEVEL);
            break;
        case InitDataState::QUERY_ACTUAL_LEVEL:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_ACTUAL_LEVEL);
            break;
        case InitDataState::QUERY_STATUS:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_STATUS);
            break;
        case InitDataState::QUERY_GROUPS_0_7:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_0_7);
            break;
        case InitDataState::QUERY_GROUPS_8_15:
            queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_8_15);
            break;
        case InitDataState::QUERY_SCENE_LEVELS: {
            if (currentSceneQueryIndex < SCENE_COUNT) {
                queryCmd = static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL) + currentSceneQueryIndex;
            } else {
                // All scene levels retrieved
                initDataState = InitDataState::DONE;
                printf("DaliEVGBase[%u] sync complete, all parameters retrieved\n", address);
                return;
            }
            break;
        }
        case InitDataState::OFF:
        case InitDataState::DONE:
            return;
    }
    
    // Send the command and store the pending ref/query
    initDataPendingRef = daliMaster.sendCommand(address, queryCmd, false, true);
    initDataPendingQuery = queryCmd;
    printf("DaliEVGBase[%u] sync sending query 0x%02X (ref=%lu)\n", address, queryCmd, initDataPendingRef);
}

bool DaliEVGBase::handleInitDataResponse(uint8_t queryCommand, uint8_t response)
{
    printf("DaliEVGBase[%u] sync processing query 0x%02X response=0x%02X\n", address, queryCommand, response);
    
    switch (queryCommand) {
        case static_cast<uint8_t>(Dali::Command::QUERY_MIN_LEVEL):
            minLevel = response;
            initDataState = InitDataState::QUERY_MAX_LEVEL;
            return true;
            
        case static_cast<uint8_t>(Dali::Command::QUERY_MAX_LEVEL):
            maxLevel = response;
            initDataState = InitDataState::QUERY_POWER_ON_LEVEL;
            return true;
            
        case static_cast<uint8_t>(Dali::Command::QUERY_POWER_ON_LEVEL):
            onLevel = response;
            nightOnLevel = onLevel;
            initDataState = InitDataState::QUERY_ACTUAL_LEVEL;
            return true;
            
        case static_cast<uint8_t>(Dali::Command::QUERY_ACTUAL_LEVEL):
            currentLevel = response;
            onState = currentLevel > 0;
            if (onState) lastNonZeroLevel = currentLevel;
            initDataState = InitDataState::QUERY_STATUS;
            return true;
            
        case static_cast<uint8_t>(Dali::Command::QUERY_STATUS): {
            uint8_t status = response;
            errorState = (status & 0x20) != 0;
            bool newOnState = (status & 0x10) != 0;
            onState = newOnState;
            if (!onState) currentLevel = 0;
            initDataState = InitDataState::QUERY_GROUPS_0_7;
            return true;
        }
            
        case static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_0_7):
            syncGroupBits = response & 0xFF;
            initDataState = InitDataState::QUERY_GROUPS_8_15;
            return true;
            
        case static_cast<uint8_t>(Dali::Command::QUERY_GROUPS_8_15):
            syncGroupBits |= (static_cast<uint16_t>(response & 0xFF) << 8);
            groupBits = syncGroupBits;
            initDataState = InitDataState::QUERY_SCENE_LEVELS;
            currentSceneQueryIndex = 0;
            return true;
            
        default:
            // Check if it's a scene level query
            if (queryCommand >= static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL)
                && queryCommand <= static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL) + 15) {
                uint8_t sceneIdx = queryCommand - static_cast<uint8_t>(Dali::Command::QUERY_SCENE_LEVEL);
                if (sceneIdx < SCENE_COUNT) {
                    sceneLevels[sceneIdx] = response;
                    currentSceneQueryIndex++;
                    // Stay in QUERY_SCENE_LEVELS state, loopInitData will send next scene query
                    return true;
                }
            }
            break;
    }
    
    return false;
}
