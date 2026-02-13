/**
 * @file instanced_draw.cpp
 * @brief GPU 实例化渲染实现
 */

#include <kale_pipeline/instanced_draw.hpp>

namespace kale::pipeline {

kale_device::BufferHandle CreateInstanceBuffer(kale_device::IRenderDevice* device,
                                              const void* data,
                                              std::size_t size) {
    if (!device || size == 0)
        return kale_device::BufferHandle{};
    kale_device::BufferDesc desc;
    desc.size = size;
    desc.usage = kale_device::BufferUsage::Vertex;
    desc.cpuVisible = true;
    return device->CreateBuffer(desc, data);
}

void DrawInstanced(kale_device::CommandList& cmd,
                   kale_device::IRenderDevice* device,
                   const kale::resource::Mesh* mesh,
                   kale::pipeline::Material* material,
                   kale_device::BufferHandle instanceBuffer,
                   std::size_t instanceBufferOffset,
                   std::uint32_t instanceCount) {
    if (instanceCount == 0)
        return;
    if (material)
        material->BindForDraw(cmd, device, nullptr, 0);
    if (!mesh || !mesh->vertexBuffer.IsValid() || !instanceBuffer.IsValid())
        return;

    cmd.BindVertexBuffer(0, mesh->vertexBuffer, 0);
    cmd.BindVertexBuffer(1, instanceBuffer, instanceBufferOffset);

    if (mesh->indexBuffer.IsValid() && mesh->indexCount > 0) {
        cmd.BindIndexBuffer(mesh->indexBuffer, 0, false);
        cmd.DrawIndexed(mesh->indexCount, instanceCount, 0, 0, 0);
    } else {
        cmd.Draw(mesh->vertexCount, instanceCount, 0, 0);
    }
}

}  // namespace kale::pipeline
