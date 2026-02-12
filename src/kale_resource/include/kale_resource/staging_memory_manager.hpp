/**
 * @file staging_memory_manager.hpp
 * @brief Staging 内存管理器：池化 Staging Buffer，供 CPU→GPU 上传使用
 *
 * 与 resource_management_layer_design.md 5.9 对齐。
 * Phase 6.1：StagingAllocation、Allocate、Free、默认 64MB 池初始化。
 */

#pragma once

#include <cstddef>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

namespace kale::resource {

/**
 * @brief Staging 分配块：从池中分配的一段可写 CPU 内存，对应 GPU Buffer 的一段
 */
struct StagingAllocation {
    kale_device::BufferHandle buffer{};
    void* mappedPtr = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;

    bool IsValid() const {
        return buffer.IsValid() && mappedPtr != nullptr && size > 0;
    }
};

/**
 * @brief Staging 内存管理器
 *
 * 预分配 CPU 可见的 Staging Buffer 池（默认 64MB），
 * Allocate(size) 从池中分配，Free(alloc) 回收到池供复用。
 */
class StagingMemoryManager {
public:
    explicit StagingMemoryManager(kale_device::IRenderDevice* device);

    StagingMemoryManager(const StagingMemoryManager&) = delete;
    StagingMemoryManager& operator=(const StagingMemoryManager&) = delete;

    /**
     * @brief 从池中分配一段 Staging 内存
     * @param size 字节数
     * @return 有效 StagingAllocation，或无效（buffer.id==0）表示分配失败
     */
    StagingAllocation Allocate(std::size_t size);

    /**
     * @brief 将分配块回收到池，供后续 Allocate 复用
     * @param alloc 此前 Allocate 返回的分配块（调用后 alloc 不再有效）
     */
    void Free(const StagingAllocation& alloc);

    /** @brief 设置池总容量（字节），仅影响后续新创建的池块，默认 64MB */
    void SetPoolSize(std::size_t bytes) { poolSize_ = bytes; }
    std::size_t GetPoolSize() const { return poolSize_; }

private:
    struct PoolBuffer {
        kale_device::BufferHandle handle;
        void* mappedPtr = nullptr;
        std::size_t totalSize = 0;
        std::size_t usedOffset = 0;  /* 线性分配水线 */
    };
    struct FreeBlock {
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    kale_device::IRenderDevice* device_ = nullptr;
    std::size_t poolSize_ = 64 * 1024 * 1024;  /* 64MB 默认 */
    std::vector<PoolBuffer> poolBuffers_;
    std::vector<std::vector<FreeBlock>> freeLists_;  /* 与 poolBuffers_ 一一对应 */
};

}  // namespace kale::resource
