/**
 * @file staging_memory_manager.hpp
 * @brief Staging 内存管理器：池化 Staging Buffer，供 CPU→GPU 上传使用
 *
 * 与 resource_management_layer_design.md 5.9 对齐。
 * Phase 6.1：StagingAllocation、Allocate、Free、默认 64MB 池初始化。
 * Phase 6.2：与 Fence 关联的延迟回收，ReclaimCompleted，分配不足时先回收再扩展池。
 * Phase 6.3：SubmitUpload Buffer→Texture / Buffer→Buffer，pendingUploads_，FlushUploads。
 */

#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <kale_device/command_list.hpp>
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

    /**
     * @brief 将分配块在 GPU Fence signal 后回收到池（延迟回收，避免上传未完成即复用）
     * @param alloc 此前 Allocate 返回的分配块
     * @param fence 与此上传关联的 Fence；GPU 完成上传并 signal 后，ReclaimCompleted() 会将 alloc 回池
     */
    void Free(const StagingAllocation& alloc, kale_device::FenceHandle fence);

    /**
     * @brief 检查所有待延迟回收的分配，若其 Fence 已 signal 则回收到池
     * 分配不足时 Allocate 内部会先调用本方法再重试；应用也可每帧调用以及时回收。
     */
    void ReclaimCompleted();

    /**
     * @brief 提交 Buffer→Texture 上传：若 cmd 非空则立即录制到 cmd，否则加入 pendingUploads_ 待 FlushUploads 执行
     * @param width, height, depth 目标 mip 的 extent（2D 纹理通常 depth=1）
     */
    void SubmitUpload(kale_device::CommandList* cmd, const StagingAllocation& src,
                      kale_device::TextureHandle dstTexture, std::uint32_t mipLevel = 0,
                      std::uint32_t width = 0, std::uint32_t height = 0, std::uint32_t depth = 1);

    /**
     * @brief 提交 Buffer→Buffer 上传：若 cmd 非空则立即录制到 cmd，否则加入 pendingUploads_
     */
    void SubmitUpload(kale_device::CommandList* cmd, const StagingAllocation& src,
                      kale_device::BufferHandle dstBuffer, std::size_t dstOffset = 0);

    /**
     * @brief 在 Execute 前提交所有待执行上传：取 CommandList 执行 pendingUploads_ 后 Submit，返回本批 Fence 供 Free(alloc, fence) 使用
     * @return 本批提交使用的 FenceHandle；若无待上传则返回无效句柄
     */
    kale_device::FenceHandle FlushUploads(kale_device::IRenderDevice* device);

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

    void RecycleAlloc(const StagingAllocation& alloc);

    kale_device::IRenderDevice* device_ = nullptr;
    std::size_t poolSize_ = 64 * 1024 * 1024;  /* 64MB 默认 */
    std::vector<PoolBuffer> poolBuffers_;
    std::vector<std::vector<FreeBlock>> freeLists_;  /* 与 poolBuffers_ 一一对应 */
    std::vector<std::pair<StagingAllocation, kale_device::FenceHandle>> pendingFrees_;
    std::vector<std::function<void(kale_device::CommandList*)>> pendingUploads_;
};

}  // namespace kale::resource
