/**
 * @file test_camera_node.cpp
 * @brief phase10-10.1 CameraNode 单元测试
 *
 * 覆盖：默认 fov/nearPlane/farPlane、viewMatrix/projectionMatrix、
 * UpdateViewProjection 更新投影与视图、aspectRatio 影响投影、
 * 与 SceneManager 配合时 view = inverse(world)。
 */

#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_manager.hpp>

#include <glm/glm.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

static bool near_eq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

int main() {
    using namespace kale::scene;

    // 默认成员
    CameraNode cam;
    TEST_CHECK(near_eq(cam.fov, 60.0f));
    TEST_CHECK(near_eq(cam.nearPlane, 0.1f));
    TEST_CHECK(near_eq(cam.farPlane, 1000.0f));
    TEST_CHECK(cam.viewMatrix[0][0] == 1.0f && cam.viewMatrix[3][3] == 1.0f);
    TEST_CHECK(cam.projectionMatrix[0][0] == 1.0f && cam.projectionMatrix[3][3] == 1.0f);

    // UpdateViewProjection() 后投影矩阵应为透视（典型：最后一列 [2][2]、[3][2] 非 0/-1 形式）
    cam.UpdateViewProjection();
    float p22 = cam.projectionMatrix[2][2];
    float p32 = cam.projectionMatrix[3][2];
    TEST_CHECK(std::fabs(p22) > 0.1f && std::fabs(p32) > 0.1f);

    // 默认世界矩阵为单位，view 应为单位
    TEST_CHECK(near_eq(cam.viewMatrix[0][0], 1.0f) && near_eq(cam.viewMatrix[1][1], 1.0f));

    // 不同 aspectRatio 产生不同投影矩阵
    cam.UpdateViewProjection(1.0f);
    float p00_1 = cam.projectionMatrix[0][0];
    cam.UpdateViewProjection(2.0f);
    float p00_2 = cam.projectionMatrix[0][0];
    TEST_CHECK(std::fabs(p00_1 - p00_2) > 1e-5f);

    // 与 SceneManager 配合：相机作为子节点，Update 后世界矩阵已设置，UpdateViewProjection 后 view = inverse(world)
    SceneManager mgr;
    auto root = mgr.CreateScene();
    auto cameraNode = std::make_unique<CameraNode>();
    CameraNode* cameraPtr = cameraNode.get();
    root->AddChild(std::move(cameraNode));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    cameraPtr->SetLocalTransform(glm::mat4(1.0f));  // 确保有 Update 后 world 已写
    mgr.Update(0.0f);
    cameraPtr->UpdateViewProjection(16.f / 9.f);
    glm::mat4 view = cameraPtr->viewMatrix;
    glm::mat4 world = cameraPtr->GetWorldMatrix();
    glm::mat4 product = view * world;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            TEST_CHECK(near_eq(product[i][j], i == j ? 1.0f : 0.0f));

    std::cout << "test_camera_node: all checks passed.\n";
    return 0;
}
