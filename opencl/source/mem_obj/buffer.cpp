/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/mem_obj/buffer.h"

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/gmm_helper/gmm.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/source/helpers/string.h"
#include "shared/source/helpers/timestamp_packet.h"
#include "shared/source/memory_manager/host_ptr_manager.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/memory_manager/memory_operations_handler.h"
#include "shared/source/memory_manager/unified_memory_manager.h"

#include "opencl/source/command_queue/command_queue.h"
#include "opencl/source/context/context.h"
#include "opencl/source/device/cl_device.h"
#include "opencl/source/helpers/memory_properties_flags_helpers.h"
#include "opencl/source/helpers/validators.h"
#include "opencl/source/mem_obj/mem_obj_helper.h"

namespace NEO {

BufferFuncs bufferFactory[IGFX_MAX_CORE] = {};

Buffer::Buffer(Context *context,
               MemoryPropertiesFlags memoryProperties,
               cl_mem_flags flags,
               cl_mem_flags_intel flagsIntel,
               size_t size,
               void *memoryStorage,
               void *hostPtr,
               GraphicsAllocation *gfxAllocation,
               bool zeroCopy,
               bool isHostPtrSVM,
               bool isObjectRedescribed)
    : MemObj(context,
             CL_MEM_OBJECT_BUFFER,
             memoryProperties,
             flags,
             flagsIntel,
             size,
             memoryStorage,
             hostPtr,
             gfxAllocation,
             zeroCopy,
             isHostPtrSVM,
             isObjectRedescribed) {
    magic = objectMagic;
    setHostPtrMinSize(size);
}

Buffer::Buffer() : MemObj(nullptr, CL_MEM_OBJECT_BUFFER, {}, 0, 0, 0, nullptr, nullptr, nullptr, false, false, false) {
}

Buffer::~Buffer() = default;

bool Buffer::isSubBuffer() {
    return this->associatedMemObject != nullptr;
}

bool Buffer::isValidSubBufferOffset(size_t offset) {
    if (this->getGraphicsAllocation()->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED) {
        // From spec: "origin value is aligned to the CL_DEVICE_MEM_BASE_ADDR_ALIGN value"
        if (!isAligned(offset, this->getContext()->getDevice(0)->getDeviceInfo().memBaseAddressAlign / 8u)) {
            return false;
        }
    }
    cl_uint address_align = 32; // 4 byte alignment
    if ((offset & (address_align / 8 - 1)) == 0) {
        return true;
    }

    return false;
}

void Buffer::validateInputAndCreateBuffer(cl_context &context,
                                          MemoryPropertiesFlags memoryProperties,
                                          cl_mem_flags flags,
                                          cl_mem_flags_intel flagsIntel,
                                          size_t size,
                                          void *hostPtr,
                                          cl_int &retVal,
                                          cl_mem &buffer) {
    Context *pContext = nullptr;
    retVal = validateObjects(WithCastToInternal(context, &pContext));
    if (retVal != CL_SUCCESS) {
        return;
    }

    if (!MemObjHelper::validateMemoryPropertiesForBuffer(memoryProperties, flags, flagsIntel)) {
        retVal = CL_INVALID_VALUE;
        return;
    }

    auto pDevice = pContext->getDevice(0);
    bool allowCreateBuffersWithUnrestrictedSize = isValueSet(flags, CL_MEM_ALLOW_UNRESTRICTED_SIZE_INTEL) ||
                                                  isValueSet(flagsIntel, CL_MEM_ALLOW_UNRESTRICTED_SIZE_INTEL);

    if (size == 0 || (size > pDevice->getHardwareCapabilities().maxMemAllocSize && !allowCreateBuffersWithUnrestrictedSize)) {
        retVal = CL_INVALID_BUFFER_SIZE;
        return;
    }

    /* Check the host ptr and data */
    bool expectHostPtr = (flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)) != 0;
    if ((hostPtr == nullptr) == expectHostPtr) {
        retVal = CL_INVALID_HOST_PTR;
        return;
    }

