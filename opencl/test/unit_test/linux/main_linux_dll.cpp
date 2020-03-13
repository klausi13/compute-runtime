/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/linux/allocator_helper.h"
#include "shared/source/os_interface/linux/os_interface.h"
#include "shared/test/unit_test/helpers/debug_manager_state_restore.h"
#include "shared/test/unit_test/helpers/default_hw_info.inl"
#include "shared/test/unit_test/helpers/ult_hw_config.inl"

#include "opencl/test/unit_test/custom_event_listener.h"
#include "opencl/test/unit_test/helpers/variable_backup.h"
#include "opencl/test/unit_test/linux/drm_wrap.h"
#include "opencl/test/unit_test/linux/mock_os_layer.h"
#include "opencl/test/unit_test/mocks/mock_execution_environment.h"
#include "opencl/test/unit_test/os_interface/linux/device_command_stream_fixture.h"
#include "test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <string>

using namespace NEO;

class DrmTestsFixture {
  public:
    void SetUp() {
        executionEnvironment.prepareRootDeviceEnvironments(1);
        rootDeviceEnvironment = executionEnvironment.rootDeviceEnvironments[0].get();
    }

    void TearDown() {
    }
    ExecutionEnvironment executionEnvironment;
    RootDeviceEnvironment *rootDeviceEnvironment = nullptr;
};

typedef Test<DrmTestsFixture> DrmTests;

void initializeTestedDevice() {
    for (uint32_t i = 0; deviceDescriptorTable[i].eGtType != GTTYPE::GTTYPE_UNDEFINED; i++) {
        if (platformDevices[0]->platform.eProductFamily == deviceDescriptorTable[i].pHwInfo->platform.eProductFamily) {
            deviceId = deviceDescriptorTable[i].deviceId;
            break;
        }
    }
}

int openRetVal = 0;
int testOpen(const char *fullPath, int, ...) {
    return openRetVal;
};

int openCounter = 1;
int openWithCounter(const char *fullPath, int, ...) {
    if (openCounter > 0) {
        openCounter--;
        return 1023; // valid file descriptor for ULT
    }
    return -1;
};

TEST(DrmTest, GivenTwoOpenableDevicesWhenDiscoverDevicesThenCreateTwoHwDeviceIds) {
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = openWithCounter;
    openCounter = 2;
    auto hwDeviceIds = OSInterface::discoverDevices();
    EXPECT_EQ(2u, hwDeviceIds.size());
}

TEST(DrmTest, GivenSelectedNotExistingDeviceWhenGetDeviceFdThenFail) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.ForceDeviceId.set("1234");
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = testOpen;
    openRetVal = -1;
    auto hwDeviceIds = OSInterface::discoverDevices();
    EXPECT_TRUE(hwDeviceIds.empty());
}

TEST(DrmTest, GivenSelectedExistingDeviceWhenGetDeviceFdThenReturnFd) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.ForceDeviceId.set("1234");
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openRetVal = 1023; // fakeFd
    openFull = testOpen;
    auto hwDeviceIds = OSInterface::discoverDevices();
    EXPECT_EQ(1u, hwDeviceIds.size());
    EXPECT_NE(nullptr, hwDeviceIds[0].get());
}

TEST(DrmTest, GivenSelectedIncorectDeviceWhenGetDeviceFdThenFail) {
    DebugManagerStateRestore stateRestore;
    DebugManager.flags.ForceDeviceId.set("1234");
    VariableBackup<decltype(openFull)> backupOpenFull(&openFull);
    openFull = testOpen;
    openRetVal = 1024;

    auto hwDeviceIds = OSInterface::discoverDevices();
    EXPECT_TRUE(hwDeviceIds.empty());
}

