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

StaticMesh::StaticMesh(kale::resource::Mesh* mesh, kale::resource::Material* material)
    : meshPtr_(mesh), materialPtr_(material) {
    if (mesh)
        bounds_ = mesh->bounds;
}

kale::resource::BoundingBox StaticMesh::GetBounds() const {
    if (meshPtr_)
        return meshPtr_->bounds;
    return bounds_;
}

const kale::resource::Mesh* StaticMesh::GetMesh() const {
    if (meshPtr_) return meshPtr_;
    if (!resourceManager_ || !meshHandle_.IsValid()) return nullptr;
    return resourceManager_->Get<kale::resource::Mesh>(meshHandle_);
}

const kale::resource::Material* StaticMesh::GetMaterial() const {
    if (materialPtr_) return materialPtr_;
    if (!resourceManager_ || !materialHandle_.IsValid()) return nullptr;
    return resourceManager_->Get<kale::resource::Material>(materialHandle_);
}

void StaticMesh::Draw(kale_device::CommandList& cmd, const glm::mat4& worldTransform,
                      kale_device::IRenderDevice* device) {
    const kale::resource::Mesh* mesh = nullptr;
    kale::resource::Material* material = nullptr;

    if (meshPtr_) {
        mesh = meshPtr_;
        material = materialPtr_;
    } else {
        if (!resourceManager_) return;
        // Mesh：确保有句柄（GetOrCreatePlaceholder），未就绪则用占位符；仅在新创建占位条目时触发 LoadAsync，避免重复
        if (!meshHandle_.IsValid()) {
            auto [h, created] = resourceManager_->GetOrCreatePlaceholder<kale::resource::Mesh>(meshPath_);
            meshHandle_ = h;
            if (created)
                resourceManager_->LoadAsync<kale::resource::Mesh>(meshPath_);
        }
        mesh = resourceManager_->Get<kale::resource::Mesh>(meshHandle_);
        if (!mesh)
            mesh = resourceManager_->GetPlaceholderMesh();

        // Material：同上
        if (!materialPath_.empty() && !materialHandle_.IsValid()) {
            auto [h, created] = resourceManager_->GetOrCreatePlaceholder<kale::resource::Material>(materialPath_);
            materialHandle_ = h;
            if (created)
                resourceManager_->LoadAsync<kale::resource::Material>(materialPath_);
        }
        if (materialHandle_.IsValid())
            material = const_cast<kale::resource::Material*>(
                resourceManager_->Get<kale::resource::Material>(materialHandle_));
        if (!material)
            material = resourceManager_->GetPlaceholderMaterial();
    }

    if (material)
        material->BindForDraw(cmd, device, &worldTransform, sizeof(glm::mat4));

    if (!mesh) return;

    // 更新包围盒供 CullScene 使用（路径模式且尚未从 mesh 设置）
    if (!meshPtr_ && bounds_.min == bounds_.max && mesh->vertexCount > 0) {
        bounds_ = mesh->bounds;
    }

    if (!mesh->vertexBuffer.IsValid()) return;

    cmd.SetPushConstants(&worldTransform, sizeof(glm::mat4), 0);
    cmd.BindVertexBuffer(0, mesh->vertexBuffer, 0);
    if (mesh->indexBuffer.IsValid() && mesh->indexCount > 0) {
        cmd.BindIndexBuffer(mesh->indexBuffer, 0, false);
        cmd.DrawIndexed(mesh->indexCount, 1, 0, 0, 0);
    } else {
        cmd.Draw(mesh->vertexCount, 1, 0, 0);
    }
}

void StaticMesh::ReleaseFrameResources() {
    kale::resource::Material* mat = materialPtr_
        ? materialPtr_
        : (materialHandle_.IsValid() && resourceManager_
           ? const_cast<kale::resource::Material*>(
                 resourceManager_->Get<kale::resource::Material>(materialHandle_))
           : nullptr);
    if (!mat && resourceManager_)
        mat = resourceManager_->GetPlaceholderMaterial();
    if (mat)
        mat->ReleaseFrameResources();
}

}  // namespace kale::scene
