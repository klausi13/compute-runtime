/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/os_interface/linux/drm_memory_operations_handler.h"
#include "shared/source/os_interface/linux/os_interface.h"

#include "opencl/test/unit_test/fixtures/memory_management_fixture.h"
#include "opencl/test/unit_test/mocks/linux/mock_drm_memory_manager.h"
#include "opencl/test/unit_test/mocks/mock_device.h"
#include "opencl/test/unit_test/mocks/mock_execution_environment.h"
#include "opencl/test/unit_test/os_interface/linux/device_command_stream_fixture.h"

#include <memory>

namespace NEO {

using AllocationData = TestedDrmMemoryManager::AllocationData;

class DrmMemoryManagerBasic : public ::testing::Test {
  public:
    DrmMemoryManagerBasic() : executionEnvironment(*platformDevices){};
    void SetUp() override {
        executionEnvironment.rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
        executionEnvironment.rootDeviceEnvironments[0]->osInterface->get()->setDrm(Drm::create(nullptr, *executionEnvironment.rootDeviceEnvironments[0]));
        executionEnvironment.rootDeviceEnvironments[0]->memoryOperationsInterface = std::make_unique<DrmMemoryOperationsHandler>();
    }

    MockExecutionEnvironment executionEnvironment;
};

class DrmMemoryManagerFixture : public MemoryManagementFixture {
  public:
    DrmMockCustom *mock;
    TestedDrmMemoryManager *memoryManager = nullptr;
    MockClDevice *device = nullptr;

    void SetUp() override {
        MemoryManagementFixture::SetUp();
        SetUp(new DrmMockCustom, false);
    }

    void SetUp(DrmMockCustom *mock, bool localMemoryEnabled) {
        this->mock = mock;
        executionEnvironment = new MockExecutionEnvironment(*platformDevices);
        executionEnvironment->incRefInternal();
        rootDeviceEnvironment = executionEnvironment->rootDeviceEnvironments[0].get();
        rootDeviceEnvironment->osInterface = std::make_unique<OSInterface>();
        rootDeviceEnvironment->osInterface->get()->setDrm(mock);

        memoryManager = new (std::nothrow) TestedDrmMemoryManager(localMemoryEnabled, false, false, *executionEnvironment);
        //assert we have memory manager
        ASSERT_NE(nullptr, memoryManager);
        if (memoryManager->getgemCloseWorker()) {
            memoryManager->getgemCloseWorker()->close(true);
        }
        device = new MockClDevice{MockDevice::createWithExecutionEnvironment<MockDevice>(*platformDevices, executionEnvironment, 0)};
    }

    void TearDown() override {
        delete device;
        delete memoryManager;
        this->mock->testIoctls();
        executionEnvironment->decRefInternal();

        MemoryManagementFixture::TearDown();
    }

  protected:
    ExecutionEnvironment *executionEnvironment;
    RootDeviceEnvironment *rootDeviceEnvironment = nullptr;
    DrmMockCustom::IoctlResExt ioctlResExt = {0, 0};
    AllocationData allocationData;
};

class DrmMemoryManagerWithLocalMemoryFixture : public DrmMemoryManagerFixture {
  public:
    void SetUp() override {
        MemoryManagementFixture::SetUp();
        DrmMemoryManagerFixture::SetUp(new DrmMockCustom, true);
    }
    void TearDown() override {
        DrmMemoryManagerFixture::TearDown();
    }
};

class DrmMemoryManagerFixtureWithoutQuietIoctlExpectation {
  public:
    std::unique_ptr<TestedDrmMemoryManager> memoryManager;
    DrmMockCustom *mock;

    void SetUp() {
        executionEnvironment = new ExecutionEnvironment;
        executionEnvironment->prepareRootDeviceEnvironments(1);
        executionEnvironment->rootDeviceEnvironments[0]->setHwInfo(*platformDevices);
        mock = new DrmMockCustom();
        executionEnvironment->rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
        executionEnvironment->rootDeviceEnvironments[0]->osInterface->get()->setDrm(mock);
        memoryManager.reset(new TestedDrmMemoryManager(*executionEnvironment));

        ASSERT_NE(nullptr, memoryManager);
        if (memoryManager->getgemCloseWorker()) {
            memoryManager->getgemCloseWorker()->close(true);
        }
        device.reset(MockDevice::createWithExecutionEnvironment<MockDevice>(*platformDevices, executionEnvironment, 0));
    }

    void TearDown() {
    }

  protected:
    ExecutionEnvironment *executionEnvironment = nullptr;
    std::unique_ptr<MockDevice> device;
    DrmMockCustom::IoctlResExt ioctlResExt = {0, 0};
};
} // namespace NEO
