/**
 * @file test_renderable.cpp
 * @brief phase5-5.10 Renderable 抽象单元测试
 *
 * 覆盖：GetBounds() 返回 bounds_、GetMesh()/GetMaterial() 默认 nullptr、
 * Draw() 可调用不崩溃、ReleaseFrameResources() 默认空实现不崩溃。
 */

#include <kale_scene/renderable.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

/** 最小实现：GetBounds 返回 bounds_，GetMesh/GetMaterial 默认 nullptr，Draw/ReleaseFrameResources 空 */
class MinimalRenderable : public kale::scene::Renderable {
public:
    explicit MinimalRenderable(const kale::resource::BoundingBox& box) { bounds_ = box; }
    kale::resource::BoundingBox GetBounds() const override { return bounds_; }
    void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}
};

/** Mock CommandList 仅保证 Draw(renderable) 调用不崩溃 */
class MockCommandList : public kale_device::CommandList {
public:
    void BeginRenderPass(const std::vector<kale_device::TextureHandle>&,
                         kale_device::TextureHandle) override {}
    void EndRenderPass() override {}
    void BindPipeline(kale_device::PipelineHandle) override {}
    void BindDescriptorSet(std::uint32_t, kale_device::DescriptorSetHandle) override {}
    void BindVertexBuffer(std::uint32_t, kale_device::BufferHandle, std::size_t) override {}
    void BindIndexBuffer(kale_device::BufferHandle, std::size_t, bool) override {}
    void SetPushConstants(const void*, std::size_t, std::size_t) override {}
    void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyBufferToBuffer(kale_device::BufferHandle, std::size_t,
                            kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void CopyBufferToTexture(kale_device::BufferHandle, std::size_t,
                             kale_device::TextureHandle, std::uint32_t,
                             std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float*) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

}  // namespace

int main() {
    using namespace kale::scene;
    using namespace kale::resource;

    kale::resource::BoundingBox box;
    box.min = glm::vec3(-1.f, -2.f, -3.f);
    box.max = glm::vec3(1.f, 2.f, 3.f);
    MinimalRenderable r(box);

    // GetBounds() 返回设置的 bounds_
    BoundingBox got = r.GetBounds();
    TEST_CHECK(got.min.x == -1.f && got.min.y == -2.f && got.min.z == -3.f);
    TEST_CHECK(got.max.x == 1.f && got.max.y == 2.f && got.max.z == 3.f);

    // GetMesh() / GetMaterial() 默认返回 nullptr
    TEST_CHECK(r.GetMesh() == nullptr);
    TEST_CHECK(r.GetMaterial() == nullptr);

    // Draw() 可调用不崩溃（device 为 nullptr）
    MockCommandList cmd;
    glm::mat4 identity(1.f);
    r.Draw(cmd, identity, nullptr);

    // Draw() 可调用不崩溃（device 非空由其他测试覆盖，此处仅保证接口）
    r.Draw(cmd, identity, nullptr);

    // ReleaseFrameResources() 默认空实现不崩溃
    r.ReleaseFrameResources();

    // UpdateBounds / GetWorldBounds（phase13-13.23）
    r.UpdateBounds(glm::mat4(1.f));
    BoundingBox worldIdentity = r.GetWorldBounds();
    TEST_CHECK(worldIdentity.min.x == got.min.x && worldIdentity.max.x == got.max.x);

    glm::mat4 translate = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f));
    r.UpdateBounds(translate);
    BoundingBox worldTranslated = r.GetWorldBounds();
    BoundingBox expected = TransformBounds(box, translate);
    TEST_CHECK(std::abs(worldTranslated.min.x - expected.min.x) < 1e-5f);
    TEST_CHECK(std::abs(worldTranslated.max.x - expected.max.x) < 1e-5f);

    return 0;
}