    // create the buffer
    buffer = create(pContext, memoryProperties, flags, flagsIntel, size, hostPtr, retVal);
}

Buffer *Buffer::create(Context *context,
                       cl_mem_flags flags,
                       size_t size,
                       void *hostPtr,
                       cl_int &errcodeRet) {
    return create(context, MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, 0, 0), flags, 0, size, hostPtr, errcodeRet);
}

Buffer *Buffer::create(Context *context,
                       MemoryPropertiesFlags memoryProperties,
                       cl_mem_flags flags,
                       cl_mem_flags_intel flagsIntel,
                       size_t size,
                       void *hostPtr,
                       cl_int &errcodeRet) {
    Buffer *pBuffer = nullptr;
    errcodeRet = CL_SUCCESS;

    GraphicsAllocation *memory = nullptr;
    GraphicsAllocation *mapAllocation = nullptr;
    bool zeroCopyAllowed = true;
    bool isHostPtrSVM = false;

    bool alignementSatisfied = true;
    bool allocateMemory = true;
    bool copyMemoryFromHostPtr = false;
    auto rootDeviceIndex = context->getDevice(0)->getRootDeviceIndex();
    MemoryManager *memoryManager = context->getMemoryManager();
    UNRECOVERABLE_IF(!memoryManager);

    GraphicsAllocation::AllocationType allocationType = getGraphicsAllocationType(
        memoryProperties,
        *context,
        HwHelper::renderCompressedBuffersSupported(context->getDevice(0)->getHardwareInfo()),
        memoryManager->isLocalMemorySupported(rootDeviceIndex),
        HwHelper::get(context->getDevice(0)->getHardwareInfo().platform.eRenderCoreFamily).obtainRenderBufferCompressionPreference(context->getDevice(0)->getHardwareInfo(), size));

    checkMemory(memoryProperties, size, hostPtr, errcodeRet, alignementSatisfied, copyMemoryFromHostPtr, memoryManager);

    if (errcodeRet != CL_SUCCESS) {
        return nullptr;
    }

    if (allocationType == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED) {
        zeroCopyAllowed = false;
        allocateMemory = true;
    }

    if (allocationType == GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY) {
        if (memoryProperties.flags.useHostPtr) {
            if (alignementSatisfied) {
                allocateMemory = false;
                zeroCopyAllowed = true;
            } else {
                zeroCopyAllowed = false;
                allocateMemory = true;
            }
        }
    }

    if (memoryProperties.flags.useHostPtr) {
        if (DebugManager.flags.DisableZeroCopyForUseHostPtr.get()) {
            zeroCopyAllowed = false;
            allocateMemory = true;
        }

        auto svmManager = context->getSVMAllocsManager();
        if (svmManager) {
            auto svmData = svmManager->getSVMAlloc(hostPtr);
            if (svmData) {
                memory = svmData->gpuAllocation;
                allocationType = memory->getAllocationType();
                isHostPtrSVM = true;
                zeroCopyAllowed = memory->getAllocationType() == GraphicsAllocation::AllocationType::SVM_ZERO_COPY;
                copyMemoryFromHostPtr = false;
                allocateMemory = false;
                mapAllocation = svmData->cpuAllocation;
            }
        }
    }

    if (context->isSharedContext) {
        zeroCopyAllowed = true;
        copyMemoryFromHostPtr = false;
        allocateMemory = false;
    }

    if (hostPtr && context->isProvidingPerformanceHints()) {
        if (zeroCopyAllowed) {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_GOOD_INTEL, CL_BUFFER_MEETS_ALIGNMENT_RESTRICTIONS, hostPtr, size);
        } else {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_BAD_INTEL, CL_BUFFER_DOESNT_MEET_ALIGNMENT_RESTRICTIONS, hostPtr, size, MemoryConstants::pageSize, MemoryConstants::pageSize);
        }
    }

    if (DebugManager.flags.DisableZeroCopyForBuffers.get()) {
        zeroCopyAllowed = false;
    }

    if (allocateMemory && context->isProvidingPerformanceHints()) {
        context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_GOOD_INTEL, CL_BUFFER_NEEDS_ALLOCATE_MEMORY);
    }

    if (!memory) {
        AllocationProperties allocProperties = MemoryPropertiesParser::getAllocationProperties(rootDeviceIndex, memoryProperties, allocateMemory, size, allocationType, context->areMultiStorageAllocationsPreferred());
        memory = memoryManager->allocateGraphicsMemoryWithProperties(allocProperties, hostPtr);
    }

    if (allocateMemory && memory && MemoryPool::isSystemMemoryPool(memory->getMemoryPool())) {
        memoryManager->addAllocationToHostPtrManager(memory);
    }

    //if allocation failed for CL_MEM_USE_HOST_PTR case retry with non zero copy path
    if (memoryProperties.flags.useHostPtr && !memory && Buffer::isReadOnlyMemoryPermittedByFlags(memoryProperties)) {
        allocationType = GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY;
        zeroCopyAllowed = false;
        copyMemoryFromHostPtr = true;
        AllocationProperties allocProperties = MemoryPropertiesParser::getAllocationProperties(rootDeviceIndex, memoryProperties, true, size, allocationType, context->areMultiStorageAllocationsPreferred());
        memory = memoryManager->allocateGraphicsMemoryWithProperties(allocProperties);
    }

    if (!memory) {
        errcodeRet = CL_OUT_OF_HOST_MEMORY;
        return nullptr;
    }

    if (!MemoryPool::isSystemMemoryPool(memory->getMemoryPool())) {
        zeroCopyAllowed = false;
        if (hostPtr) {
            if (!isHostPtrSVM) {
                copyMemoryFromHostPtr = true;
            }
        }
    } else if (allocationType == GraphicsAllocation::AllocationType::BUFFER) {
        allocationType = GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY;
    }

    memory->setAllocationType(allocationType);
    memory->setMemObjectsAllocationWithWritableFlags(!(memoryProperties.flags.readOnly || memoryProperties.flags.hostReadOnly || memoryProperties.flags.hostNoAccess));

    pBuffer = createBufferHw(context,
                             memoryProperties,
                             flags,
                             flagsIntel,
                             size,
                             memory->getUnderlyingBuffer(),
                             (memoryProperties.flags.useHostPtr) ? hostPtr : nullptr,
                             memory,
                             zeroCopyAllowed,
                             isHostPtrSVM,
                             false);

    if (!pBuffer) {
        errcodeRet = CL_OUT_OF_HOST_MEMORY;
        memoryManager->removeAllocationFromHostPtrManager(memory);
        memoryManager->freeGraphicsMemory(memory);
        return nullptr;
    }

    printDebugString(DebugManager.flags.LogMemoryObject.get(), stdout,
                     "\nCreated Buffer: Handle %p, hostPtr %p, size %llu, memoryStorage %p, GPU address %#llx, memoryPool:%du\n",
                     pBuffer, hostPtr, size, memory->getUnderlyingBuffer(), memory->getGpuAddress(), memory->getMemoryPool());

    if (memoryProperties.flags.useHostPtr) {
        if (!zeroCopyAllowed && !isHostPtrSVM) {
            AllocationProperties properties{rootDeviceIndex, false, size, GraphicsAllocation::AllocationType::MAP_ALLOCATION, false};
            properties.flags.flushL3RequiredForRead = properties.flags.flushL3RequiredForWrite = true;
            mapAllocation = memoryManager->allocateGraphicsMemoryWithProperties(properties, hostPtr);
        }
    }

    Buffer::provideCompressionHint(allocationType, context, pBuffer);

    pBuffer->mapAllocation = mapAllocation;
    pBuffer->setHostPtrMinSize(size);

    if (copyMemoryFromHostPtr) {
        auto gmm = memory->getDefaultGmm();
        bool gpuCopyRequired = (gmm && gmm->isRenderCompressed) || !MemoryPool::isSystemMemoryPool(memory->getMemoryPool());

        if (gpuCopyRequired) {
            auto blitMemoryToAllocationResult = context->blitMemoryToAllocation(*pBuffer, memory, hostPtr, size);

            if (blitMemoryToAllocationResult != BlitOperationResult::Success) {
                auto cmdQ = context->getSpecialQueue();
                if (CL_SUCCESS != cmdQ->enqueueWriteBuffer(pBuffer, CL_TRUE, 0, size, hostPtr, nullptr, 0, nullptr, nullptr)) {
                    errcodeRet = CL_OUT_OF_RESOURCES;
                }
            }
        } else {
            memcpy_s(memory->getUnderlyingBuffer(), size, hostPtr, size);
        }
    }

    if (errcodeRet != CL_SUCCESS) {
        pBuffer->release();
        return nullptr;
    }

    if (DebugManager.flags.MakeAllBuffersResident.get()) {
        auto graphicsAllocation = pBuffer->getGraphicsAllocation();
        context->getDevice(0u)->getRootDeviceEnvironment().memoryOperationsInterface->makeResident(ArrayRef<GraphicsAllocation *>(&graphicsAllocation, 1));
    }

    return pBuffer;
}

