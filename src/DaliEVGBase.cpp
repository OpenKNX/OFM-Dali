#include "DaliEVGBase.h"

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
                                                 bool realDevicePresent)
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
      errorState(errorState),
      onState(startOn),
      currentLevel(startOn ? onLevel : 0),
      lastNonZeroLevel(startOn ? onLevel : (onLevel > 0 ? onLevel : 1)),
      groupBits(0),
      pendingDeviceType(0),
      dtr{0, 0, 0},
      realDevicePresent(realDevicePresent),
      fadeTargetLevel(currentLevel),
      fadeActive(false),
      fadeLastMs(0),
    fadeAccumulator(0.0f),
    fadeStepsPerSecOverride(0.0f)
{
    registerInstance(address, this);
}

DaliEVGBase::~DaliEVGBase()
{
    unregisterInstance(address, this);
}

void DaliEVGBase::attachToMaster()
{
    daliMaster.registerMonitor([this](Dali::Frame frame) { this->handleFrame(frame); });
}

void DaliEVGBase::loop(uint32_t nowMs)
{
    for (auto *instance : instances) {
        if (instance != nullptr) {
            instance->update(nowMs);
        }
    }
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

bool DaliEVGBase::handleFrame(const Dali::Frame &frame)
{
    if (frame.size == 0) {
        return false;
    }

    if (frame.flags & (DALI_FRAME_ECHO | DALI_FRAME_BACKWARD | DALI_FRAME_COLLISION | DALI_FRAME_ERROR)) {
        return false;
    }

    ParsedFrame parsed = parseFrame(frame);
    if (parsed.type == FrameType::Unknown) {
        return false;
    }

    if (parsed.type == FrameType::Special) {
        return handleSpecialCommand(parsed.command, parsed.value);
    }

    if (!matchesFrameTarget(parsed)) {
        return false;
    }

    if (parsed.type == FrameType::Arc) {
        // ARC telegrams should start a fade towards the ARC level
        startFadeTo(parsed.value, 0);
        return true;
    }

    if (pendingDeviceType != 0) {
        bool handled = handleDeviceExtendedCommand(parsed.command, parsed.value);
        clearPendingDeviceType();
        if (handled) {
            return true;
        }
    }

    return handleCommand(parsed.command, parsed.value, parsed);
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

void DaliEVGBase::setErrorState(bool error)
{
    errorState = error;
}

void DaliEVGBase::setOnLevel(uint8_t arcLevel)
{
    onLevel = arcLevel;
    if (onState && currentLevel > 0) {
        currentLevel = arcLevel;
    }
}

void DaliEVGBase::setOffLevel(uint8_t arcLevel)
{
    minLevel = arcLevel;
    if (!onState) {
        currentLevel = arcLevel;
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

void DaliEVGBase::setGroups(uint16_t groupBitsValue)
{
    groupBits = groupBitsValue;
}

void DaliEVGBase::startFadeTo(uint8_t targetLevel, uint32_t nowMs)
{
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
        return;
    }

    fadeActive = true;
    // If nowMs == 0 the caller didn't provide a timestamp; mark last time as 0
    fadeLastMs = nowMs;
    fadeAccumulator = 0.0f;
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
    return realDevicePresent;
}

void DaliEVGBase::setRealDevicePresent(bool present)
{
    realDevicePresent = present;
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
    // keep backward compatibility: start fade with now=0 (caller should call update with real time)
    startFadeTo(arcLevel, 0);
    return true;
}

bool DaliEVGBase::handleCommand(uint8_t command, uint8_t parameter, const ParsedFrame &parsed)
{
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
    if (command >= static_cast<uint8_t>(Dali::Command::GO_TO_SCENE)
        && command <= static_cast<uint8_t>(Dali::Command::GO_TO_SCENE) + 15) {
        startFadeTo(onLevel, 0);
        lastNonZeroLevel = onLevel;
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
    // Default device-specific commands are not supported in the base class.
    return false;
}

bool DaliEVGBase::handleSpecialCommand(uint8_t specialCommand, uint8_t value)
{
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

    if (handleDeviceQuery(query, parsed)) {
        return true;
    }

    return false;
}

bool DaliEVGBase::handleDeviceExtendedCommand(uint8_t command, uint8_t value)
{
    // Extended device commands are not handled by the generic base implementation.
    return false;
}

bool DaliEVGBase::handleDeviceQuery(uint8_t query, const ParsedFrame &parsed)
{
    return false;
}

void DaliEVGBase::respond(uint8_t value)
{
    if (realDevicePresent) {
        return;
    }

    Dali::Frame response;
    response.data = value;
    response.size = 8;
    response.flags = DALI_FRAME_BACKWARD;
    daliMaster.sendRaw(response);
}
