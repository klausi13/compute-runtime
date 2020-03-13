/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/preemption.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/test/unit_test/fixtures/preemption_fixture.h"
#include "shared/test/unit_test/helpers/debug_manager_state_restore.h"

#include "opencl/source/command_queue/command_queue_hw.h"
#include "opencl/source/helpers/dispatch_info.h"
#include "opencl/test/unit_test/helpers/dispatch_flags_helper.h"
#include "opencl/test/unit_test/helpers/hw_parse.h"
#include "opencl/test/unit_test/mocks/mock_builtins.h"
#include "opencl/test/unit_test/mocks/mock_device.h"
#include "opencl/test/unit_test/mocks/mock_graphics_allocation.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"

#include "gmock/gmock.h"

using namespace NEO;

class ThreadGroupPreemptionTests : public DevicePreemptionTests {
    void SetUp() override {
        dbgRestore.reset(new DebugManagerStateRestore());
        DebugManager.flags.ForcePreemptionMode.set(static_cast<int32_t>(PreemptionMode::ThreadGroup));
        preemptionMode = PreemptionMode::ThreadGroup;
        DevicePreemptionTests::SetUp();
    }
};

class MidThreadPreemptionTests : public DevicePreemptionTests {
  public:
    void SetUp() override {
        dbgRestore.reset(new DebugManagerStateRestore());
        DebugManager.flags.ForcePreemptionMode.set(static_cast<int32_t>(PreemptionMode::MidThread));
        preemptionMode = PreemptionMode::MidThread;
        DevicePreemptionTests::SetUp();
    }
};

TEST_F(ThreadGroupPreemptionTests, disallowByKMD) {
    PreemptionFlags flags = {};
    waTable->waDisablePerCtxtPreemptionGranularityControl = 1;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_FALSE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, disallowByDevice) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidThread);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::MidThread, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, disallowByReadWriteFencesWA) {
    PreemptionFlags flags = {};
    executionEnvironment->UsesFencesForReadWriteImages = 1u;
    waTable->waDisableLSQCROPERFforOCL = 1;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_FALSE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, disallowBySchedulerKernel) {
    PreemptionFlags flags = {};
    kernel.reset(new MockKernel(program.get(), *kernelInfo, *device, true));
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_FALSE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, disallowByVmeKernel) {
    PreemptionFlags flags = {};
    kernelInfo->isVmeWorkload = true;
    kernel.reset(new MockKernel(program.get(), *kernelInfo, *device));
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_FALSE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, simpleAllow) {
    PreemptionFlags flags = {};
    EXPECT_TRUE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, allowDefaultModeForNonKernelRequest) {
    PreemptionFlags flags = {};
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), nullptr);
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, givenKernelWithNoEnvironmentPatchSetWhenLSQCWaIsTurnedOnThenThreadGroupPreemptionIsBeingSelected) {
    PreemptionFlags flags = {};
    kernelInfo.get()->patchInfo.executionEnvironment = nullptr;
    waTable->waDisableLSQCROPERFforOCL = 1;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, givenKernelWithEnvironmentPatchSetWhenLSQCWaIsTurnedOnThenThreadGroupPreemptionIsBeingSelected) {
    PreemptionFlags flags = {};
    executionEnvironment.get()->UsesFencesForReadWriteImages = 0;
    waTable->waDisableLSQCROPERFforOCL = 1;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, givenKernelWithEnvironmentPatchSetWhenLSQCWaIsTurnedOffThenThreadGroupPreemptionIsBeingSelected) {
    PreemptionFlags flags = {};
    executionEnvironment.get()->UsesFencesForReadWriteImages = 1;
    waTable->waDisableLSQCROPERFforOCL = 0;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowThreadGroupPreemption(flags));
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, allowMidBatch) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidBatch);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), nullptr);
    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, disallowWhenAdjustedDisabled) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::Disabled);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), nullptr);
    EXPECT_EQ(PreemptionMode::Disabled, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(ThreadGroupPreemptionTests, returnDefaultDeviceModeForZeroSizedMdi) {
    MultiDispatchInfo multiDispatchInfo;
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getDevice(), multiDispatchInfo));
}