TEST_F(DrmTests, createReturnsDrm) {
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);

    drm_i915_getparam_t getParam;
    int lDeviceId;

    VariableBackup<decltype(ioctlCnt)> backupIoctlCnt(&ioctlCnt);
    VariableBackup<int> backupIoctlSeq(&ioctlSeq[0]);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = EINTR;
    // check if device works, although there was EINTR error from KMD
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    auto ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(deviceId, lDeviceId);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = EAGAIN;
    // check if device works, although there was EAGAIN error from KMD
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(deviceId, lDeviceId);

    ioctlCnt = 0;
    ioctlSeq[0] = -1;
    errno = 0;
    // we failed with any other error code
    getParam.param = I915_PARAM_CHIPSET_ID;
    getParam.value = &lDeviceId;
    ret = drm->ioctl(DRM_IOCTL_I915_GETPARAM, &getParam);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(deviceId, lDeviceId);
}

TEST_F(DrmTests, createTwiceReturnsDifferentDrm) {
    auto drm1 = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm1, nullptr);
    auto drm2 = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm2, nullptr);
    EXPECT_NE(drm1, drm2);
}

TEST_F(DrmTests, createDriFallback) {
    VariableBackup<decltype(haveDri)> backupHaveDri(&haveDri);

    haveDri = 1;
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
}

TEST_F(DrmTests, createNoDevice) {
    VariableBackup<decltype(haveDri)> backupHaveDri(&haveDri);
    haveDri = -1;
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, createUnknownDevice) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.PrintDebugMessages.set(true);

    VariableBackup<decltype(deviceId)> backupDeviceId(&deviceId);

    deviceId = -1;

    ::testing::internal::CaptureStderr();
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
    std::string errStr = ::testing::internal::GetCapturedStderr();
    EXPECT_THAT(errStr, ::testing::HasSubstr(std::string("FATAL: Unknown device: deviceId: ffffffff, revisionId: 0000")));
}

TEST_F(DrmTests, createNoSoftPin) {
    VariableBackup<decltype(haveSoftPin)> backupHaveSoftPin(&haveSoftPin);
    haveSoftPin = 0;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, failOnDeviceId) {
    VariableBackup<decltype(failOnDeviceId)> backupFailOnDeviceId(&failOnDeviceId);
    failOnDeviceId = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, failOnEuTotal) {
    VariableBackup<decltype(failOnEuTotal)> backupfailOnEuTotal(&failOnEuTotal);
    failOnEuTotal = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, failOnSubsliceTotal) {
    VariableBackup<decltype(failOnSubsliceTotal)> backupfailOnSubsliceTotal(&failOnSubsliceTotal);
    failOnSubsliceTotal = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, failOnRevisionId) {
    VariableBackup<decltype(failOnRevisionId)> backupFailOnRevisionId(&failOnRevisionId);
    failOnRevisionId = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, failOnSoftPin) {
    VariableBackup<decltype(failOnSoftPin)> backupFailOnSoftPin(&failOnSoftPin);
    failOnSoftPin = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
}

TEST_F(DrmTests, failOnParamBoost) {
    VariableBackup<decltype(failOnParamBoost)> backupFailOnParamBoost(&failOnParamBoost);
    failOnParamBoost = -1;

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    //non-fatal error - issue warning only
    EXPECT_NE(drm, nullptr);
}

TEST_F(DrmTests, failOnContextCreate) {
    VariableBackup<decltype(failOnContextCreate)> backupFailOnContextCreate(&failOnContextCreate);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
    failOnContextCreate = -1;
    EXPECT_THROW(drm->createDrmContext(), std::exception);
    EXPECT_FALSE(drm->isPreemptionSupported());
    failOnContextCreate = 0;
}

TEST_F(DrmTests, failOnSetPriority) {
    VariableBackup<decltype(failOnSetPriority)> backupFailOnSetPriority(&failOnSetPriority);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
    failOnSetPriority = -1;
    auto drmContext = drm->createDrmContext();
    EXPECT_THROW(drm->setLowPriorityContextParam(drmContext), std::exception);
    EXPECT_FALSE(drm->isPreemptionSupported());
    failOnSetPriority = 0;
}

