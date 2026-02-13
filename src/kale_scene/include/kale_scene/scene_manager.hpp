/**
 * @file scene_manager.hpp
 * @brief 场景管理器：句柄注册表、场景生命周期、GetHandle/GetNode
 *
 * 与 scene_management_layer_design.md 5.5 对齐。
 * phase5-5.1：handleRegistry_、GetHandle、GetNode。
 * phase5-5.3：CreateScene、SetActiveScene、GetActiveRoot；销毁旧场景时递归 Unregister 并从 handleRegistry 移除。
 */

#pragma once

#include <kale_scene/scene_node.hpp>
#include <kale_scene/scene_types.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace kale::scene {

class CameraNode;
class EntityManager;
class LODManager;

/**
 * 场景管理器：管理场景图节点句柄注册表与活动场景生命周期。
 * 节点创建时通过 RegisterNode 分配 handle 并注册；销毁时通过 UnregisterNode 从注册表移除。
 * SetActiveScene 销毁旧场景（递归 Unregister 后释放），激活新场景并取得所有权。
 */
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    /**
     * 创建根节点，分配 handle 并注册。
     * @return 根节点所有权，调用方可继续 AddChild 后通过 SetActiveScene 激活
     */
    std::unique_ptr<SceneNode> CreateScene();

    /**
     * 销毁旧场景（递归从 handleRegistry 移除并释放），激活新场景并取得所有权。
     * @param root 新场景根节点所有权；若为 nullptr 则仅销毁当前活动场景
     * @param em 可选；Debug 模式下非空时，若存在 Entity 的 SceneNodeRef 指向即将销毁的子树则 assert
     */
    void SetActiveScene(std::unique_ptr<SceneNode> root, EntityManager* em = nullptr);

    /**
     * 每帧更新：递归计算活动场景中所有节点的世界矩阵。
     * 应在 ECS 写回 Scene Graph 之后、OnRender 之前调用。
     * @param deltaTime 帧间隔（保留供后续 UpdateBounds 等扩展）
     */
    void Update(float deltaTime);

    /**
     * 返回当前活动场景根节点；无活动场景时返回 nullptr。
     */
    SceneNode* GetActiveRoot() const { return activeRoot_; }

    /**
     * 单相机场景剔除：递归遍历场景图，对带 Renderable 的节点做视锥测试，返回可见节点列表。
     * 调用前应已调用 Update(deltaTime) 以保证世界矩阵有效。
     * @param camera 相机节点，提供 viewMatrix、projectionMatrix；为 nullptr 时返回空列表
     * @return 通过视锥测试的 SceneNode* 列表（有 Renderable 的节点）
     */
    std::vector<SceneNode*> CullScene(CameraNode* camera);

    /**
     * 多相机场景剔除：对每个相机分别做视锥剔除，返回按相机分组的可见节点列表。
     * visibleByCamera[i] 对应 cameras[i]。
     * @param cameras 相机节点列表；空列表时返回空 vector
     * @return 与 cameras 等长的 vector，每元素为该相机的可见节点列表
     */
    std::vector<std::vector<SceneNode*>> CullScene(const std::vector<CameraNode*>& cameras);

    /**
     * 根据节点指针查找其句柄。
     * @param node 非空且已注册的节点
     * @return 对应句柄，未注册则返回 kInvalidSceneNodeHandle
     */
    SceneNodeHandle GetHandle(SceneNode* node) const;

    /**
     * 根据句柄解析节点指针。
     * @param handle 由 RegisterNode 分配的句柄
     * @return 对应节点指针，已销毁或无效句柄则返回 nullptr
     */
    SceneNode* GetNode(SceneNodeHandle handle) const;

    /**
     * 为节点分配新句柄并注册到注册表。
     * 节点创建时（CreateScene/AddChild）调用。
     * @param node 非空节点指针
     * @return 分配得到的 SceneNodeHandle（永不为 kInvalidSceneNodeHandle）
     */
    SceneNodeHandle RegisterNode(SceneNode* node);

    /**
     * 将节点从注册表移除。
     * 节点销毁时调用，之后 GetNode(handle) 返回 nullptr。
     * @param node 已注册的节点指针；若未注册则无操作
     */
    void UnregisterNode(SceneNode* node);

    /**
     * 设置可选的 LOD 管理器。CullScene 内对可见节点调用 lodManager->SelectLOD(node, camera)。
     * @param mgr 可为 nullptr 表示禁用 LOD 选择
     */
    void SetLODManager(LODManager* mgr) { lodManager_ = mgr; }
    /** 当前 LOD 管理器，未设置时返回 nullptr */
    LODManager* GetLODManager() const { return lodManager_; }

    /**
     * 遮挡剔除开关。CullScene 中视锥剔除后若启用则可选调用 OcclusionCull（依赖 Hi-Z 或软件近似）。
     * @param enable true 表示启用遮挡剔除
     */
    void SetEnableOcclusionCulling(bool enable) { enableOcclusionCulling_ = enable; }
    /** 是否启用遮挡剔除 */
    bool IsOcclusionCullingEnabled() const { return enableOcclusionCulling_; }

    /**
     * 设置可选的 Hi-Z Buffer 供遮挡剔除使用。为 nullptr 时使用软件近似（视空间 AABB 深度）。
     * @param buffer 由渲染管线提供的 Hi-Z 缓冲句柄或 nullptr
     */
    void SetOcclusionHiZBuffer(void* buffer) { occlusionHiZBuffer_ = buffer; }
    /** 当前设置的 Hi-Z Buffer，未设置时返回 nullptr */
    void* GetOcclusionHiZBuffer() const { return occlusionHiZBuffer_; }

    /**
     * 判断 node 是否为 parent 自身或其子树中的节点（即从 node 沿父指针能到达 parent）。
     * @param parent 子树根节点
     * @param node 待判断节点
     * @return node == parent 或 node 在 parent 的子树中时返回 true
     */
    static bool IsDescendantOf(SceneNode* parent, SceneNode* node);

