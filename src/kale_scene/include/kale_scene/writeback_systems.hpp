/**
 * @file writeback_systems.hpp
 * @brief 写回流程示例：PhysicsSystem / AnimationSystem 通过 SceneNodeRef 写回场景图
 *
 * 与 scene_management_layer_design.md 8.1、scene_management_layer_todolist 3.6 对齐。
 * phase7-7.6：PhysicsSystem 读取 PhysicsComponent 写回 SetLocalTransform；
 * AnimationSystem 声明依赖 PhysicsSystem，在物理之后应用动画偏移。
 */

#pragma once

#include <kale_scene/entity_manager.hpp>
#include <kale_scene/scene_node_ref.hpp>

#include <glm/glm.hpp>
#include <typeindex>
#include <vector>

namespace kale::scene {

/** 物理输出：局部变换，由 PhysicsSystem 写回 SceneNode */
struct PhysicsComponent {
    glm::mat4 localTransform{1.0f};
};

/** 动画偏移：在物理之后应用，AnimationSystem 将 node->GetLocalTransform() * localOffset 写回 */
struct AnimationComponent {
    glm::mat4 localOffset{1.0f};
};

/**
 * 物理系统示例：读取 PhysicsComponent，通过 SceneNodeRef 写回 SetLocalTransform。
 * 无 SceneManager 或节点无效时跳过。
 */
class PhysicsSystem : public System {
public:
    void Update(float /*deltaTime*/, EntityManager& em) override {
        SceneManager* sm = em.GetSceneManager();
        if (!sm) return;
        for (Entity e : em.EntitiesWith<PhysicsComponent, SceneNodeRef>()) {
            PhysicsComponent* phys = em.GetComponent<PhysicsComponent>(e);
            SceneNodeRef* ref = em.GetComponent<SceneNodeRef>(e);
            if (!phys || !ref) continue;
            SceneNode* node = ref->GetNode(sm);
            if (!node) continue;
            node->SetLocalTransform(phys->localTransform);
        }
    }
};

/**
 * 动画系统示例：依赖 PhysicsSystem，在物理写回之后应用动画偏移。
 * 对每个带 AnimationComponent + SceneNodeRef 的实体，将当前 localTransform * localOffset 写回。
 */
class AnimationSystem : public System {
public:
    void Update(float /*deltaTime*/, EntityManager& em) override {
        SceneManager* sm = em.GetSceneManager();
        if (!sm) return;
        for (Entity e : em.EntitiesWith<AnimationComponent, SceneNodeRef>()) {
            AnimationComponent* anim = em.GetComponent<AnimationComponent>(e);
            SceneNodeRef* ref = em.GetComponent<SceneNodeRef>(e);
            if (!anim || !ref) continue;
            SceneNode* node = ref->GetNode(sm);
            if (!node) continue;
            node->SetLocalTransform(node->GetLocalTransform() * anim->localOffset);
        }
    }

    std::vector<std::type_index> GetDependencies() const override {
        return {std::type_index(typeid(PhysicsSystem))};
    }
};

}  // namespace kale::scene
