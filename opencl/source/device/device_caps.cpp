/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/device/device.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/hw_info_config.h"
#include "shared/source/os_interface/os_interface.h"
#include "shared/source/source_level_debugger/source_level_debugger.h"

#include "opencl/source/device/cl_device.h"
#include "opencl/source/device/driver_info.h"
#include "opencl/source/platform/extensions.h"
#include "opencl/source/sharings/sharing_factory.h"

#include "CL/cl_ext_intel.h"
#include "driver_version.h"

#include <algorithm>

namespace NEO {
extern const char *familyName[];

static std::string vendor = "Intel(R) Corporation";
static std::string profile = "FULL_PROFILE";
static std::string spirVersions = "1.2 ";
static const char *spirvVersion = "SPIR-V_1.2 ";
#define QTR(a) #a
#define TOSTR(b) QTR(b)
static std::string driverVersion = TOSTR(NEO_DRIVER_VERSION);

const char *builtInKernels = ""; // the "always available" (extension-independent) builtin kernels

static constexpr cl_device_fp_config defaultFpFlags = static_cast<cl_device_fp_config>(CL_FP_ROUND_TO_NEAREST |
                                                                                       CL_FP_ROUND_TO_ZERO |
                                                                                       CL_FP_ROUND_TO_INF |
                                                                                       CL_FP_INF_NAN |
                                                                                       CL_FP_DENORM |
                                                                                       CL_FP_FMA);

bool releaseFP64Override();

void ClDevice::setupFp64Flags() {
    auto &hwInfo = getHardwareInfo();

    if (releaseFP64Override() || DebugManager.flags.OverrideDefaultFP64Settings.get() == 1) {
        deviceExtensions += "cl_khr_fp64 ";
        deviceInfo.singleFpConfig = static_cast<cl_device_fp_config>(CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT);
        deviceInfo.doubleFpConfig = defaultFpFlags;
    } else if (DebugManager.flags.OverrideDefaultFP64Settings.get() == -1) {
        if (hwInfo.capabilityTable.ftrSupportsFP64) {
            deviceExtensions += "cl_khr_fp64 ";
        }

        deviceInfo.singleFpConfig = static_cast<cl_device_fp_config>(
            hwInfo.capabilityTable.ftrSupports64BitMath
                ? CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT
                : 0);

        deviceInfo.doubleFpConfig = hwInfo.capabilityTable.ftrSupportsFP64
                                        ? defaultFpFlags
                                        : 0;
    }
}

void Device::initializeCaps() {
    auto &hwInfo = getHardwareInfo();
    auto hwInfoConfig = HwInfoConfig::get(hwInfo.platform.eProductFamily);
    auto addressing32bitAllowed = is64bit;

    auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    deviceInfo.ilVersion = "";
    auto enabledClVersion = hwInfo.capabilityTable.clVersionSupport;
    if (DebugManager.flags.ForceOCLVersion.get() != 0) {
        enabledClVersion = DebugManager.flags.ForceOCLVersion.get();
    }
    switch (enabledClVersion) {
    case 21:
        deviceInfo.ilVersion = spirvVersion;
        addressing32bitAllowed = false;
        break;
    case 20:
        addressing32bitAllowed = false;
        break;
    }

    deviceInfo.vendorId = 0x8086;
    deviceInfo.maxReadImageArgs = 128;
    deviceInfo.maxWriteImageArgs = 128;
    deviceInfo.maxParameterSize = 1024;

    deviceInfo.addressBits = 64;

    //copy system info to prevent misaligned reads
    const auto systemInfo = hwInfo.gtSystemInfo;

    deviceInfo.globalMemCachelineSize = 64;

    deviceInfo.globalMemSize = getMemoryManager()->isLocalMemorySupported(this->getRootDeviceIndex())
                                   ? getMemoryManager()->getLocalMemorySize(this->getRootDeviceIndex())
                                   : getMemoryManager()->getSystemSharedMemory(this->getRootDeviceIndex());
    deviceInfo.globalMemSize = std::min(deviceInfo.globalMemSize, getMemoryManager()->getMaxApplicationAddress() + 1);
    deviceInfo.globalMemSize = static_cast<uint64_t>(static_cast<double>(deviceInfo.globalMemSize) * 0.8);

    if (DebugManager.flags.Force32bitAddressing.get() || addressing32bitAllowed || is32bit) {
        deviceInfo.globalMemSize = std::min(deviceInfo.globalMemSize, static_cast<uint64_t>(4 * GB * 0.8));
        deviceInfo.addressBits = 32;
        deviceInfo.force32BitAddressess = is64bit;
    }

    deviceInfo.globalMemSize = alignDown(deviceInfo.globalMemSize, MemoryConstants::pageSize);

    deviceInfo.profilingTimerResolution = getProfilingTimerResolution();
    deviceInfo.outProfilingTimerResolution = static_cast<size_t>(deviceInfo.profilingTimerResolution);

    deviceInfo.maxOnDeviceQueues = 1;

    // OpenCL 1.2 requires 128MB minimum
    deviceInfo.maxMemAllocSize = std::min(std::max(deviceInfo.globalMemSize / 2, static_cast<uint64_t>(128llu * MB)), this->hardwareCapabilities.maxMemAllocSize);

    static const int maxPixelSize = 16;
    deviceInfo.imageMaxBufferSize = static_cast<size_t>(deviceInfo.maxMemAllocSize / maxPixelSize);

    deviceInfo.maxNumEUsPerSubSlice = 0;
    deviceInfo.numThreadsPerEU = 0;
    auto simdSizeUsed = DebugManager.flags.UseMaxSimdSizeToDeduceMaxWorkgroupSize.get() ? 32u : hwHelper.getMinimalSIMDSize();

    deviceInfo.maxNumEUsPerSubSlice = (systemInfo.EuCountPerPoolMin == 0 || hwInfo.featureTable.ftrPooledEuEnabled == 0)
                                          ? (systemInfo.EUCount / systemInfo.SubSliceCount)
                                          : systemInfo.EuCountPerPoolMin;
    deviceInfo.numThreadsPerEU = systemInfo.ThreadCount / systemInfo.EUCount;
    auto maxWS = hwHelper.getMaxThreadsForWorkgroup(hwInfo, static_cast<uint32_t>(deviceInfo.maxNumEUsPerSubSlice)) * simdSizeUsed;

    maxWS = Math::prevPowerOfTwo(maxWS);
    deviceInfo.maxWorkGroupSize = std::min(maxWS, 1024u);

    if (DebugManager.flags.OverrideMaxWorkgroupSize.get() != -1) {
        deviceInfo.maxWorkGroupSize = DebugManager.flags.OverrideMaxWorkgroupSize.get();
    }

    deviceInfo.maxWorkItemSizes[0] = deviceInfo.maxWorkGroupSize;
    deviceInfo.maxWorkItemSizes[1] = deviceInfo.maxWorkGroupSize;
    deviceInfo.maxWorkItemSizes[2] = deviceInfo.maxWorkGroupSize;
    deviceInfo.maxSamplers = 16;

    deviceInfo.computeUnitsUsedForScratch = hwHelper.getComputeUnitsUsedForScratch(&hwInfo);
    deviceInfo.maxFrontEndThreads = HwHelper::getMaxThreadsForVfe(hwInfo);

    deviceInfo.localMemSize = hwInfo.capabilityTable.slmSize * KB;

    deviceInfo.imageSupport = hwInfo.capabilityTable.supportsImages;
    deviceInfo.image2DMaxWidth = 16384;
    deviceInfo.image2DMaxHeight = 16384;
    deviceInfo.image3DMaxDepth = 2048;
    deviceInfo.imageMaxArraySize = 2048;

    deviceInfo.printfBufferSize = 4 * 1024 * 1024;
    deviceInfo.maxClockFrequency = hwInfo.capabilityTable.maxRenderFrequency;

    deviceInfo.maxSubGroups = hwHelper.getDeviceSubGroupSizes();

    deviceInfo.vmeAvcSupportsPreemption = hwInfo.capabilityTable.ftrSupportsVmeAvcPreemption;

    deviceInfo.debuggerActive = (executionEnvironment->debugger) ? executionEnvironment->debugger->isDebuggerActive() : false;
    if (deviceInfo.debuggerActive) {
        this->preemptionMode = PreemptionMode::Disabled;
    }

    deviceInfo.sharedSystemAllocationsSupport = hwInfoConfig->getSharedSystemMemCapabilities();
    if (DebugManager.flags.EnableSharedSystemUsmSupport.get() != -1) {
        deviceInfo.sharedSystemAllocationsSupport = DebugManager.flags.EnableSharedSystemUsmSupport.get();
    }
}

void ClDevice::copyCommonCapsFromDevice() {
    auto &sourceDeviceInfo = device.getDeviceInfo();

    deviceInfo.maxSubGroups = sourceDeviceInfo.maxSubGroups;
    deviceInfo.debuggerActive = sourceDeviceInfo.debuggerActive;
    deviceInfo.errorCorrectionSupport = sourceDeviceInfo.errorCorrectionSupport;
    deviceInfo.force32BitAddressess = sourceDeviceInfo.force32BitAddressess;
    deviceInfo.imageSupport = sourceDeviceInfo.imageSupport;
    deviceInfo.vmeAvcSupportsPreemption = sourceDeviceInfo.vmeAvcSupportsPreemption;
    deviceInfo.ilVersion = sourceDeviceInfo.ilVersion;
    deviceInfo.profilingTimerResolution = sourceDeviceInfo.profilingTimerResolution;
    deviceInfo.addressBits = sourceDeviceInfo.addressBits;
    deviceInfo.computeUnitsUsedForScratch = sourceDeviceInfo.computeUnitsUsedForScratch;
    deviceInfo.globalMemCachelineSize = sourceDeviceInfo.globalMemCachelineSize;
    deviceInfo.maxClockFrequency = sourceDeviceInfo.maxClockFrequency;
    deviceInfo.maxFrontEndThreads = sourceDeviceInfo.maxFrontEndThreads;
    deviceInfo.maxOnDeviceQueues = sourceDeviceInfo.maxOnDeviceQueues;
    deviceInfo.maxReadImageArgs = sourceDeviceInfo.maxReadImageArgs;
    deviceInfo.maxSamplers = sourceDeviceInfo.maxSamplers;
    deviceInfo.maxWriteImageArgs = sourceDeviceInfo.maxWriteImageArgs;
    deviceInfo.numThreadsPerEU = sourceDeviceInfo.numThreadsPerEU;
    deviceInfo.vendorId = sourceDeviceInfo.vendorId;
    deviceInfo.globalMemSize = sourceDeviceInfo.globalMemSize;
    deviceInfo.image2DMaxHeight = static_cast<size_t>(sourceDeviceInfo.image2DMaxHeight);
    deviceInfo.image2DMaxWidth = static_cast<size_t>(sourceDeviceInfo.image2DMaxWidth);
    deviceInfo.image3DMaxDepth = static_cast<size_t>(sourceDeviceInfo.image3DMaxDepth);
    deviceInfo.imageMaxArraySize = static_cast<size_t>(sourceDeviceInfo.imageMaxArraySize);
    deviceInfo.imageMaxBufferSize = static_cast<size_t>(sourceDeviceInfo.imageMaxBufferSize);
    deviceInfo.localMemSize = sourceDeviceInfo.localMemSize;
    deviceInfo.maxMemAllocSize = sourceDeviceInfo.maxMemAllocSize;
    deviceInfo.maxNumEUsPerSubSlice = static_cast<size_t>(sourceDeviceInfo.maxNumEUsPerSubSlice);
    deviceInfo.maxParameterSize = static_cast<size_t>(sourceDeviceInfo.maxParameterSize);
    deviceInfo.maxWorkGroupSize = static_cast<size_t>(sourceDeviceInfo.maxWorkGroupSize);
    deviceInfo.maxWorkItemSizes[0] = static_cast<size_t>(sourceDeviceInfo.maxWorkItemSizes[0]);
    deviceInfo.maxWorkItemSizes[1] = static_cast<size_t>(sourceDeviceInfo.maxWorkItemSizes[1]);
    deviceInfo.maxWorkItemSizes[2] = static_cast<size_t>(sourceDeviceInfo.maxWorkItemSizes[2]);
    deviceInfo.outProfilingTimerResolution = static_cast<size_t>(sourceDeviceInfo.outProfilingTimerResolution);
    deviceInfo.printfBufferSize = static_cast<size_t>(sourceDeviceInfo.printfBufferSize);
}

void ClDevice::initializeCaps() {
    copyCommonCapsFromDevice();

    auto &hwInfo = getHardwareInfo();
    auto hwInfoConfig = HwInfoConfig::get(hwInfo.platform.eProductFamily);
    deviceExtensions.clear();
    deviceExtensions.append(deviceExtensionsList);

    driverVersion = TOSTR(NEO_DRIVER_VERSION);

    // Add our graphics family name to the device name
    name += "Intel(R) ";
    name += familyName[hwInfo.platform.eRenderCoreFamily];
    name += " HD Graphics NEO";

    if (driverInfo) {
        name.assign(driverInfo.get()->getDeviceName(name).c_str());
        driverVersion.assign(driverInfo.get()->getVersion(driverVersion).c_str());
    }

    auto &hwHelper = HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    deviceInfo.name = name.c_str();
    deviceInfo.driverVersion = driverVersion.c_str();

    setupFp64Flags();

    deviceInfo.vendor = vendor.c_str();
    deviceInfo.profile = profile.c_str();
    enabledClVersion = hwInfo.capabilityTable.clVersionSupport;
    if (DebugManager.flags.ForceOCLVersion.get() != 0) {
        enabledClVersion = DebugManager.flags.ForceOCLVersion.get();
    }
    switch (enabledClVersion) {
    case 21:
        deviceInfo.clVersion = "OpenCL 2.1 NEO ";
        deviceInfo.clCVersion = "OpenCL C 2.0 ";
        break;
    case 20:
        deviceInfo.clVersion = "OpenCL 2.0 NEO ";
        deviceInfo.clCVersion = "OpenCL C 2.0 ";
        break;
    case 12:
    default:
        deviceInfo.clVersion = "OpenCL 1.2 NEO ";
        deviceInfo.clCVersion = "OpenCL C 1.2 ";
        break;
    }
    deviceInfo.platformLP = (hwInfo.capabilityTable.clVersionSupport == 12) ? true : false;
    deviceInfo.spirVersions = spirVersions.c_str();
    auto supportsVme = hwInfo.capabilityTable.supportsVme;
    auto supportsAdvancedVme = hwInfo.capabilityTable.supportsVme;

    if (enabledClVersion >= 21) {
        deviceInfo.independentForwardProgress = true;
        deviceExtensions += "cl_khr_subgroups ";
        deviceExtensions += "cl_khr_il_program ";
        if (supportsVme) {
            deviceExtensions += "cl_intel_spirv_device_side_avc_motion_estimation ";
        }
        if (hwInfo.capabilityTable.supportsImages) {
            deviceExtensions += "cl_intel_spirv_media_block_io ";
        }
        deviceExtensions += "cl_intel_spirv_subgroups ";
        deviceExtensions += "cl_khr_spirv_no_integer_wrap_decoration ";
    } else {
        deviceInfo.independentForwardProgress = false;
    }

    if (enabledClVersion >= 20) {
        deviceExtensions += "cl_intel_unified_shared_memory_preview ";
        if (hwInfo.capabilityTable.supportsImages) {
            deviceExtensions += "cl_khr_mipmap_image cl_khr_mipmap_image_writes ";
        }
    }

    if (DebugManager.flags.EnableNV12.get() && hwInfo.capabilityTable.supportsImages) {
        deviceExtensions += "cl_intel_planar_yuv ";
        deviceInfo.nv12Extension = true;
    }
    if (DebugManager.flags.EnablePackedYuv.get() && hwInfo.capabilityTable.supportsImages) {
        deviceExtensions += "cl_intel_packed_yuv ";
        deviceInfo.packedYuvExtension = true;
    }
    if (DebugManager.flags.EnableIntelVme.get() != -1) {
        supportsVme = !!DebugManager.flags.EnableIntelVme.get();
    }

    if (supportsVme) {
        deviceExtensions += "cl_intel_motion_estimation cl_intel_device_side_avc_motion_estimation ";
        deviceInfo.vmeExtension = true;
    }

    if (DebugManager.flags.EnableIntelAdvancedVme.get() != -1) {
        supportsAdvancedVme = !!DebugManager.flags.EnableIntelAdvancedVme.get();
    }
    if (supportsAdvancedVme) {
        deviceExtensions += "cl_intel_advanced_motion_estimation ";
    }

    if (hwInfo.capabilityTable.ftrSupportsInteger64BitAtomics) {
        deviceExtensions += "cl_khr_int64_base_atomics ";
        deviceExtensions += "cl_khr_int64_extended_atomics ";
    }

    if (hwInfo.capabilityTable.supportsImages) {
        deviceExtensions += "cl_khr_image2d_from_buffer ";
        deviceExtensions += "cl_khr_depth_images ";
        deviceExtensions += "cl_intel_media_block_io ";
        deviceExtensions += "cl_khr_3d_image_writes ";
    }

    auto sharingAllowed = (HwHelper::getSubDevicesCount(&hwInfo) == 1u);
    if (sharingAllowed) {
        deviceExtensions += sharingFactory.getExtensions();
    }

    deviceExtensions += hwHelper.getExtensions();

    deviceInfo.deviceExtensions = deviceExtensions.c_str();

    exposedBuiltinKernels = builtInKernels;

    if (supportsVme) {
        exposedBuiltinKernels.append("block_motion_estimate_intel;");
    }
    if (supportsAdvancedVme) {
        auto advVmeKernels = "block_advanced_motion_estimate_check_intel;block_advanced_motion_estimate_bidirectional_check_intel;";
        exposedBuiltinKernels.append(advVmeKernels);
    }

    deviceInfo.builtInKernels = exposedBuiltinKernels.c_str();

    deviceInfo.deviceType = CL_DEVICE_TYPE_GPU;
    deviceInfo.endianLittle = 1;
    deviceInfo.hostUnifiedMemory = (false == hwHelper.isLocalMemoryEnabled(hwInfo));
    deviceInfo.deviceAvailable = CL_TRUE;
    deviceInfo.compilerAvailable = CL_TRUE;
    deviceInfo.parentDevice = nullptr;
    deviceInfo.partitionMaxSubDevices = HwHelper::getSubDevicesCount(&hwInfo);
    if (deviceInfo.partitionMaxSubDevices > 1) {
        deviceInfo.partitionProperties[0] = CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN;
        deviceInfo.partitionProperties[1] = 0;
        deviceInfo.partitionAffinityDomain = CL_DEVICE_AFFINITY_DOMAIN_NUMA | CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE;
    } else {
        deviceInfo.partitionMaxSubDevices = 0;
        deviceInfo.partitionProperties[0] = 0;
        deviceInfo.partitionAffinityDomain = 0;
    }
    deviceInfo.partitionType[0] = 0;
    deviceInfo.preferredVectorWidthChar = 16;
    deviceInfo.preferredVectorWidthShort = 8;
    deviceInfo.preferredVectorWidthInt = 4;
    deviceInfo.preferredVectorWidthLong = 1;
    deviceInfo.preferredVectorWidthFloat = 1;
    deviceInfo.preferredVectorWidthDouble = 1;
    deviceInfo.preferredVectorWidthHalf = 8;
    deviceInfo.nativeVectorWidthChar = 16;
    deviceInfo.nativeVectorWidthShort = 8;
    deviceInfo.nativeVectorWidthInt = 4;
    deviceInfo.nativeVectorWidthLong = 1;
    deviceInfo.nativeVectorWidthFloat = 1;
    deviceInfo.nativeVectorWidthDouble = 1;
    deviceInfo.nativeVectorWidthHalf = 8;
    deviceInfo.maxReadWriteImageArgs = 128;
    deviceInfo.executionCapabilities = CL_EXEC_KERNEL;

    //copy system info to prevent misaligned reads
    const auto systemInfo = hwInfo.gtSystemInfo;

    deviceInfo.globalMemCacheSize = systemInfo.L3BankCount * 128 * KB;
    deviceInfo.grfSize = hwInfo.capabilityTable.grfSize;

    deviceInfo.globalMemCacheType = CL_READ_WRITE_CACHE;
    deviceInfo.memBaseAddressAlign = 1024;
    deviceInfo.minDataTypeAlignSize = 128;

    deviceInfo.maxOnDeviceEvents = 1024;
    deviceInfo.queueOnDeviceMaxSize = 64 * MB;
    deviceInfo.queueOnDevicePreferredSize = 128 * KB;
    deviceInfo.queueOnDeviceProperties = CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;

    deviceInfo.preferredInteropUserSync = 1u;

    // OpenCL 1.2 requires 128MB minimum

    deviceInfo.maxConstantBufferSize = deviceInfo.maxMemAllocSize;

    deviceInfo.maxWorkItemDimensions = 3;

    deviceInfo.maxComputUnits = systemInfo.EUCount;
    deviceInfo.maxConstantArgs = 8;
    deviceInfo.maxSliceCount = systemInfo.SliceCount;
    auto simdSizeUsed = DebugManager.flags.UseMaxSimdSizeToDeduceMaxWorkgroupSize.get() ? 32u : hwHelper.getMinimalSIMDSize();

    // calculate a maximum number of subgroups in a workgroup (for the required SIMD size)
    deviceInfo.maxNumOfSubGroups = static_cast<uint32_t>(deviceInfo.maxWorkGroupSize / simdSizeUsed);

    deviceInfo.singleFpConfig |= defaultFpFlags;

    deviceInfo.halfFpConfig = defaultFpFlags;

    printDebugString(DebugManager.flags.PrintDebugMessages.get(), stderr, "computeUnitsUsedForScratch: %d\n", deviceInfo.computeUnitsUsedForScratch);

    printDebugString(DebugManager.flags.PrintDebugMessages.get(), stderr, "hwInfo: {%d, %d}: (%d, %d, %d)\n",
                     systemInfo.EUCount,
                     systemInfo.ThreadCount,
                     systemInfo.MaxEuPerSubSlice,
                     systemInfo.MaxSlicesSupported,
                     systemInfo.MaxSubSlicesSupported);

    deviceInfo.localMemType = CL_LOCAL;

    deviceInfo.image3DMaxWidth = this->getHardwareCapabilities().image3DMaxWidth;
    deviceInfo.image3DMaxHeight = this->getHardwareCapabilities().image3DMaxHeight;

    // cl_khr_image2d_from_buffer
    deviceInfo.imagePitchAlignment = hwHelper.getPitchAlignmentForImage(&hwInfo);
    deviceInfo.imageBaseAddressAlignment = 4;
    deviceInfo.maxPipeArgs = 16;
    deviceInfo.pipeMaxPacketSize = 1024;
    deviceInfo.pipeMaxActiveReservations = 1;
    deviceInfo.queueOnHostProperties = CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;

    deviceInfo.linkerAvailable = true;
    deviceInfo.svmCapabilities = hwInfo.capabilityTable.ftrSvm * CL_DEVICE_SVM_COARSE_GRAIN_BUFFER;
    deviceInfo.svmCapabilities |= static_cast<cl_device_svm_capabilities>(
        hwInfo.capabilityTable.ftrSvm * hwInfo.capabilityTable.ftrSupportsCoherency *
        (CL_DEVICE_SVM_FINE_GRAIN_BUFFER | CL_DEVICE_SVM_ATOMICS));
    deviceInfo.preemptionSupported = false;
    deviceInfo.maxGlobalVariableSize = 64 * 1024;
    deviceInfo.globalVariablePreferredTotalSize = static_cast<size_t>(deviceInfo.maxMemAllocSize);

    deviceInfo.planarYuvMaxWidth = 16384;
    deviceInfo.planarYuvMaxHeight = 16352;

    deviceInfo.vmeAvcSupportsTextureSampler = hwInfo.capabilityTable.ftrSupportsVmeAvcTextureSampler;
    if (hwInfo.capabilityTable.supportsVme) {
        deviceInfo.vmeAvcVersion = CL_AVC_ME_VERSION_1_INTEL;
        deviceInfo.vmeVersion = CL_ME_VERSION_ADVANCED_VER_2_INTEL;
    }
    deviceInfo.platformHostTimerResolution = getPlatformHostTimerResolution();

    deviceInfo.internalDriverVersion = CL_DEVICE_DRIVER_VERSION_INTEL_NEO1;

    deviceInfo.preferredGlobalAtomicAlignment = MemoryConstants::cacheLineSize;
    deviceInfo.preferredLocalAtomicAlignment = MemoryConstants::cacheLineSize;
    deviceInfo.preferredPlatformAtomicAlignment = MemoryConstants::cacheLineSize;

    deviceInfo.hostMemCapabilities = hwInfoConfig->getHostMemCapabilities();
    deviceInfo.deviceMemCapabilities = hwInfoConfig->getDeviceMemCapabilities();
    deviceInfo.singleDeviceSharedMemCapabilities = hwInfoConfig->getSingleDeviceSharedMemCapabilities();
    deviceInfo.crossDeviceSharedMemCapabilities = hwInfoConfig->getCrossDeviceSharedMemCapabilities();
    deviceInfo.sharedSystemMemCapabilities = hwInfoConfig->getSharedSystemMemCapabilities();
    if (DebugManager.flags.EnableSharedSystemUsmSupport.get() != -1) {
        if (DebugManager.flags.EnableSharedSystemUsmSupport.get() == 0) {
            deviceInfo.sharedSystemMemCapabilities = 0u;
        } else {
            deviceInfo.sharedSystemMemCapabilities = CL_UNIFIED_SHARED_MEMORY_ACCESS_INTEL | CL_UNIFIED_SHARED_MEMORY_ATOMIC_ACCESS_INTEL | CL_UNIFIED_SHARED_MEMORY_CONCURRENT_ACCESS_INTEL | CL_UNIFIED_SHARED_MEMORY_CONCURRENT_ATOMIC_ACCESS_INTEL;
        }
    }

    initializeExtraCaps();
}

} // namespace NEO
