/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/compiler_interface/compiler_interface.h"
#include "shared/source/device/device.h"

#include "opencl/source/device/cl_device.h"
#include "opencl/source/program/block_kernel_manager.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"
#include "opencl/test/unit_test/fixtures/platform_fixture.h"
#include "opencl/test/unit_test/fixtures/program_fixture.h"
#include "opencl/test/unit_test/fixtures/run_kernel_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_program.h"

#include <vector>

namespace NEO {

class ProgramWithBlockKernelsTest : public ContextFixture,
                                    public PlatformFixture,
                                    public ProgramFixture,
                                    public testing::Test {

    using ContextFixture::SetUp;
    using PlatformFixture::SetUp;

  protected:
    ProgramWithBlockKernelsTest() {
    }

    void SetUp() override {
        PlatformFixture::SetUp();
        device = pPlatform->getClDevice(0);
        ContextFixture::SetUp(1, &device);
        ProgramFixture::SetUp();
    }

    void TearDown() override {
        ProgramFixture::TearDown();
        ContextFixture::TearDown();
        PlatformFixture::TearDown();
    }
    cl_device_id device;
    cl_int retVal = CL_SUCCESS;
};

TEST_F(ProgramWithBlockKernelsTest, GivenKernelWithBlockKernelsWhenProgramIsBuildingThenKernelInfosHaveCorrectNames) {
    if (std::string(pPlatform->getClDevice(0)->getDeviceInfo().clVersion).find("OpenCL 2.") != std::string::npos) {
        CreateProgramFromBinary(pContext, &device, "simple_block_kernel", "-cl-std=CL2.0");
        auto mockProgram = (MockProgram *)pProgram;
        ASSERT_NE(nullptr, mockProgram);

        retVal = mockProgram->build(
            1,
            &device,
            nullptr,
            nullptr,
            nullptr,
            false);
        EXPECT_EQ(CL_SUCCESS, retVal);

        auto kernelInfo = mockProgram->Program::getKernelInfo("simple_block_kernel");
        EXPECT_NE(nullptr, kernelInfo);

        auto blockKernelInfo = mockProgram->Program::getKernelInfo("simple_block_kernel_dispatch_0");
        EXPECT_EQ(nullptr, blockKernelInfo);

        std::vector<const KernelInfo *> blockKernelInfos(mockProgram->blockKernelManager->getCount());

        for (size_t i = 0; i < mockProgram->blockKernelManager->getCount(); i++) {
            const KernelInfo *blockKernelInfo = mockProgram->blockKernelManager->getBlockKernelInfo(i);
            EXPECT_NE(nullptr, blockKernelInfo);
            blockKernelInfos[i] = blockKernelInfo;
        }

        bool blockKernelFound = false;
        for (size_t i = 0; i < mockProgram->blockKernelManager->getCount(); i++) {
            if (blockKernelInfos[i]->name.find("simple_block_kernel_dispatch") != std::string::npos) {
                blockKernelFound = true;
                break;
            }
        }

        EXPECT_TRUE(blockKernelFound);

    } else {
        EXPECT_EQ(nullptr, pProgram);
    }
}

TEST_F(ProgramWithBlockKernelsTest, GivenKernelWithBlockKernelsWhenProgramIsLinkedThenBlockKernelsAreSeparated) {
    if (std::string(pPlatform->getClDevice(0)->getDeviceInfo().clVersion).find("OpenCL 2.0") != std::string::npos) {
        CreateProgramFromBinary(pContext, &device, "simple_block_kernel", "-cl-std=CL2.0");
        const char *buildOptions = "-cl-std=CL2.0";

        overwriteBuiltInBinaryName(
            pPlatform->getDevice(0),
            "simple_block_kernel", true);

        ASSERT_NE(nullptr, pProgram);

        EXPECT_EQ(CL_SUCCESS, retVal);
        Program *programLinked = new Program(*pPlatform->peekExecutionEnvironment(), pContext, false, nullptr);
        cl_program program = pProgram;

        retVal = pProgram->compile(1, &device, buildOptions, 0, nullptr, nullptr, nullptr, nullptr);

        EXPECT_EQ(CL_SUCCESS, retVal);

        retVal = programLinked->link(1, &device, buildOptions, 1, &program, nullptr, nullptr);
        EXPECT_EQ(CL_SUCCESS, retVal);

        BlockKernelManager *blockManager = programLinked->getBlockKernelManager();

        EXPECT_NE(0u, blockManager->getCount());

        for (uint32_t i = 0; i < blockManager->getCount(); i++) {
            const KernelInfo *info = blockManager->getBlockKernelInfo(i);
            if (info->name.find("simple_block_kernel_dispatch") != std::string::npos) {
                break;
            }
        }
        restoreBuiltInBinaryName(nullptr);
        delete programLinked;
    } else {
        EXPECT_EQ(nullptr, pProgram);
    }
}
} // namespace NEO
