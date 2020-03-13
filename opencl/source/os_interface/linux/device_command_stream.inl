/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/device_command_stream.h"

#include "opencl/source/command_stream/command_stream_receiver_with_aub_dump.h"
#include "opencl/source/os_interface/linux/drm_command_stream.h"

namespace NEO {

template <typename GfxFamily>
CommandStreamReceiver *DeviceCommandStreamReceiver<GfxFamily>::create(bool withAubDump, ExecutionEnvironment &executionEnvironment, uint32_t rootDeviceIndex) {
    if (withAubDump) {
        return new CommandStreamReceiverWithAUBDump<DrmCommandStreamReceiver<GfxFamily>>("aubfile", executionEnvironment, rootDeviceIndex);
    } else {
        return new DrmCommandStreamReceiver<GfxFamily>(executionEnvironment, rootDeviceIndex);
    }
};
} // namespace NEO