TEST_F(ThreadGroupPreemptionTests, returnDefaultDeviceModeForValidKernelsInMdi) {
    MultiDispatchInfo multiDispatchInfo;
    multiDispatchInfo.push(*dispatchInfo);
    multiDispatchInfo.push(*dispatchInfo);
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getDevice(), multiDispatchInfo));
}

TEST_F(ThreadGroupPreemptionTests, disallowDefaultDeviceModeForValidKernelsInMdiAndDisabledPremption) {
    device->setPreemptionMode(PreemptionMode::Disabled);
    MultiDispatchInfo multiDispatchInfo;
    multiDispatchInfo.push(*dispatchInfo);
    multiDispatchInfo.push(*dispatchInfo);
    EXPECT_EQ(PreemptionMode::Disabled, PreemptionHelper::taskPreemptionMode(device->getDevice(), multiDispatchInfo));
}

TEST_F(ThreadGroupPreemptionTests, disallowDefaultDeviceModeWhenAtLeastOneInvalidKernelInMdi) {
    MockKernel schedulerKernel(program.get(), *kernelInfo, *device, true);
    DispatchInfo schedulerDispatchInfo(&schedulerKernel, 1, Vec3<size_t>(1, 1, 1), Vec3<size_t>(1, 1, 1), Vec3<size_t>(0, 0, 0));

    PreemptionFlags flags = {};
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), &schedulerKernel);
    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));

    MultiDispatchInfo multiDispatchInfo;
    multiDispatchInfo.push(*dispatchInfo);
    multiDispatchInfo.push(schedulerDispatchInfo);
    multiDispatchInfo.push(*dispatchInfo);

    EXPECT_EQ(PreemptionMode::MidBatch, PreemptionHelper::taskPreemptionMode(device->getDevice(), multiDispatchInfo));
}

TEST_F(MidThreadPreemptionTests, allowMidThreadPreemption) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidThread);
    executionEnvironment->DisableMidThreadPreemption = 0;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowMidThreadPreemption(flags));
}

TEST_F(MidThreadPreemptionTests, allowMidThreadPreemptionNullKernel) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidThread);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), nullptr);
    EXPECT_TRUE(PreemptionHelper::allowMidThreadPreemption(flags));
}

TEST_F(MidThreadPreemptionTests, allowMidThreadPreemptionDeviceSupportPreemptionOnVmeKernel) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidThread);
    device->deviceInfo.vmeAvcSupportsPreemption = true;
    device->device.deviceInfo.vmeAvcSupportsPreemption = true;
    kernelInfo->isVmeWorkload = true;
    kernel.reset(new MockKernel(program.get(), *kernelInfo, *device));
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowMidThreadPreemption(flags));
}

TEST_F(MidThreadPreemptionTests, disallowMidThreadPreemptionByDevice) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::ThreadGroup);
    executionEnvironment->DisableMidThreadPreemption = 0;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_TRUE(PreemptionHelper::allowMidThreadPreemption(flags));
    EXPECT_EQ(PreemptionMode::ThreadGroup, PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags));
}

TEST_F(MidThreadPreemptionTests, disallowMidThreadPreemptionByKernel) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidThread);
    executionEnvironment->DisableMidThreadPreemption = 1;
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_FALSE(PreemptionHelper::allowMidThreadPreemption(flags));
}

TEST_F(MidThreadPreemptionTests, disallowMidThreadPreemptionByVmeKernel) {
    PreemptionFlags flags = {};
    device->setPreemptionMode(PreemptionMode::MidThread);
    device->deviceInfo.vmeAvcSupportsPreemption = false;
    device->device.deviceInfo.vmeAvcSupportsPreemption = false;
    kernelInfo->isVmeWorkload = true;
    kernel.reset(new MockKernel(program.get(), *kernelInfo, *device));
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    EXPECT_FALSE(PreemptionHelper::allowMidThreadPreemption(flags));
}

TEST_F(MidThreadPreemptionTests, taskPreemptionDisallowMidThreadByDevice) {
    PreemptionFlags flags = {};
    executionEnvironment->DisableMidThreadPreemption = 0;
    device->setPreemptionMode(PreemptionMode::ThreadGroup);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    PreemptionMode outMode = PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags);
    EXPECT_EQ(PreemptionMode::ThreadGroup, outMode);
}

