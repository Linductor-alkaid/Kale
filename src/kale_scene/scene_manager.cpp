/**
 * @file scene_manager.cpp
 * @brief SceneManager 实现：句柄注册表、CreateScene、SetActiveScene、Update、CullScene、UpdateRecursive、UnregisterSubtree
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_scene/frustum.hpp>
#include <kale_scene/lod_manager.hpp>
#include <kale_scene/entity_manager.hpp>
#include <kale_scene/scene_node_ref.hpp>
#include <kale_resource/resource_types.hpp>

#include <cassert>
#include <functional>
#include <algorithm>

namespace kale::scene {

std::vector<SceneNode*> SceneManager::CullScene(CameraNode* camera) {
    std::vector<SceneNode*> visibleNodes;
    if (!camera || !activeRoot_) return visibleNodes;

    glm::mat4 viewProj = camera->projectionMatrix * camera->viewMatrix;
    FrustumPlanes frustum = ExtractFrustumPlanes(viewProj);

    std::function<void(SceneNode*)> cullRecursive = [&](SceneNode* node) {
        if (!node->GetRenderable()) {
            for (const auto& child : node->GetChildren())
                cullRecursive(child.get());
            return;
        }

        kale::resource::BoundingBox worldBounds = node->GetRenderable()->GetWorldBounds();
        if (!IsBoundsInFrustum(worldBounds, frustum)) return;

        if (lodManager_) lodManager_->SelectLOD(node, camera);
        visibleNodes.push_back(node);

        for (const auto& child : node->GetChildren())
            cullRecursive(child.get());
    };

    cullRecursive(activeRoot_);

    if (enableOcclusionCulling_ && !visibleNodes.empty())
        OcclusionCull(visibleNodes, camera);

    return visibleNodes;
}

std::vector<std::vector<SceneNode*>> SceneManager::CullScene(const std::vector<CameraNode*>& cameras) {
    std::vector<std::vector<SceneNode*>> visibleByCamera;
    visibleByCamera.reserve(cameras.size());
    for (CameraNode* cam : cameras)
        visibleByCamera.push_back(CullScene(cam));
    return visibleByCamera;
}

void SceneManager::Update(float deltaTime) {
    (void)deltaTime;
    if (!activeRoot_) return;
    UpdateRecursive(activeRoot_, glm::mat4(1.0f));
}

void SceneManager::UpdateRecursive(SceneNode* node, const glm::mat4& parentWorld) {
    glm::mat4 world = parentWorld * node->GetLocalTransform();
    node->SetWorldMatrix(world);
    if (Renderable* r = node->GetRenderable())
        r->UpdateBounds(world);
    for (const auto& child : node->GetChildren())
        UpdateRecursive(child.get(), world);
}

std::unique_ptr<SceneNode> SceneManager::CreateScene() {
    auto root = std::make_unique<SceneNode>();
    RegisterNode(root.get());
    return root;
}

bool SceneManager::IsDescendantOf(SceneNode* parent, SceneNode* node) {
    if (!parent || !node) return false;
    for (SceneNode* n = node; n; n = n->GetParent())
        if (n == parent) return true;
    return false;
}

void SceneManager::SetActiveScene(std::unique_ptr<SceneNode> root, EntityManager* em) {
#ifndef NDEBUG
    if (em && activeRoot_) {
        for (Entity e : em->EntitiesWith<SceneNodeRef>()) {
            SceneNodeRef* ref = em->GetComponent<SceneNodeRef>(e);
            if (!ref || !ref->IsValid()) continue;
            SceneNode* n = ref->GetNode(const_cast<SceneManager*>(this));
            if (n && IsDescendantOf(activeRoot_, n))
                assert(0 && "SetActiveScene: Entity has SceneNodeRef pointing to node in scene being destroyed; unbind first (e.g. SwitchToNewLevel or UnbindSceneNodeRefsPointingToSubtree).");
        }
    }
#endif
    if (activeRootStorage_) {
        UnregisterSubtree(activeRootStorage_.get());
        activeRootStorage_.reset();
    }
    activeRoot_ = nullptr;
    if (root) {
        activeRootStorage_ = std::move(root);
        activeRoot_ = activeRootStorage_.get();
    }
}

void SceneManager::UnregisterSubtree(SceneNode* node) {
    if (!node) return;
    for (const auto& child : node->GetChildren())
        UnregisterSubtree(child.get());
    UnregisterNode(node);
}

SceneNodeHandle SceneManager::GetHandle(SceneNode* node) const {
    if (!node) return kInvalidSceneNodeHandle;
    auto it = nodeToHandle_.find(node);
    if (it == nodeToHandle_.end()) return kInvalidSceneNodeHandle;
    return it->second;
}

SceneNode* SceneManager::GetNode(SceneNodeHandle handle) const {
    if (handle == kInvalidSceneNodeHandle) return nullptr;
    auto it = handleRegistry_.find(handle);
    if (it == handleRegistry_.end()) return nullptr;
    return it->second;
}

SceneNodeHandle SceneManager::RegisterNode(SceneNode* node) {
    if (!node) return kInvalidSceneNodeHandle;
    SceneNodeHandle h = nextHandle_++;
    handleRegistry_[h] = node;
    nodeToHandle_[node] = h;
    node->handle_ = h;  // phase5-5.2: SceneNode handle_ 由 SceneManager 在注册时设置
    return h;
}

void SceneManager::UnregisterNode(SceneNode* node) {
    if (!node) return;
    auto it = nodeToHandle_.find(node);
    if (it == nodeToHandle_.end()) return;
    SceneNodeHandle h = it->second;
    nodeToHandle_.erase(it);
    handleRegistry_.erase(h);
}

namespace {

/** 计算节点世界 AABB 在视空间中的深度范围 [minZ, maxZ]（用于遮挡剔除排序与比较） */
void getViewDepthRange(SceneNode* node, const glm::mat4& viewMatrix, float& outMinZ, float& outMaxZ) {
    kale::resource::BoundingBox worldBounds = node->GetRenderable()->GetWorldBounds();
    const glm::vec3 corners[8] = {
        { worldBounds.min.x, worldBounds.min.y, worldBounds.min.z },
        { worldBounds.max.x, worldBounds.min.y, worldBounds.min.z },
        { worldBounds.min.x, worldBounds.max.y, worldBounds.min.z },
        { worldBounds.max.x, worldBounds.max.y, worldBounds.min.z },
        { worldBounds.min.x, worldBounds.min.y, worldBounds.max.z },
        { worldBounds.max.x, worldBounds.min.y, worldBounds.max.z },
        { worldBounds.min.x, worldBounds.max.y, worldBounds.max.z },
        { worldBounds.max.x, worldBounds.max.y, worldBounds.max.z },
    };
    outMinZ = outMaxZ = glm::vec3(viewMatrix * glm::vec4(corners[0], 1.f)).z;
    for (int i = 1; i < 8; ++i) {
        float z = glm::vec3(viewMatrix * glm::vec4(corners[i], 1.f)).z;
        if (z < outMinZ) outMinZ = z;
        if (z > outMaxZ) outMaxZ = z;
    }
}

}  // namespace