TEST_F(DrmTests, failOnDrmGetVersion) {
    VariableBackup<decltype(failOnDrmVersion)> backupFailOnDrmVersion(&failOnDrmVersion);

    failOnDrmVersion = -1;
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
    failOnDrmVersion = 0;
}

TEST_F(DrmTests, failOnInvalidDeviceName) {
    VariableBackup<decltype(failOnDrmVersion)> backupFailOnDrmVersion(&failOnDrmVersion);

    strcpy(providedDrmVersion, "NA");
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_EQ(drm, nullptr);
    failOnDrmVersion = 0;
    strcpy(providedDrmVersion, "i915");
}

TEST_F(DrmTests, whenDrmIsCreatedThenSetMemoryRegionsDoesntFailAndDrmObjectIsReturned) {
    DebugManagerStateRestore restore;
    DebugManager.flags.EnableLocalMemory.set(1);

    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
}

TEST(AllocatorHelper, givenExpectedSizeToReserveWhenGetSizeToReserveCalledThenExpectedValueReturned) {
    EXPECT_EQ((maxNBitValue(47) + 1) / 4, NEO::getSizeToReserve());
}

TEST(DrmMemoryManagerCreate, whenCallCreateMemoryManagerThenDrmMemoryManagerIsCreated) {
    MockExecutionEnvironment executionEnvironment(*platformDevices);
    auto drm = new DrmMockSuccess(*executionEnvironment.rootDeviceEnvironments[0]);

    executionEnvironment.rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    executionEnvironment.rootDeviceEnvironments[0]->osInterface->get()->setDrm(drm);
    auto drmMemoryManager = MemoryManager::createMemoryManager(executionEnvironment);
    EXPECT_NE(nullptr, drmMemoryManager.get());
    executionEnvironment.memoryManager = std::move(drmMemoryManager);
}

TEST(OsInterfaceTests, givenOsInterfaceWhenEnableLocalMemoryIsSpecifiedThenItIsSetToTrueOn64Bit) {
    EXPECT_TRUE(OSInterface::osEnableLocalMemory);
}

int main(int argc, char **argv) {
    bool useDefaultListener = false;

    ::testing::InitGoogleTest(&argc, argv);

    // parse remaining args assuming they're mine
    for (int i = 1; i < argc; ++i) {
        if (!strcmp("--disable_default_listener", argv[i])) {
            useDefaultListener = false;
        } else if (!strcmp("--enable_default_listener", argv[i])) {
            useDefaultListener = true;
        }
    }

    if (useDefaultListener == false) {
        auto &listeners = ::testing::UnitTest::GetInstance()->listeners();
        auto defaultListener = listeners.default_result_printer();
        auto customEventListener = new CCustomEventListener(defaultListener);

        listeners.Release(defaultListener);
        listeners.Append(customEventListener);
    }

    initializeTestedDevice();

    auto retVal = RUN_ALL_TESTS();

    return retVal;
}

TEST_F(DrmTests, whenCreateDrmIsCalledThenProperHwInfoIsSetup) {
    auto oldHwInfo = rootDeviceEnvironment->getMutableHardwareInfo();
    *oldHwInfo = {};
    auto drm = DrmWrap::createDrm(*rootDeviceEnvironment);
    EXPECT_NE(drm, nullptr);
    auto currentHwInfo = rootDeviceEnvironment->getHardwareInfo();
    EXPECT_NE(IGFX_UNKNOWN, currentHwInfo->platform.eProductFamily);
    EXPECT_NE(IGFX_UNKNOWN_CORE, currentHwInfo->platform.eRenderCoreFamily);
    EXPECT_LT(0u, currentHwInfo->gtSystemInfo.EUCount);
    EXPECT_LT(0u, currentHwInfo->gtSystemInfo.SubSliceCount);
}
