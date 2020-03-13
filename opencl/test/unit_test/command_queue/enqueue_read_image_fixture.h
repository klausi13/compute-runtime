/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/ptr_math.h"

#include "opencl/test/unit_test/command_queue/command_enqueue_fixture.h"
#include "opencl/test/unit_test/command_queue/enqueue_fixture.h"
#include "opencl/test/unit_test/fixtures/image_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"

#include "gtest/gtest.h"

namespace NEO {

struct EnqueueReadImageTest : public CommandEnqueueFixture,
                              public ::testing::Test {
    typedef CommandQueueHwFixture CommandQueueFixture;
    using CommandQueueHwFixture::pCmdQ;

    EnqueueReadImageTest() : dstPtr(nullptr),
                             srcImage(nullptr) {
    }

    void SetUp(void) override {
        CommandEnqueueFixture::SetUp();

        context = new MockContext(pClDevice);
        srcImage = Image2dHelper<>::create(context);
        const auto &imageDesc = srcImage->getImageDesc();
        dstPtr = new float[imageDesc.image_width * imageDesc.image_height];
    }

    void TearDown(void) override {
        delete srcImage;
        delete[] dstPtr;
        delete context;
        CommandEnqueueFixture::TearDown();
    }

  protected:
    template <typename FamilyType>
    void enqueueReadImage(cl_bool blocking = EnqueueReadImageTraits::blocking) {
        auto retVal = EnqueueReadImageHelper<>::enqueueReadImage(pCmdQ,
                                                                 srcImage,
                                                                 blocking);
        EXPECT_EQ(CL_SUCCESS, retVal);
        parseCommands<FamilyType>(*pCmdQ);
    }

    float *dstPtr;
    Image *srcImage;
    MockContext *context;
};

struct EnqueueReadImageMipMapTest : public EnqueueReadImageTest,
                                    public ::testing::WithParamInterface<uint32_t> {
};
} // namespace NEO
