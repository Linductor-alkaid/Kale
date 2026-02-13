/**
 * @file test_frustum.cpp
 * @brief 单元测试：phase5-5.8 视锥剔除 ExtractFrustumPlanes、IsBoundsInFrustum
 */

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_scene/frustum.hpp>

static bool float_near(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

int main() {
    using namespace kale::resource;
    using namespace kale::scene;

    // 1. ExtractFrustumPlanes：使用透视+视图矩阵，平面应被归一化
    float fov = glm::radians(60.f);
    float aspect = 16.f / 9.f;
    float nearPlane = 0.1f;
    float farPlane = 1000.f;
    glm::mat4 proj = glm::perspective(fov, aspect, nearPlane, farPlane);
    glm::mat4 view = glm::lookAt(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 viewProj = proj * view;

    FrustumPlanes frustum = ExtractFrustumPlanes(viewProj);
    for (int i = 0; i < 6; ++i) {
        const glm::vec4& p = frustum.planes[i];
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (!float_near(len, 1.f, 1e-4f))
            return 1;  // 平面法线应归一化
    }

    // 2. IsBoundsInFrustum：相机前方、视锥内的 AABB 应可见
    BoundingBox inFront;
    inFront.min = glm::vec3(-0.5f, -0.5f, -3.f);
    inFront.max = glm::vec3(0.5f, 0.5f, -2.f);
    if (!IsBoundsInFrustum(inFront, frustum))
        return 2;

    // 3. 完全在相机后方的 AABB 应不可见
    BoundingBox behind;
    behind.min = glm::vec3(-1.f, -1.f, 1.f);
    behind.max = glm::vec3(1.f, 1.f, 2.f);
    if (IsBoundsInFrustum(behind, frustum))
        return 3;

    // 4. 远在视锥一侧外的 AABB 应不可见（例如右侧很远）
    BoundingBox rightFar;
    rightFar.min = glm::vec3(1000.f, -1.f, -10.f);
    rightFar.max = glm::vec3(1001.f, 1.f, -9.f);
    if (IsBoundsInFrustum(rightFar, frustum))
        return 4;

    // 5. 原点附近小 AABB（在 near 与 far 之间且在 FOV 内）应可见
    BoundingBox originBox;
    originBox.min = glm::vec3(-0.01f, -0.01f, -1.f);
    originBox.max = glm::vec3(0.01f, 0.01f, -0.5f);
    if (!IsBoundsInFrustum(originBox, frustum))
        return 5;

    // 6. 退化 AABB（min==max）在视锥内应可见
    BoundingBox pointBox;
    pointBox.min = glm::vec3(0.f, 0.f, -2.f);
    pointBox.max = glm::vec3(0.f, 0.f, -2.f);
    if (!IsBoundsInFrustum(pointBox, frustum))
        return 6;

    return 0;
}
