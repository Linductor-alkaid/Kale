/**
 * @file test_instance_descriptor_set.cpp
 * @brief phase7-7.9 实例级 DescriptorSet 池化单元测试
 *
 * 覆盖：AcquireInstanceDescriptorSet(nullptr) 返回无效；Acquire 返回有效 set；
 * ReleaseAllInstanceDescriptorSets 回收；再次 Acquire 从池复用；多帧 Acquire/Release 复用。
 */

#include <kale_pipeline/material.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

/** Mock 设备：CreateDescriptorSet/CreateBuffer(Uniform) 返回有效句柄，WriteDescriptorSetBuffer 可调用 */
class MockInstanceDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc& desc, const void*) override {
        if (kale_device::HasBufferUsage(desc.usage, kale_device::BufferUsage::Uniform)) {
            nextBufferId_++;
            kale_device::BufferHandle h;
            h.id = nextBufferId_;
            return h;
        }
        return {};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }

    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc& layout) override {
        if (layout.bindings.empty()) return {};
        nextSetId_++;
        kale_device::DescriptorSetHandle h;
        h.id = nextSetId_;
        return h;
    }

    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t,
                                   kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                 kale_device::BufferHandle, std::size_t, std::size_t) override {
        writeBufferCount_++;
    }

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

    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    std::uint64_t createSetCount() const { return nextSetId_; }
    std::uint64_t writeBufferCount() const { return writeBufferCount_; }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextSetId_ = 0;
    std::uint64_t nextBufferId_ = 0;
    std::uint64_t writeBufferCount_ = 0;
};

static void test_acquire_with_null_device_returns_invalid() {
    kale::pipeline::Material mat;
    float data[16] = {};
    kale_device::DescriptorSetHandle h = mat.AcquireInstanceDescriptorSet(nullptr, data, sizeof(data));
    TEST_CHECK(!h.IsValid());
}

static void test_acquire_returns_valid_set() {
    MockInstanceDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    float data[16] = {};
    kale_device::DescriptorSetHandle h = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(h.IsValid());
    TEST_CHECK(dev.writeBufferCount() >= 1u);
    dev.Shutdown();
}

static void test_release_recycles_pool() {
    MockInstanceDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    float data[16] = {};
    kale_device::DescriptorSetHandle h1 = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(h1.IsValid());
    std::uint64_t setsCreated = dev.createSetCount();
    mat.ReleaseAllInstanceDescriptorSets();
    kale_device::DescriptorSetHandle h2 = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(dev.createSetCount() == setsCreated);
    dev.Shutdown();
}

static void test_multiple_acquire_then_release_reuse() {
    MockInstanceDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    float data[16] = {};
    kale_device::DescriptorSetHandle a = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    kale_device::DescriptorSetHandle b = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(a.IsValid() && b.IsValid());
    std::uint64_t n = dev.createSetCount();
    mat.ReleaseAllInstanceDescriptorSets();
    kale_device::DescriptorSetHandle c = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    kale_device::DescriptorSetHandle d = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(c.IsValid() && d.IsValid());
    TEST_CHECK(dev.createSetCount() == n);
    dev.Shutdown();
}

static void test_acquire_with_zero_size_ok() {
    MockInstanceDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    kale_device::DescriptorSetHandle h = mat.AcquireInstanceDescriptorSet(&dev, nullptr, 0);
    TEST_CHECK(h.IsValid());
    dev.Shutdown();
}

}  // namespace

int main() {
    test_acquire_with_null_device_returns_invalid();
    test_acquire_returns_valid_set();
    test_release_recycles_pool();
    test_multiple_acquire_then_release_reuse();
    test_acquire_with_zero_size_ok();
    return 0;
}