Buffer *Buffer::createSharedBuffer(Context *context, cl_mem_flags flags, SharingHandler *sharingHandler,
                                   GraphicsAllocation *graphicsAllocation) {
    auto sharedBuffer = createBufferHw(context, MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, 0, 0), flags, 0, graphicsAllocation->getUnderlyingBufferSize(), nullptr, nullptr, graphicsAllocation, false, false, false);

    sharedBuffer->setSharingHandler(sharingHandler);
    return sharedBuffer;
}

void Buffer::checkMemory(MemoryPropertiesFlags memoryProperties,
                         size_t size,
                         void *hostPtr,
                         cl_int &errcodeRet,
                         bool &alignementSatisfied,
                         bool &copyMemoryFromHostPtr,
                         MemoryManager *memoryManager) {
    errcodeRet = CL_SUCCESS;
    alignementSatisfied = true;
    copyMemoryFromHostPtr = false;
    uintptr_t minAddress = 0;
    auto memRestrictions = memoryManager->getAlignedMallocRestrictions();
    if (memRestrictions) {
        minAddress = memRestrictions->minAddress;
    }

    if (hostPtr) {
        if (!(memoryProperties.flags.useHostPtr || memoryProperties.flags.copyHostPtr)) {
            errcodeRet = CL_INVALID_HOST_PTR;
            return;
        }
    }

    if (memoryProperties.flags.useHostPtr) {
        if (hostPtr) {
            auto fragment = memoryManager->getHostPtrManager()->getFragment(hostPtr);
            if (fragment && fragment->driverAllocation) {
                errcodeRet = CL_INVALID_HOST_PTR;
                return;
            }
            if (alignUp(hostPtr, MemoryConstants::cacheLineSize) != hostPtr ||
                alignUp(size, MemoryConstants::cacheLineSize) != size ||
                minAddress > reinterpret_cast<uintptr_t>(hostPtr)) {
                alignementSatisfied = false;
                copyMemoryFromHostPtr = true;
            }
        } else {
            errcodeRet = CL_INVALID_HOST_PTR;
        }
    }

    if (memoryProperties.flags.copyHostPtr) {
        if (hostPtr) {
            copyMemoryFromHostPtr = true;
        } else {
            errcodeRet = CL_INVALID_HOST_PTR;
        }
    }
    return;
}