TEST_F(MidThreadPreemptionTests, taskPreemptionDisallowMidThreadByKernel) {
    PreemptionFlags flags = {};
    executionEnvironment->DisableMidThreadPreemption = 1;
    device->setPreemptionMode(PreemptionMode::MidThread);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    PreemptionMode outMode = PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags);
    EXPECT_EQ(PreemptionMode::ThreadGroup, outMode);
}

TEST_F(MidThreadPreemptionTests, taskPreemptionDisallowMidThreadByVmeKernel) {
    PreemptionFlags flags = {};
    kernelInfo->isVmeWorkload = true;
    device->deviceInfo.vmeAvcSupportsPreemption = false;
    device->device.deviceInfo.vmeAvcSupportsPreemption = false;
    kernel.reset(new MockKernel(program.get(), *kernelInfo, *device));
    device->setPreemptionMode(PreemptionMode::MidThread);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    PreemptionMode outMode = PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags);
    //VME disables mid thread and thread group when device does not support it
    EXPECT_EQ(PreemptionMode::MidBatch, outMode);
}

TEST_F(MidThreadPreemptionTests, taskPreemptionAllow) {
    PreemptionFlags flags = {};
    executionEnvironment->DisableMidThreadPreemption = 0;
    device->setPreemptionMode(PreemptionMode::MidThread);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    PreemptionMode outMode = PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags);
    EXPECT_EQ(PreemptionMode::MidThread, outMode);
}

TEST_F(MidThreadPreemptionTests, taskPreemptionAllowDeviceSupportsPreemptionOnVmeKernel) {
    PreemptionFlags flags = {};
    executionEnvironment->DisableMidThreadPreemption = 0;
    kernelInfo->isVmeWorkload = true;
    kernel.reset(new MockKernel(program.get(), *kernelInfo, *device));
    device->deviceInfo.vmeAvcSupportsPreemption = true;
    device->device.deviceInfo.vmeAvcSupportsPreemption = true;
    device->setPreemptionMode(PreemptionMode::MidThread);
    PreemptionHelper::setPreemptionLevelFlags(flags, device->getDevice(), kernel.get());
    PreemptionMode outMode = PreemptionHelper::taskPreemptionMode(device->getPreemptionMode(), flags);
    EXPECT_EQ(PreemptionMode::MidThread, outMode);
}

TEST_F(DevicePreemptionTests, setDefaultMidThreadPreemption) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::MidThread;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, true, true, true);
    EXPECT_EQ(PreemptionMode::MidThread, devCapabilities.defaultPreemptionMode);
}

TEST_F(DevicePreemptionTests, setDefaultThreadGroupPreemptionNoMidThreadDefault) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::ThreadGroup;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, true, true, true);
    EXPECT_EQ(PreemptionMode::ThreadGroup, devCapabilities.defaultPreemptionMode);
}

TEST_F(DevicePreemptionTests, setDefaultThreadGroupPreemptionNoMidThreadSupport) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::MidThread;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, false, true, true);
    EXPECT_EQ(PreemptionMode::ThreadGroup, devCapabilities.defaultPreemptionMode);
}

TEST_F(DevicePreemptionTests, setDefaultMidBatchPreemptionNoThreadGroupDefault) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::MidBatch;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, true, true, true);
    EXPECT_EQ(PreemptionMode::MidBatch, devCapabilities.defaultPreemptionMode);
}

TEST_F(DevicePreemptionTests, setDefaultMidBatchPreemptionNoThreadGroupSupport) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::MidThread;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, false, false, true);
    EXPECT_EQ(PreemptionMode::MidBatch, devCapabilities.defaultPreemptionMode);
}

TEST_F(DevicePreemptionTests, setDefaultDisabledPreemptionNoMidBatchDefault) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::Disabled;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, true, true, true);
    EXPECT_EQ(PreemptionMode::Disabled, devCapabilities.defaultPreemptionMode);
}

