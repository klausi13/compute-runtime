/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/hw_helper.h"

#include "opencl/test/unit_test/fixtures/device_fixture.h"
#include "test.h"

using namespace NEO;

typedef Test<DeviceFixture> BdwDeviceCaps;

BDWTEST_F(BdwDeviceCaps, givenBdwDeviceWhenAskedForClVersionThenReport21) {
    const auto &caps = pClDevice->getDeviceInfo();
    EXPECT_STREQ("OpenCL 2.1 NEO ", caps.clVersion);
    EXPECT_STREQ("OpenCL C 2.0 ", caps.clCVersion);
}

BDWTEST_F(BdwDeviceCaps, skuSpecificCaps) {
    const auto &caps = pClDevice->getDeviceInfo();
    std::string extensionString = caps.deviceExtensions;

    EXPECT_NE(std::string::npos, extensionString.find(std::string("cl_khr_fp64")));
    EXPECT_NE(0u, caps.doubleFpConfig);
}

BDWTEST_F(BdwDeviceCaps, allSkusSupportCorrectlyRoundedDivideSqrt) {
    const auto &caps = pClDevice->getDeviceInfo();
    EXPECT_NE(0u, caps.singleFpConfig & CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT);
}

BDWTEST_F(BdwDeviceCaps, defaultPreemptionMode) {
    EXPECT_TRUE(PreemptionMode::Disabled == pDevice->getHardwareInfo().capabilityTable.defaultPreemptionMode);
}

BDWTEST_F(BdwDeviceCaps, BdwProfilingTimerResolution) {
    const auto &caps = pDevice->getDeviceInfo();
    EXPECT_EQ(80u, caps.outProfilingTimerResolution);
}

BDWTEST_F(BdwDeviceCaps, givenHwInfoWhenRequestedComputeUnitsUsedForScratchThenReturnValidValue) {
    const auto &hwInfo = pDevice->getHardwareInfo();
    auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint32_t expectedValue = hwInfo.gtSystemInfo.MaxSubSlicesSupported * hwInfo.gtSystemInfo.MaxEuPerSubSlice *
                             hwInfo.gtSystemInfo.ThreadCount / hwInfo.gtSystemInfo.EUCount;

    EXPECT_EQ(expectedValue, hwHelper.getComputeUnitsUsedForScratch(&hwInfo));
    EXPECT_EQ(expectedValue, pDevice->getDeviceInfo().computeUnitsUsedForScratch);
}

BDWTEST_F(BdwDeviceCaps, givenHwInfoWhenRequestedMaxFrontEndThreadsThenReturnValidValue) {
    const auto &hwInfo = pDevice->getHardwareInfo();

    EXPECT_EQ(HwHelper::getMaxThreadsForVfe(hwInfo), pDevice->getDeviceInfo().maxFrontEndThreads);
}

typedef Test<DeviceFixture> BdwUsDeviceIdTest;

BDWTEST_F(BdwUsDeviceIdTest, isSimulationCap) {
    unsigned short bdwSimulationIds[6] = {
        IBDW_GT0_DESK_DEVICE_F0_ID,
        IBDW_GT1_DESK_DEVICE_F0_ID,
        IBDW_GT2_DESK_DEVICE_F0_ID,
        IBDW_GT3_DESK_DEVICE_F0_ID,
        IBDW_GT4_DESK_DEVICE_F0_ID,
        0, // default, non-simulation
    };
    NEO::MockDevice *mockDevice = nullptr;

    for (auto id : bdwSimulationIds) {
        mockDevice = createWithUsDeviceId(id);
        ASSERT_NE(mockDevice, nullptr);

        if (id == 0)
            EXPECT_FALSE(mockDevice->isSimulation());
        else
            EXPECT_TRUE(mockDevice->isSimulation());
        delete mockDevice;
    }
}

BDWTEST_F(BdwUsDeviceIdTest, GivenBDWWhenCheckftr64KBpagesThenFalse) {
    EXPECT_FALSE(pDevice->getHardwareInfo().capabilityTable.ftr64KBpages);
}
