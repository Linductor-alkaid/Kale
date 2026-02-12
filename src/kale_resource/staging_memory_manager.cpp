/**
 * @file staging_memory_manager.cpp
 * @brief StagingMemoryManager 实现：池分配与回收
 */

#include <kale_resource/staging_memory_manager.hpp>

#include <cstddef>
#include <kale_device/rdi_types.hpp>

namespace kale::resource {

using namespace kale_device;

StagingMemoryManager::StagingMemoryManager(IRenderDevice* device) : device_(device) {}

StagingAllocation StagingMemoryManager::Allocate(std::size_t size) {
    if (!device_ || size == 0) return StagingAllocation{};

    const std::size_t align = 256;  /* 常见上传对齐 */
    const std::size_t alignedSize = (size + align - 1) & ~(align - 1);

    /* 1) 在现有池中找空闲块：first fit */
    for (size_t i = 0; i < poolBuffers_.size(); ++i) {
        auto& freeList = freeLists_[i];
        for (auto it = freeList.begin(); it != freeList.end(); ++it) {
            if (it->size >= alignedSize) {
                StagingAllocation alloc;
                alloc.buffer = poolBuffers_[i].handle;
                alloc.offset = it->offset;
                alloc.size = alignedSize;
                alloc.mappedPtr = poolBuffers_[i].mappedPtr
                    ? static_cast<char*>(poolBuffers_[i].mappedPtr) + it->offset
                    : nullptr;
                if (it->size == alignedSize)
                    freeList.erase(it);
                else {
                    it->offset += alignedSize;
                    it->size -= alignedSize;
                }
                return alloc;
            }
        }
        /* 2) 线性分配：从 usedOffset 往后 */
        PoolBuffer& pb = poolBuffers_[i];
        if (pb.usedOffset + alignedSize <= pb.totalSize) {
            StagingAllocation alloc;
            alloc.buffer = pb.handle;
            alloc.offset = pb.usedOffset;
            alloc.size = alignedSize;
            alloc.mappedPtr = pb.mappedPtr
                ? static_cast<char*>(pb.mappedPtr) + pb.usedOffset
                : nullptr;
            pb.usedOffset += alignedSize;
            return alloc;
        }
    }

    /* 3) 分配新池块 */
    BufferDesc desc;
    desc.size = (std::max)(alignedSize, poolSize_);
    desc.usage = BufferUsage::Transfer;
    desc.cpuVisible = true;
    BufferHandle newBuf = device_->CreateBuffer(desc, nullptr);
    if (!newBuf.IsValid()) return StagingAllocation{};

    void* mapped = device_->MapBuffer(newBuf, 0, desc.size);
    PoolBuffer pb;
    pb.handle = newBuf;
    pb.mappedPtr = mapped;
    pb.totalSize = desc.size;
    pb.usedOffset = alignedSize;
    poolBuffers_.push_back(pb);
    freeLists_.push_back({});

    StagingAllocation alloc;
    alloc.buffer = newBuf;
    alloc.offset = 0;
    alloc.size = alignedSize;
    alloc.mappedPtr = mapped;
    return alloc;
}

void StagingMemoryManager::Free(const StagingAllocation& alloc) {
    if (!alloc.IsValid()) return;
    for (size_t i = 0; i < poolBuffers_.size(); ++i) {
        if (poolBuffers_[i].handle.id != alloc.buffer.id) continue;
        freeLists_[i].push_back(FreeBlock{ alloc.offset, alloc.size });
        return;
    }
}

}  // namespace kale::resource