private:
    /** 递归计算世界矩阵：world = parentWorld * node->GetLocalTransform()，并递归子节点 */
    void UpdateRecursive(SceneNode* node, const glm::mat4& parentWorld);

    /** 递归将子树所有节点从注册表移除（先子后父） */
    void UnregisterSubtree(SceneNode* node);

    /**
     * 遮挡剔除：根据 Hi-Z 或相机做软件近似，从可见列表中移除被遮挡的节点。
     * 当 occlusionHiZBuffer_ 非空时预留 Hi-Z 路径（当前为 no-op）；否则用相机做视空间 AABB 深度剔除。
     */
    void OcclusionCull(std::vector<SceneNode*>& inOutVisibleNodes, CameraNode* camera) const;

    /** handle -> node，用于 GetNode */
    std::unordered_map<SceneNodeHandle, SceneNode*> handleRegistry_;
    /** node -> handle，用于 GetHandle（节点创建时注册，销毁时移除） */
    std::unordered_map<SceneNode*, SceneNodeHandle> nodeToHandle_;
    /** 下一个可分配的句柄值（从 1 递增，0 保留为无效） */
    SceneNodeHandle nextHandle_ = 1;
    /** 当前活动场景根节点所有权；释放时递归销毁整棵树 */
    std::unique_ptr<SceneNode> activeRootStorage_;
    /** 当前活动场景根节点指针，与 activeRootStorage_ 一致 */
    SceneNode* activeRoot_ = nullptr;
    /** 可选 LOD 管理器；CullScene 内对可见节点调用 SelectLOD */
    LODManager* lodManager_ = nullptr;
    /** 是否在 CullScene 中视锥剔除后调用遮挡剔除 */
    bool enableOcclusionCulling_ = false;
    /** 可选的 Hi-Z Buffer；为 nullptr 时 OcclusionCull 使用软件近似（视空间 AABB 深度） */
    void* occlusionHiZBuffer_ = nullptr;
};

}  // namespace kale::scene