GraphicsAllocation::AllocationType Buffer::getGraphicsAllocationType(const MemoryPropertiesFlags &properties, Context &context,
                                                                     bool renderCompressedBuffers, bool isLocalMemoryEnabled,
                                                                     bool preferCompression) {
    if (context.isSharedContext || properties.flags.forceSharedPhysicalMemory) {
        return GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY;
    }

    if (properties.flags.useHostPtr && !isLocalMemoryEnabled) {
        return GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY;
    }

    if (MemObjHelper::isSuitableForRenderCompression(renderCompressedBuffers, properties, context, preferCompression)) {
        return GraphicsAllocation::AllocationType::BUFFER_COMPRESSED;
    }

    return GraphicsAllocation::AllocationType::BUFFER;
}

bool Buffer::isReadOnlyMemoryPermittedByFlags(const MemoryPropertiesFlags &properties) {
    // Host won't access or will only read and kernel will only read
    return (properties.flags.hostNoAccess || properties.flags.hostReadOnly) && properties.flags.readOnly;
}

Buffer *Buffer::createSubBuffer(cl_mem_flags flags,
                                cl_mem_flags_intel flagsIntel,
                                const cl_buffer_region *region,
                                cl_int &errcodeRet) {
    DEBUG_BREAK_IF(nullptr == createFunction);
    MemoryPropertiesFlags memoryProperties = MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, flagsIntel, 0);
    auto buffer = createFunction(this->context, memoryProperties, flags, 0, region->size,
                                 ptrOffset(this->memoryStorage, region->origin),
                                 this->hostPtr ? ptrOffset(this->hostPtr, region->origin) : nullptr,
                                 this->graphicsAllocation,
                                 this->isZeroCopy, this->isHostPtrSVM, false);

    if (this->context->isProvidingPerformanceHints()) {
        this->context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_GOOD_INTEL, SUBBUFFER_SHARES_MEMORY, static_cast<cl_mem>(this));
    }

    buffer->associatedMemObject = this;
    buffer->offset = region->origin;
    buffer->setParentSharingHandler(this->getSharingHandler());
    this->incRefInternal();

    errcodeRet = CL_SUCCESS;
    return buffer;
}