TEST_F(DevicePreemptionTests, setDefaultDisabledPreemptionNoMidBatchSupport) {
    RuntimeCapabilityTable devCapabilities = {};

    devCapabilities.defaultPreemptionMode = PreemptionMode::MidThread;

    PreemptionHelper::adjustDefaultPreemptionMode(devCapabilities, false, false, false);
    EXPECT_EQ(PreemptionMode::Disabled, devCapabilities.defaultPreemptionMode);
}

struct PreemptionHwTest : ::testing::Test, ::testing::WithParamInterface<PreemptionMode> {
};

HWTEST_P(PreemptionHwTest, getRequiredCmdStreamSizeReturns0WhenPreemptionModeIsNotChanging) {
    PreemptionMode mode = GetParam();
    size_t requiredSize = PreemptionHelper::getRequiredCmdStreamSize<FamilyType>(mode, mode);
    EXPECT_EQ(0U, requiredSize);

    StackVec<char, 4096> buffer(requiredSize);
    LinearStream cmdStream(buffer.begin(), buffer.size());

    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    {
        auto builtIns = new MockBuiltins();

        builtIns->overrideSipKernel(std::unique_ptr<NEO::SipKernel>(new NEO::SipKernel{SipKernelType::Csr, GlobalMockSipProgram::getSipProgramWithCustomBinary()}));
        mockDevice->getExecutionEnvironment()->rootDeviceEnvironments[0]->builtins.reset(builtIns);
        PreemptionHelper::programCmdStream<FamilyType>(cmdStream, mode, mode, nullptr);
    }
    EXPECT_EQ(0U, cmdStream.getUsed());
}

HWTEST_P(PreemptionHwTest, getRequiredCmdStreamSizeReturnsSizeOfMiLoadRegisterImmWhenPreemptionModeIsChanging) {
    PreemptionMode mode = GetParam();
    PreemptionMode differentPreemptionMode = static_cast<PreemptionMode>(0);

    if (false == GetPreemptionTestHwDetails<FamilyType>().supportsPreemptionProgramming()) {
        EXPECT_EQ(0U, PreemptionHelper::getRequiredCmdStreamSize<FamilyType>(mode, differentPreemptionMode));
        return;
    }

    using MI_LOAD_REGISTER_IMM = typename FamilyType::MI_LOAD_REGISTER_IMM;

    size_t requiredSize = PreemptionHelper::getRequiredCmdStreamSize<FamilyType>(mode, differentPreemptionMode);
    EXPECT_EQ(sizeof(MI_LOAD_REGISTER_IMM), requiredSize);

    StackVec<char, 4096> buffer(requiredSize);
    LinearStream cmdStream(buffer.begin(), buffer.size());

    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));

    size_t minCsrSize = mockDevice->getHardwareInfo().gtSystemInfo.CsrSizeInMb * MemoryConstants::megaByte;
    uint64_t minCsrAlignment = 2 * 256 * MemoryConstants::kiloByte;
    MockGraphicsAllocation csrSurface((void *)minCsrAlignment, minCsrSize);

    PreemptionHelper::programCmdStream<FamilyType>(cmdStream, mode, differentPreemptionMode, nullptr);
    EXPECT_EQ(requiredSize, cmdStream.getUsed());
}

