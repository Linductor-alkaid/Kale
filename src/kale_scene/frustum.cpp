/**
 * @file frustum.cpp
 * @brief 视锥剔除实现
 */

#include <kale_scene/frustum.hpp>

#include <cmath>

namespace kale::scene {

namespace {

inline glm::vec4 row(const glm::mat4& m, int r) {
    return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]);
}

inline void normalize_plane(glm::vec4& p) {
    float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 1e-6f) {
        p.x /= len;
        p.y /= len;
        p.z /= len;
        p.w /= len;
    }
}

}  // namespace

FrustumPlanes ExtractFrustumPlanes(const glm::mat4& viewProj) {
    FrustumPlanes f;
    glm::vec4 r0 = row(viewProj, 0);
    glm::vec4 r1 = row(viewProj, 1);
    glm::vec4 r2 = row(viewProj, 2);
    glm::vec4 r3 = row(viewProj, 3);

    f.planes[0] = r3 + r0;   // left
    f.planes[1] = r3 - r0;   // right
    f.planes[2] = r3 + r1;   // bottom
    f.planes[3] = r3 - r1;   // top
    f.planes[4] = r3 + r2;   // near
    f.planes[5] = r3 - r2;   // far

    for (int i = 0; i < 6; ++i)
        normalize_plane(f.planes[i]);

    return f;
}

bool IsBoundsInFrustum(const kale::resource::BoundingBox& bounds, const FrustumPlanes& frustum) {
    const glm::vec3& min = bounds.min;
    const glm::vec3& max = bounds.max;

    for (int i = 0; i < 6; ++i) {
        const glm::vec4& p = frustum.planes[i];
        glm::vec3 pvertex(
            p.x >= 0.f ? max.x : min.x,
            p.y >= 0.f ? max.y : min.y,
            p.z >= 0.f ? max.z : min.z
        );
        if (p.x * pvertex.x + p.y * pvertex.y + p.z * pvertex.z + p.w < 0.f)
            return false;
    }
    return true;
}

}  // namespace kale::scene
