/**
 * @file test_descriptor_set_pool.cpp
 * @brief phase9-9.2 设备层 DescriptorSet 池化单元测试
 *
 * 覆盖：IRenderDevice 默认 AcquireInstanceDescriptorSet 返回无效；
 * 实现池的设备 Acquire 返回有效、Release 后再次 Acquire 复用（分配数不增）。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

/** 默认设备：不实现池，Acquire 返回无效 */
class DefaultDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                  kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}
    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return nullptr; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { return {}; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override { return kale_device::IRenderDevice::kInvalidSwapchainImageIndex; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
};

/** 实现实例池的设备：Acquire 从池分配，Release 归还 */
class PooledDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                  kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}

    kale_device::DescriptorSetHandle AcquireInstanceDescriptorSet(const void*, std::size_t size) override {
        if (size > kale_device::kInstanceDescriptorDataSize) size = kale_device::kInstanceDescriptorDataSize;
        kale_device::DescriptorSetHandle h;
        if (!freeList_.empty()) {
            h.id = freeList_.back();
            freeList_.pop_back();
        } else {
            allocCount_++;
            h.id = allocCount_;
        }
        return h;
    }
    void ReleaseInstanceDescriptorSet(kale_device::DescriptorSetHandle handle) override {
        if (handle.IsValid()) freeList_.push_back(handle.id);
    }

    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return nullptr; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { return {}; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override { return kale_device::IRenderDevice::kInvalidSwapchainImageIndex; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    std::uint64_t allocCount() const { return allocCount_; }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t allocCount_ = 0;
    std::vector<std::uint64_t> freeList_;
};

static void test_default_acquire_returns_invalid() {
    DefaultDevice dev;
    dev.Initialize({});
    kale_device::DescriptorSetHandle h = dev.AcquireInstanceDescriptorSet(nullptr, 64);
    TEST_CHECK(!h.IsValid());
    dev.Shutdown();
}

static void test_pooled_acquire_returns_valid() {
    PooledDevice dev;
    dev.Initialize({});
    kale_device::DescriptorSetHandle h = dev.AcquireInstanceDescriptorSet(nullptr, 64);
    TEST_CHECK(h.IsValid());
    TEST_CHECK(dev.allocCount() == 1u);
    dev.Shutdown();
}

static void test_pooled_release_then_acquire_reuses() {
    PooledDevice dev;
    dev.Initialize({});
    kale_device::DescriptorSetHandle h1 = dev.AcquireInstanceDescriptorSet(nullptr, 64);
    TEST_CHECK(h1.IsValid());
    std::uint64_t n = dev.allocCount();
    dev.ReleaseInstanceDescriptorSet(h1);
    kale_device::DescriptorSetHandle h2 = dev.AcquireInstanceDescriptorSet(nullptr, 64);
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(dev.allocCount() == n);
    dev.Shutdown();
}

}  // namespace

int main() {
    test_default_acquire_returns_invalid();
    test_pooled_acquire_returns_valid();
    test_pooled_release_then_acquire_reuses();
    return 0;
}