HWTEST_P(PreemptionHwTest, programCmdStreamAddsProperMiLoadRegisterImmCommandToTheStream) {
    PreemptionMode mode = GetParam();
    PreemptionMode differentPreemptionMode = static_cast<PreemptionMode>(0);
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));

    if (false == GetPreemptionTestHwDetails<FamilyType>().supportsPreemptionProgramming()) {
        LinearStream cmdStream(nullptr, 0U);
        PreemptionHelper::programCmdStream<FamilyType>(cmdStream, mode, differentPreemptionMode, nullptr);
        EXPECT_EQ(0U, cmdStream.getUsed());
        return;
    }

    using MI_LOAD_REGISTER_IMM = typename FamilyType::MI_LOAD_REGISTER_IMM;
    auto hwDetails = GetPreemptionTestHwDetails<FamilyType>();

    uint32_t defaultRegValue = hwDetails.defaultRegValue;

    uint32_t expectedRegValue = defaultRegValue;
    if (hwDetails.modeToRegValueMap.find(mode) != hwDetails.modeToRegValueMap.end()) {
        expectedRegValue = hwDetails.modeToRegValueMap[mode];
    }

    size_t requiredSize = PreemptionHelper::getRequiredCmdStreamSize<FamilyType>(mode, differentPreemptionMode);
    StackVec<char, 4096> buffer(requiredSize);
    LinearStream cmdStream(buffer.begin(), buffer.size());

    size_t minCsrSize = mockDevice->getHardwareInfo().gtSystemInfo.CsrSizeInMb * MemoryConstants::megaByte;
    uint64_t minCsrAlignment = 2 * 256 * MemoryConstants::kiloByte;
    MockGraphicsAllocation csrSurface((void *)minCsrAlignment, minCsrSize);

    PreemptionHelper::programCmdStream<FamilyType>(cmdStream, mode, differentPreemptionMode, &csrSurface);

    HardwareParse cmdParser;
    cmdParser.parseCommands<FamilyType>(cmdStream);
    const uint32_t regAddress = hwDetails.regAddress;
    MI_LOAD_REGISTER_IMM *cmd = findMmioCmd<FamilyType>(cmdParser.cmdList.begin(), cmdParser.cmdList.end(), regAddress);
    ASSERT_NE(nullptr, cmd);
    EXPECT_EQ(expectedRegValue, cmd->getDataDword());
}

INSTANTIATE_TEST_CASE_P(
    CreateParametrizedPreemptionHwTest,
    PreemptionHwTest,
    ::testing::Values(PreemptionMode::Disabled, PreemptionMode::MidBatch, PreemptionMode::ThreadGroup, PreemptionMode::MidThread));

struct PreemptionTest : ::testing::Test, ::testing::WithParamInterface<PreemptionMode> {
};

HWTEST_P(PreemptionTest, whenInNonMidThreadModeThenSizeForStateSipIsZero) {
    PreemptionMode mode = GetParam();
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    mockDevice->setPreemptionMode(mode);

    auto size = PreemptionHelper::getRequiredStateSipCmdSize<FamilyType>(*mockDevice);
    EXPECT_EQ(0u, size);
}

HWTEST_P(PreemptionTest, whenInNonMidThreadModeThenStateSipIsNotProgrammed) {
    PreemptionMode mode = GetParam();
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    mockDevice->setPreemptionMode(mode);

    auto requiredSize = PreemptionHelper::getRequiredStateSipCmdSize<FamilyType>(*mockDevice);
    StackVec<char, 4096> buffer(requiredSize);
    LinearStream cmdStream(buffer.begin(), buffer.size());

    PreemptionHelper::programStateSip<FamilyType>(cmdStream, *mockDevice);
    EXPECT_EQ(0u, cmdStream.getUsed());
}

HWTEST_P(PreemptionTest, whenInNonMidThreadModeThenSizeForCsrBaseAddressIsZero) {
    PreemptionMode mode = GetParam();
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    mockDevice->setPreemptionMode(mode);

    auto size = PreemptionHelper::getRequiredPreambleSize<FamilyType>(*mockDevice);
    EXPECT_EQ(0u, size);
}

HWTEST_P(PreemptionTest, whenInNonMidThreadModeThenCsrBaseAddressIsNotProgrammed) {
    PreemptionMode mode = GetParam();
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    mockDevice->setPreemptionMode(mode);

    auto requiredSize = PreemptionHelper::getRequiredPreambleSize<FamilyType>(*mockDevice);
    StackVec<char, 4096> buffer(requiredSize);
    LinearStream cmdStream(buffer.begin(), buffer.size());

    PreemptionHelper::programCsrBaseAddress<FamilyType>(cmdStream, *mockDevice, nullptr);
    EXPECT_EQ(0u, cmdStream.getUsed());
}

