/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/test/unit_test/command_queue/command_enqueue_fixture.h"
#include "opencl/test/unit_test/command_queue/enqueue_fixture.h"
#include "opencl/test/unit_test/fixtures/image_fixture.h"

#include "gtest/gtest.h"

namespace NEO {

struct EnqueueFillImageTestFixture : public CommandEnqueueFixture {

    EnqueueFillImageTestFixture() : image(nullptr) {
    }

    void SetUp(void) override {
        CommandEnqueueFixture::SetUp();
        context = new MockContext(pClDevice);
        image = Image2dHelper<>::create(context);
    }

    void TearDown(void) override {
        delete image;
        delete context;
        CommandEnqueueFixture::TearDown();
    }

  protected:
    template <typename FamilyType>
    void enqueueFillImage() {
        auto retVal = EnqueueFillImageHelper<>::enqueueFillImage(pCmdQ,
                                                                 image);
        EXPECT_EQ(CL_SUCCESS, retVal);
        parseCommands<FamilyType>(*pCmdQ);
    }

    MockContext *context;
    Image *image;
};
} // namespace NEO
