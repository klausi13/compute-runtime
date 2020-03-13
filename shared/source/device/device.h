/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/common_types.h"
#include "shared/source/helpers/engine_control.h"
#include "shared/source/helpers/hw_info.h"

#include "opencl/source/device/device_info.h"
#include "opencl/source/os_interface/performance_counters.h"

namespace NEO {
class OSTime;

class Device : public ReferenceTrackedObject<Device> {
  public:
    Device &operator=(const Device &) = delete;
    Device(const Device &) = delete;
    ~Device() override;

    unsigned int getSupportedClVersion() const;

    template <typename DeviceT, typename... ArgsT>
    static DeviceT *create(ArgsT &&... args) {
        DeviceT *device = new DeviceT(std::forward<ArgsT>(args)...);
        return createDeviceInternals(device);
    }

    bool getDeviceAndHostTimer(uint64_t *deviceTimestamp, uint64_t *hostTimestamp) const;
    bool getHostTimer(uint64_t *hostTimestamp) const;
    const HardwareInfo &getHardwareInfo() const;
    const DeviceInfo &getDeviceInfo() const;
    EngineControl &getEngine(aub_stream::EngineType engineType, bool lowPriority);
    EngineControl &getDefaultEngine();
    EngineControl &getInternalEngine();
    std::atomic<uint32_t> &getSelectorCopyEngine();
    MemoryManager *getMemoryManager() const;
    GmmHelper *getGmmHelper() const;
    GmmClientContext *getGmmClientContext() const;
    OSTime *getOSTime() const { return osTime.get(); };
    double getProfilingTimerResolution();
    double getPlatformHostTimerResolution() const;
    bool isSimulation() const;
    GFXCORE_FAMILY getRenderCoreFamily() const;
    PerformanceCounters *getPerformanceCounters() { return performanceCounters.get(); }
    PreemptionMode getPreemptionMode() const { return preemptionMode; }
    MOCKABLE_VIRTUAL bool isDebuggerActive() const;
    Debugger *getDebugger() { return executionEnvironment->debugger.get(); }
    ExecutionEnvironment *getExecutionEnvironment() const { return executionEnvironment; }
    const RootDeviceEnvironment &getRootDeviceEnvironment() const { return *executionEnvironment->rootDeviceEnvironments[getRootDeviceIndex()]; }
    const HardwareCapabilities &getHardwareCapabilities() const { return hardwareCapabilities; }
    bool isFullRangeSvm() const {
        return getRootDeviceEnvironment().isFullRangeSvm();
    }
    bool areSharedSystemAllocationsAllowed() const {
        return this->deviceInfo.sharedSystemAllocationsSupport;
    }
    template <typename SpecializedDeviceT>
    void setSpecializedDevice(SpecializedDeviceT *specializedDevice) {
        this->specializedDevice = reinterpret_cast<uintptr_t>(specializedDevice);
    }
    template <typename SpecializedDeviceT>
    SpecializedDeviceT *getSpecializedDevice() const {
        return reinterpret_cast<SpecializedDeviceT *>(specializedDevice);
    }
    CompilerInterface *getCompilerInterface() const;
    BuiltIns *getBuiltIns() const;

    virtual uint32_t getRootDeviceIndex() const = 0;
    virtual uint32_t getNumAvailableDevices() const = 0;
    virtual Device *getDeviceById(uint32_t deviceId) const = 0;
    virtual DeviceBitfield getDeviceBitfield() const = 0;

    static decltype(&PerformanceCounters::create) createPerformanceCountersFunc;

  protected:
    Device() = delete;
    Device(ExecutionEnvironment *executionEnvironment);

    MOCKABLE_VIRTUAL void initializeCaps();

    template <typename T>
    static T *createDeviceInternals(T *device) {
        if (false == device->createDeviceImpl()) {
            delete device;
            return nullptr;
        }
        return device;
    }

    virtual bool createDeviceImpl();
    virtual bool createEngines();
    bool createEngine(uint32_t deviceCsrIndex, aub_stream::EngineType engineType);
    MOCKABLE_VIRTUAL std::unique_ptr<CommandStreamReceiver> createCommandStreamReceiver() const;

    DeviceInfo deviceInfo = {};

    HardwareCapabilities hardwareCapabilities = {};
    std::unique_ptr<OSTime> osTime;
    std::unique_ptr<PerformanceCounters> performanceCounters;
    std::vector<std::unique_ptr<CommandStreamReceiver>> commandStreamReceivers;
    std::vector<EngineControl> engines;
    PreemptionMode preemptionMode;
    ExecutionEnvironment *executionEnvironment = nullptr;
    uint32_t defaultEngineIndex = 0;
    std::atomic<uint32_t> selectorCopyEngine{0};

    uintptr_t specializedDevice = reinterpret_cast<uintptr_t>(nullptr);
};

inline EngineControl &Device::getDefaultEngine() {
    return engines[defaultEngineIndex];
}

inline MemoryManager *Device::getMemoryManager() const {
    return executionEnvironment->memoryManager.get();
}

inline GmmHelper *Device::getGmmHelper() const {
    return getRootDeviceEnvironment().getGmmHelper();
}

inline CompilerInterface *Device::getCompilerInterface() const {
    return executionEnvironment->rootDeviceEnvironments[getRootDeviceIndex()]->getCompilerInterface();
}
inline BuiltIns *Device::getBuiltIns() const {
    return executionEnvironment->rootDeviceEnvironments[getRootDeviceIndex()]->getBuiltIns();
}

inline std::atomic<uint32_t> &Device::getSelectorCopyEngine() {
    return selectorCopyEngine;
}

} // namespace NEO