uint64_t Buffer::setArgStateless(void *memory, uint32_t patchSize, bool set32BitAddressing) {
    // Subbuffers have offset that graphicsAllocation is not aware of
    uintptr_t addressToPatch = ((set32BitAddressing) ? static_cast<uintptr_t>(graphicsAllocation->getGpuAddressToPatch()) : static_cast<uintptr_t>(graphicsAllocation->getGpuAddress())) + this->offset;
    DEBUG_BREAK_IF(!(graphicsAllocation->isLocked() || (addressToPatch != 0) || (graphicsAllocation->getGpuBaseAddress() != 0) ||
                     (this->getCpuAddress() == nullptr && this->getGraphicsAllocation()->peekSharedHandle())));

    patchWithRequiredSize(memory, patchSize, addressToPatch);

    return addressToPatch;
}

bool Buffer::bufferRectPitchSet(const size_t *bufferOrigin,
                                const size_t *region,
                                size_t &bufferRowPitch,
                                size_t &bufferSlicePitch,
                                size_t &hostRowPitch,
                                size_t &hostSlicePitch) {
    if (bufferRowPitch == 0)
        bufferRowPitch = region[0];
    if (bufferSlicePitch == 0)
        bufferSlicePitch = region[1] * bufferRowPitch;

    if (hostRowPitch == 0)
        hostRowPitch = region[0];
    if (hostSlicePitch == 0)
        hostSlicePitch = region[1] * hostRowPitch;

    if (bufferRowPitch < region[0] ||
        hostRowPitch < region[0]) {
        return false;
    }
    if ((bufferSlicePitch < region[1] * bufferRowPitch || bufferSlicePitch % bufferRowPitch != 0) ||
        (hostSlicePitch < region[1] * hostRowPitch || hostSlicePitch % hostRowPitch != 0)) {
        return false;
    }

    if ((bufferOrigin[2] + region[2] - 1) * bufferSlicePitch + (bufferOrigin[1] + region[1] - 1) * bufferRowPitch + bufferOrigin[0] + region[0] > this->getSize()) {
        return false;
    }
    return true;
}