void SceneManager::OcclusionCull(std::vector<SceneNode*>& inOutVisibleNodes, CameraNode* camera) const {
    if (inOutVisibleNodes.empty() || !camera) return;
    if (occlusionHiZBuffer_ != nullptr) {
        /* 预留 Hi-Z 路径：由渲染管线传入 Hi-Z Buffer 时在此做 GPU 遮挡查询；当前为 no-op */
        return;
    }
    /* 软件近似：按视空间深度排序，用最前节点的背面对其后的节点做“完全在后”剔除 */
    std::vector<std::pair<float, float>> depthRanges;
    depthRanges.reserve(inOutVisibleNodes.size());
    const glm::mat4 viewMatrix = camera->viewMatrix;
    for (SceneNode* n : inOutVisibleNodes) {
        float minZ, maxZ;
        getViewDepthRange(n, viewMatrix, minZ, maxZ);
        depthRanges.push_back({ minZ, maxZ });
    }
    /* 按视空间最近深度排序（前到后） */
    std::vector<size_t> order(inOutVisibleNodes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&depthRanges](size_t a, size_t b) {
        return depthRanges[a].first < depthRanges[b].first;
    });
    const float occluderMaxZ = depthRanges[order[0]].second;
    auto it = std::remove_if(order.begin() + 1, order.end(), [&depthRanges, occluderMaxZ](size_t i) {
        return depthRanges[i].first >= occluderMaxZ;  /* 完全在 occluder 背后则剔除 */
    });
    order.erase(it, order.end());
    std::vector<SceneNode*> kept;
    kept.reserve(order.size());
    for (size_t i : order)
        kept.push_back(inOutVisibleNodes[i]);
    inOutVisibleNodes = std::move(kept);
}

}  // namespace kale::scene
