/**
 * @file frustum.hpp
 * @brief 视锥剔除：FrustumPlanes 与 AABB 测试
 *
 * 与 scene_management_layer_design.md 附录 A.2 对齐。
 * phase5-5.8：FrustumPlanes、ExtractFrustumPlanes、IsBoundsInFrustum。
 */

#pragma once

#include <kale_resource/resource_types.hpp>

#include <glm/glm.hpp>
#include <glm/vec4.hpp>

namespace kale::scene {

/**
 * 视锥六面体平面（平面方程 ax + by + cz + d = 0，法线指向视锥外）。
 * 顺序：left, right, bottom, top, near, far。
 * 点在平面负侧（ax+by+cz+d < 0）表示在视锥外。
 */
struct FrustumPlanes {
    glm::vec4 planes[6];
};

/**
 * 从视图-投影矩阵提取视锥六个平面（列主序，与 GLM 一致）。
 * @param viewProj 通常为 projectionMatrix * viewMatrix
 * @return 归一化后的六个平面
 */
FrustumPlanes ExtractFrustumPlanes(const glm::mat4& viewProj);

/**
 * 判断轴对齐包围盒是否在视锥内（与任一平面相交或在内侧即视为可见）。
 * @param bounds 世界空间 AABB（min/max）
 * @param frustum 由 ExtractFrustumPlanes 得到的视锥平面
 * @return 若包围盒与视锥有交集则 true，完全在外则 false
 */
bool IsBoundsInFrustum(const kale::resource::BoundingBox& bounds, const FrustumPlanes& frustum);

}  // namespace kale::scene