HWTEST_P(PreemptionTest, whenFailToCreatePreemptionAllocationThenFailToCreateDevice) {

    class MockUltCsr : public UltCommandStreamReceiver<FamilyType> {

      public:
        MockUltCsr(ExecutionEnvironment &executionEnvironment) : UltCommandStreamReceiver<FamilyType>(executionEnvironment, 0) {
        }

        bool createPreemptionAllocation() override {
            return false;
        }
    };

    class MockDeviceReturnedDebuggerActive : public MockDevice {
      public:
        MockDeviceReturnedDebuggerActive(ExecutionEnvironment *executionEnvironment, uint32_t deviceIndex)
            : MockDevice(executionEnvironment, deviceIndex) {}
        bool isDebuggerActive() const override {
            return true;
        }
        std::unique_ptr<CommandStreamReceiver> createCommandStreamReceiver() const override {
            return std::make_unique<MockUltCsr>(*executionEnvironment);
        }
    };

    ExecutionEnvironment *executionEnvironment = platform()->peekExecutionEnvironment();

    std::unique_ptr<MockDevice> mockDevice(MockDevice::create<MockDeviceReturnedDebuggerActive>(executionEnvironment, 0));
    EXPECT_EQ(nullptr, mockDevice);
}

INSTANTIATE_TEST_CASE_P(
    NonMidThread,
    PreemptionTest,
    ::testing::Values(PreemptionMode::Disabled, PreemptionMode::MidBatch, PreemptionMode::ThreadGroup));

HWTEST_F(MidThreadPreemptionTests, createCsrSurfaceNoWa) {
    HardwareInfo hwInfo = *platformDevices[0];
    hwInfo.workaroundTable.waCSRUncachable = false;

    std::unique_ptr<MockDevice> mockDevice(MockDevice::createWithNewExecutionEnvironment<MockDevice>(&hwInfo));
    ASSERT_NE(nullptr, mockDevice.get());

    auto &csr = mockDevice->getUltCommandStreamReceiver<FamilyType>();
    MemoryAllocation *csrSurface = static_cast<MemoryAllocation *>(csr.getPreemptionAllocation());
    ASSERT_NE(nullptr, csrSurface);
    EXPECT_FALSE(csrSurface->uncacheable);

    GraphicsAllocation *devCsrSurface = csr.getPreemptionAllocation();
    EXPECT_EQ(csrSurface, devCsrSurface);
}

HWTEST_F(MidThreadPreemptionTests, givenMidThreadPreemptionWhenFailingOnCsrSurfaceAllocationThenFailToCreateDevice) {

    class FailingMemoryManager : public OsAgnosticMemoryManager {
      public:
        FailingMemoryManager(ExecutionEnvironment &executionEnvironment) : OsAgnosticMemoryManager(executionEnvironment) {}

        GraphicsAllocation *allocateGraphicsMemoryWithAlignment(const AllocationData &allocationData) override {
            auto hwInfo = executionEnvironment.rootDeviceEnvironments[allocationData.rootDeviceIndex]->getHardwareInfo();
            if (++allocateGraphicsMemoryCount > HwHelper::get(hwInfo->platform.eRenderCoreFamily).getGpgpuEngineInstances(*hwInfo).size() - 1) {
                return nullptr;
            }
            return OsAgnosticMemoryManager::allocateGraphicsMemoryWithAlignment(allocationData);
        }

        uint32_t allocateGraphicsMemoryCount = 0;
    };
    ExecutionEnvironment *executionEnvironment = platform()->peekExecutionEnvironment();
    executionEnvironment->memoryManager = std::make_unique<FailingMemoryManager>(*executionEnvironment);

    std::unique_ptr<MockDevice> mockDevice(MockDevice::create<MockDevice>(executionEnvironment, 0));
    EXPECT_EQ(nullptr, mockDevice.get());
}

HWTEST_F(MidThreadPreemptionTests, createCsrSurfaceWa) {
    HardwareInfo hwInfo = *platformDevices[0];
    hwInfo.workaroundTable.waCSRUncachable = true;

    std::unique_ptr<MockDevice> mockDevice(MockDevice::createWithNewExecutionEnvironment<MockDevice>(&hwInfo));
    ASSERT_NE(nullptr, mockDevice.get());

    auto &csr = mockDevice->getUltCommandStreamReceiver<FamilyType>();
    MemoryAllocation *csrSurface = static_cast<MemoryAllocation *>(csr.getPreemptionAllocation());
    ASSERT_NE(nullptr, csrSurface);
    EXPECT_TRUE(csrSurface->uncacheable);

    GraphicsAllocation *devCsrSurface = csr.getPreemptionAllocation();
    EXPECT_EQ(csrSurface, devCsrSurface);
}