void Buffer::transferData(void *dst, void *src, size_t copySize, size_t copyOffset) {
    DBG_LOG(LogMemoryObject, __FUNCTION__, " hostPtr: ", hostPtr, ", size: ", copySize, ", offset: ", copyOffset, ", memoryStorage: ", memoryStorage);
    auto dstPtr = ptrOffset(dst, copyOffset);
    auto srcPtr = ptrOffset(src, copyOffset);
    memcpy_s(dstPtr, copySize, srcPtr, copySize);
}

void Buffer::transferDataToHostPtr(MemObjSizeArray &copySize, MemObjOffsetArray &copyOffset) {
    transferData(hostPtr, memoryStorage, copySize[0], copyOffset[0]);
}

void Buffer::transferDataFromHostPtr(MemObjSizeArray &copySize, MemObjOffsetArray &copyOffset) {
    transferData(memoryStorage, hostPtr, copySize[0], copyOffset[0]);
}

size_t Buffer::calculateHostPtrSize(const size_t *origin, const size_t *region, size_t rowPitch, size_t slicePitch) {
    size_t hostPtrOffsetInBytes = origin[2] * slicePitch + origin[1] * rowPitch + origin[0];
    size_t hostPtrRegionSizeInbytes = region[0] + rowPitch * (region[1] - 1) + slicePitch * (region[2] - 1);
    size_t hostPtrSize = hostPtrOffsetInBytes + hostPtrRegionSizeInbytes;
    return hostPtrSize;
}

bool Buffer::isReadWriteOnCpuAllowed() {
    if (forceDisallowCPUCopy) {
        return false;
    }

    if (this->isCompressed()) {
        return false;
    }

    if (graphicsAllocation->peekSharedHandle() != 0) {
        return false;
    }
    return true;
}

bool Buffer::isReadWriteOnCpuPreffered(void *ptr, size_t size) {
    if (MemoryPool::isSystemMemoryPool(graphicsAllocation->getMemoryPool())) {
        //if buffer is not zero copy and pointer is aligned it will be more beneficial to do the transfer on GPU
        if (!isMemObjZeroCopy() && (reinterpret_cast<uintptr_t>(ptr) & (MemoryConstants::cacheLineSize - 1)) == 0) {
            return false;
        }

        //on low power devices larger transfers are better on the GPU
        if (context->getDevice(0)->getDeviceInfo().platformLP && size > maxBufferSizeForReadWriteOnCpu) {
            return false;
        }
        return true;
    }

    return false;
}

Buffer *Buffer::createBufferHw(Context *context,
                               MemoryPropertiesFlags memoryProperties,
                               cl_mem_flags flags,
                               cl_mem_flags_intel flagsIntel,
                               size_t size,
                               void *memoryStorage,
                               void *hostPtr,
                               GraphicsAllocation *gfxAllocation,
                               bool zeroCopy,
                               bool isHostPtrSVM,
                               bool isImageRedescribed) {
    const auto device = context->getDevice(0);
    const auto &hwInfo = device->getHardwareInfo();

    auto funcCreate = bufferFactory[hwInfo.platform.eRenderCoreFamily].createBufferFunction;
    DEBUG_BREAK_IF(nullptr == funcCreate);
    auto pBuffer = funcCreate(context, memoryProperties, flags, flagsIntel, size, memoryStorage, hostPtr, gfxAllocation,
                              zeroCopy, isHostPtrSVM, isImageRedescribed);
    DEBUG_BREAK_IF(nullptr == pBuffer);
    if (pBuffer) {
        pBuffer->createFunction = funcCreate;
    }
    return pBuffer;
}

