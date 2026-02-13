/**
 * @file static_mesh.cpp
 * @brief StaticMesh 实现：Draw 时资源检查、占位符与 LoadAsync 触发
 */

#include <kale_scene/static_mesh.hpp>

namespace kale::scene {

StaticMesh::StaticMesh(kale::resource::ResourceManager* resourceManager,
                       std::string meshPath,
                       std::string materialPath)
    : resourceManager_(resourceManager),
      meshPath_(std::move(meshPath)),
      materialPath_(std::move(materialPath)) {}

kale::resource::BoundingBox StaticMesh::GetBounds() const {
    return bounds_;
}

const kale::resource::Mesh* StaticMesh::GetMesh() const {
    if (!resourceManager_ || !meshHandle_.IsValid()) return nullptr;
    return resourceManager_->Get<kale::resource::Mesh>(meshHandle_);
}

const kale::resource::Material* StaticMesh::GetMaterial() const {
    if (!resourceManager_ || !materialHandle_.IsValid()) return nullptr;
    return resourceManager_->Get<kale::resource::Material>(materialHandle_);
}

void StaticMesh::Draw(kale_device::CommandList& cmd, const glm::mat4& worldTransform) {
    if (!resourceManager_) return;

    // Mesh：确保有句柄（GetOrCreatePlaceholder），未就绪则用占位符；仅在新创建占位条目时触发 LoadAsync，避免重复
    if (!meshHandle_.IsValid()) {
        auto [h, created] = resourceManager_->GetOrCreatePlaceholder<kale::resource::Mesh>(meshPath_);
        meshHandle_ = h;
        if (created)
            resourceManager_->LoadAsync<kale::resource::Mesh>(meshPath_);
    }
    const kale::resource::Mesh* mesh = resourceManager_->Get<kale::resource::Mesh>(meshHandle_);
    if (!mesh)
        mesh = resourceManager_->GetPlaceholderMesh();

    // Material：同上
    if (!materialPath_.empty() && !materialHandle_.IsValid()) {
        auto [h, created] = resourceManager_->GetOrCreatePlaceholder<kale::resource::Material>(materialPath_);
        materialHandle_ = h;
        if (created)
            resourceManager_->LoadAsync<kale::resource::Material>(materialPath_);
    }
    const kale::resource::Material* material = nullptr;
    if (materialHandle_.IsValid())
        material = resourceManager_->Get<kale::resource::Material>(materialHandle_);
    if (!material)
        material = resourceManager_->GetPlaceholderMaterial();

    (void)material;  // 当前 Material 为空结构，后续 phase 用于绑定管线/描述符

    if (!mesh) return;

    // 更新包围盒供 CullScene 使用（若尚未从 mesh 设置）
    if (bounds_.min == bounds_.max && mesh->vertexCount > 0) {
        bounds_ = mesh->bounds;
    }

    if (!mesh->vertexBuffer.IsValid()) return;

    cmd.BindVertexBuffer(0, mesh->vertexBuffer, 0);
    if (mesh->indexBuffer.IsValid() && mesh->indexCount > 0) {
        cmd.BindIndexBuffer(mesh->indexBuffer, 0, false);
        cmd.DrawIndexed(mesh->indexCount, 1, 0, 0, 0);
    } else {
        cmd.Draw(mesh->vertexCount, 1, 0, 0);
    }
}

}  // namespace kale::scene
