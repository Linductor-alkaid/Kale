/**
 * @file scene_node.hpp
 * @brief 场景节点：局部/世界变换、父子层级、句柄、Renderable 挂载
 *
 * 与 scene_management_layer_design.md 5.3 对齐。
 * phase5-5.2：SceneNode 核心（localTransform、worldMatrix、AddChild、GetParent/GetChildren、GetHandle）。
 * phase5-5.5：Pass 标志 SetPassFlags/GetPassFlags，默认 PassFlags::All。
 * phase5-5.6：Renderable 挂载 SetRenderable/GetRenderable，非占有指针。
 */

#pragma once

#include <kale_scene/scene_types.hpp>
#include <kale_scene/renderable.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace kale::scene {

class SceneManager;

/**
 * 场景图节点：局部变换、世界矩阵（由 SceneManager::Update 计算）、父子层级。
 * handle_ 由 SceneManager 在 CreateScene/AddChild 时通过 RegisterNode 后设置。
 */
class SceneNode {
public:
    SceneNode() = default;
    ~SceneNode() = default;

    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;

    /** 设置局部变换矩阵 */
    void SetLocalTransform(const glm::mat4& t) { localTransform_ = t; }
    /** 获取局部变换矩阵 */
    const glm::mat4& GetLocalTransform() const { return localTransform_; }

    /**
     * 获取世界矩阵。
     * 由 SceneManager::Update 递归计算后只读，应用层不应修改。
     */
    const glm::mat4& GetWorldMatrix() const { return worldMatrix_; }

    /**
     * 添加子节点，接管所有权。
     * @return 子节点指针，供调用方继续配置或交给 SceneManager 注册句柄
     */
    SceneNode* AddChild(std::unique_ptr<SceneNode> child) {
        if (!child) return nullptr;
        child->parent_ = this;
        SceneNode* p = child.get();
        children_.push_back(std::move(child));
        return p;
    }

    /** 父节点指针，根节点为 nullptr */
    SceneNode* GetParent() const { return parent_; }
    /** 只读子节点列表 */
    const std::vector<std::unique_ptr<SceneNode>>& GetChildren() const { return children_; }

    /** 本节点句柄，由 SceneManager 在创建/加入场景时设置 */
    SceneNodeHandle GetHandle() const { return handle_; }

    /** 设置参与哪些渲染 Pass（ShadowCaster、Opaque、Transparent），默认 All */
    void SetPassFlags(PassFlags f) { passFlags_ = f; }
    /** 获取当前 Pass 标志 */
    PassFlags GetPassFlags() const { return passFlags_; }

    /** 挂载可渲染对象（非占有指针，生命周期由调用方管理） */
    void SetRenderable(Renderable* r) { renderable_ = r; }
    /**
     * 挂载可渲染对象并取得所有权（节点析构时一并释放）。
     * 用于工厂创建的 Renderable（如 CreateStaticMeshNode 内创建的 StaticMesh）。
     */
    void SetOwnedRenderable(std::unique_ptr<Renderable> r) {
        ownedRenderable_ = std::move(r);
        renderable_ = ownedRenderable_ ? ownedRenderable_.get() : nullptr;
    }
    /** 获取挂载的 Renderable，未挂载时返回 nullptr */
    Renderable* GetRenderable() const {
        return ownedRenderable_ ? ownedRenderable_.get() : renderable_;
    }

private:
    /** 仅供 SceneManager::UpdateRecursive 调用，用于写入世界矩阵 */
    void SetWorldMatrix(const glm::mat4& m) { worldMatrix_ = m; }

    SceneNodeHandle handle_ = kInvalidSceneNodeHandle;
    glm::mat4 localTransform_{1.0f};
    glm::mat4 worldMatrix_{1.0f};
    std::vector<std::unique_ptr<SceneNode>> children_;
    SceneNode* parent_ = nullptr;
    PassFlags passFlags_ = PassFlags::All;
    Renderable* renderable_ = nullptr;  ///< 非占有指针，供 CullScene / SubmitRenderable 使用
    std::unique_ptr<Renderable> ownedRenderable_;  ///< 可选所有权，工厂创建的 Renderable

    friend class SceneManager;
};

}  // namespace kale::scene