HWCMDTEST_F(IGFX_GEN8_CORE, MidThreadPreemptionTests, givenDirtyCsrStateWhenStateBaseAddressIsProgrammedThenStateSipIsAdded) {
    using STATE_BASE_ADDRESS = typename FamilyType::STATE_BASE_ADDRESS;
    using STATE_SIP = typename FamilyType::STATE_SIP;

    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));

    if (mockDevice->getHardwareInfo().capabilityTable.defaultPreemptionMode == PreemptionMode::MidThread) {
        mockDevice->setPreemptionMode(PreemptionMode::MidThread);

        auto &csr = mockDevice->getUltCommandStreamReceiver<FamilyType>();
        csr.isPreambleSent = true;

        CommandQueueHw<FamilyType> commandQueue(nullptr, device.get(), 0, false);
        auto &commandStream = commandQueue.getCS(4096u);

        DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();

        void *buffer = alignedMalloc(MemoryConstants::pageSize, MemoryConstants::pageSize64k);

        std::unique_ptr<MockGraphicsAllocation> allocation(new MockGraphicsAllocation(buffer, MemoryConstants::pageSize));
        std::unique_ptr<IndirectHeap> heap(new IndirectHeap(allocation.get()));

        csr.flushTask(commandStream,
                      0,
                      *heap.get(),
                      *heap.get(),
                      *heap.get(),
                      0,
                      dispatchFlags,
                      *mockDevice);

        HardwareParse hwParser;
        hwParser.parseCommands<FamilyType>(csr.getCS(0));

        auto stateBaseAddressItor = find<STATE_BASE_ADDRESS *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
        EXPECT_NE(hwParser.cmdList.end(), stateBaseAddressItor);

        auto stateSipItor = find<STATE_SIP *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
        EXPECT_NE(hwParser.cmdList.end(), stateSipItor);

        auto stateSipAfterSBA = ++stateBaseAddressItor;
        EXPECT_EQ(*stateSipAfterSBA, *stateSipItor);

        alignedFree(buffer);
    }
}

HWCMDTEST_F(IGFX_GEN8_CORE, MidThreadPreemptionTests, givenPreemptionProgrammedAfterVFEStateProgrammedInFlushedCmdBuffer) {
    using MEDIA_VFE_STATE = typename FamilyType::MEDIA_VFE_STATE;

    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));

    if (mockDevice->getHardwareInfo().capabilityTable.defaultPreemptionMode == PreemptionMode::MidThread) {
        mockDevice->setPreemptionMode(PreemptionMode::MidThread);

        auto &csr = mockDevice->getUltCommandStreamReceiver<FamilyType>();
        csr.isPreambleSent = true;

        CommandQueueHw<FamilyType> commandQueue(nullptr, device.get(), 0, false);
        auto &commandStream = commandQueue.getCS(4096u);

        DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();

        void *buffer = alignedMalloc(MemoryConstants::pageSize, MemoryConstants::pageSize64k);

        std::unique_ptr<MockGraphicsAllocation> allocation(new MockGraphicsAllocation(buffer, MemoryConstants::pageSize));
        std::unique_ptr<IndirectHeap> heap(new IndirectHeap(allocation.get()));

        csr.flushTask(commandStream,
                      0,
                      *heap.get(),
                      *heap.get(),
                      *heap.get(),
                      0,
                      dispatchFlags,
                      *mockDevice);

        auto hwDetails = GetPreemptionTestHwDetails<FamilyType>();

        HardwareParse hwParser;
        hwParser.parseCommands<FamilyType>(csr.getCS(0));

        const uint32_t regAddress = hwDetails.regAddress;
        auto itorPreemptionMode = findMmio<FamilyType>(hwParser.cmdList.begin(), hwParser.cmdList.end(), regAddress);
        auto itorMediaVFEMode = find<MEDIA_VFE_STATE *>(hwParser.cmdList.begin(), hwParser.cmdList.end());

        itorMediaVFEMode++;
        EXPECT_TRUE(itorMediaVFEMode == itorPreemptionMode);

        alignedFree(buffer);
    }
}
