/**
 * @file static_mesh.hpp
 * @brief 静态网格 Renderable：按路径持有 mesh/material，Draw 时检查就绪、占位符与触发 LoadAsync
 *
 * 与 resource_management_layer_design.md 5.11、phase4-4.6 对齐。
 * 未就绪时使用占位符绘制，并仅在一次创建占位条目时触发 LoadAsync，避免重复触发。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <kale_scene/renderable.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_handle.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <glm/glm.hpp>

namespace kale::scene {

/**
 * 静态网格可渲染对象：meshPath_ / materialPath_ 通过 ResourceManager 解析，
 * Draw 时若未就绪则用占位符并触发 LoadAsync（仅当占位条目新创建时触发一次）。
 */
class StaticMesh : public Renderable {
public:
    /**
     * @param resourceManager 资源管理器（非空），用于 Get/IsReady/GetOrCreatePlaceholder/LoadAsync/GetPlaceholder*
     * @param meshPath 网格路径（如 "models/box.gltf"）
     * @param materialPath 材质路径（可为空，空则仅用占位材质）
     */
    StaticMesh(kale::resource::ResourceManager* resourceManager,
               std::string meshPath,
               std::string materialPath = "");

    /**
     * 直接使用已加载的 Mesh 与 Material 指针（非占有）。
     * 用于工厂 CreateStaticMeshNode(mesh, material)；调用方需保证指针在 Renderable 使用期间有效。
     */
    StaticMesh(kale::resource::Mesh* mesh, kale::resource::Material* material);

    kale::resource::BoundingBox GetBounds() const override;
    const kale::resource::Mesh* GetMesh() const override;
    const kale::resource::Material* GetMaterial() const override;
    size_t GetLODCount() const override;
    void SetCurrentLOD(uint32_t lod) override;
    void Draw(kale_device::CommandList& cmd, const glm::mat4& worldTransform,
          kale_device::IRenderDevice* device = nullptr) override;
    void ReleaseFrameResources() override;

    /**
     * 设置多 LOD 网格句柄（LOD 0 为最高细节）。
     * 非空时 GetLODCount() 返回 handles.size()，GetMesh() 按 currentLOD_ 返回对应 mesh。
     */
    void SetLODHandles(std::vector<kale::resource::MeshHandle> handles);

private:
    kale::resource::ResourceManager* resourceManager_ = nullptr;
    std::string meshPath_;
    std::string materialPath_;

    kale::resource::MeshHandle meshHandle_{};
    kale::resource::MaterialHandle materialHandle_{};

    /** 直接指针模式（工厂构造）：非占有，用于 CreateStaticMeshNode */
    kale::resource::Mesh* meshPtr_ = nullptr;
    kale::resource::Material* materialPtr_ = nullptr;

    /** 当前选中的 LOD 索引（由 LODManager::SelectLOD 设置） */
    uint32_t currentLOD_ = 0;
    /** 多 LOD 网格句柄；非空时使用，GetMesh() 返回 meshLODHandles_[currentLOD_] 对应 mesh */
    std::vector<kale::resource::MeshHandle> meshLODHandles_;
};

}  // namespace kale::scene