Buffer *Buffer::createBufferHwFromDevice(const Device *device,
                                         cl_mem_flags flags,
                                         cl_mem_flags_intel flagsIntel,
                                         size_t size,
                                         void *memoryStorage,
                                         void *hostPtr,
                                         GraphicsAllocation *gfxAllocation,
                                         size_t offset,
                                         bool zeroCopy,
                                         bool isHostPtrSVM,
                                         bool isImageRedescribed) {

    const auto &hwInfo = device->getHardwareInfo();

    auto funcCreate = bufferFactory[hwInfo.platform.eRenderCoreFamily].createBufferFunction;
    DEBUG_BREAK_IF(nullptr == funcCreate);
    MemoryPropertiesFlags memoryProperties = MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, flagsIntel, 0);
    auto pBuffer = funcCreate(nullptr, memoryProperties, flags, flagsIntel, size, memoryStorage, hostPtr, gfxAllocation,
                              zeroCopy, isHostPtrSVM, isImageRedescribed);
    pBuffer->offset = offset;
    pBuffer->executionEnvironment = device->getExecutionEnvironment();
    pBuffer->rootDeviceEnvironment = pBuffer->executionEnvironment->rootDeviceEnvironments[device->getRootDeviceIndex()].get();
    return pBuffer;
}

uint32_t Buffer::getMocsValue(bool disableL3Cache, bool isReadOnlyArgument) const {
    uint64_t bufferAddress = 0;
    size_t bufferSize = 0;
    if (getGraphicsAllocation()) {
        bufferAddress = getGraphicsAllocation()->getGpuAddress();
        bufferSize = getGraphicsAllocation()->getUnderlyingBufferSize();
    } else {
        bufferAddress = reinterpret_cast<uint64_t>(getHostPtr());
        bufferSize = getSize();
    }
    bufferAddress += this->offset;

    bool readOnlyMemObj = isValueSet(getMemoryPropertiesFlags(), CL_MEM_READ_ONLY) || isReadOnlyArgument;
    bool alignedMemObj = isAligned<MemoryConstants::cacheLineSize>(bufferAddress) &&
                         isAligned<MemoryConstants::cacheLineSize>(bufferSize);

    auto gmmHelper = rootDeviceEnvironment->getGmmHelper();
    if (!disableL3Cache && !isMemObjUncacheableForSurfaceState() && (alignedMemObj || readOnlyMemObj || !isMemObjZeroCopy())) {
        return gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER);
    } else {
        return gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CACHELINE_MISALIGNED);
    }
}

bool Buffer::isCompressed() const {
    if (this->getGraphicsAllocation()->getDefaultGmm()) {
        return this->getGraphicsAllocation()->getDefaultGmm()->isRenderCompressed;
    }
    if (this->getGraphicsAllocation()->getAllocationType() == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED) {
        return true;
    }

    return false;
}

void Buffer::setSurfaceState(const Device *device,
                             void *surfaceState,
                             size_t svmSize,
                             void *svmPtr,
                             size_t offset,
                             GraphicsAllocation *gfxAlloc,
                             cl_mem_flags flags,
                             cl_mem_flags_intel flagsIntel) {
    auto buffer = Buffer::createBufferHwFromDevice(device, flags, flagsIntel, svmSize, svmPtr, svmPtr, gfxAlloc, offset, true, false, false);
    buffer->setArgStateful(surfaceState, false, false, false, false);
    buffer->graphicsAllocation = nullptr;
    delete buffer;
}

void Buffer::provideCompressionHint(GraphicsAllocation::AllocationType allocationType,
                                    Context *context,
                                    Buffer *buffer) {
    if (context->isProvidingPerformanceHints() && HwHelper::renderCompressedBuffersSupported(context->getDevice(0)->getHardwareInfo())) {
        if (allocationType == GraphicsAllocation::AllocationType::BUFFER_COMPRESSED) {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, BUFFER_IS_COMPRESSED, buffer);
        } else {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, BUFFER_IS_NOT_COMPRESSED, buffer);
        }
    }
}
} // namespace NEO
