/**
 * @file test_bounding_box.cpp
 * @brief 单元测试：phase5-5.7 BoundingBox 与 TransformBounds
 */

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <kale_resource/resource_types.hpp>

static bool vec3_near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

int main() {
    using namespace kale::resource;

    // 1. BoundingBox 默认构造
    BoundingBox empty;
    if (empty.min != glm::vec3(0.f) || empty.max != glm::vec3(0.f))
        return 1;

    // 2. 单位矩阵变换保持不变
    BoundingBox box;
    box.min = glm::vec3(-1.f, -1.f, -1.f);
    box.max = glm::vec3(1.f, 1.f, 1.f);
    glm::mat4 identity(1.f);
    BoundingBox t1 = box.Transform(identity);
    if (!vec3_near(t1.min, box.min) || !vec3_near(t1.max, box.max))
        return 2;
    BoundingBox t2 = TransformBounds(box, identity);
    if (!vec3_near(t2.min, box.min) || !vec3_near(t2.max, box.max))
        return 3;

    // 3. 平移变换
    glm::mat4 translate = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, -5.f));
    BoundingBox translated = TransformBounds(box, translate);
    if (!vec3_near(translated.min, glm::vec3(9.f, -1.f, -6.f)) ||
        !vec3_near(translated.max, glm::vec3(11.f, 1.f, -4.f)))
        return 4;

    // 4. 缩放变换
    glm::mat4 scale = glm::scale(glm::mat4(1.f), glm::vec3(2.f, 3.f, 0.5f));
    BoundingBox scaled = box.Transform(scale);
    if (!vec3_near(scaled.min, glm::vec3(-2.f, -3.f, -0.5f)) ||
        !vec3_near(scaled.max, glm::vec3(2.f, 3.f, 0.5f)))
        return 5;

    // 5. 平移+缩放组合
    glm::mat4 combined = glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
    combined = glm::scale(combined, glm::vec3(2.f, 2.f, 2.f));
    BoundingBox small;
    small.min = glm::vec3(0.f, 0.f, 0.f);
    small.max = glm::vec3(1.f, 1.f, 1.f);
    BoundingBox result = TransformBounds(small, combined);
    if (!vec3_near(result.min, glm::vec3(1.f, 0.f, 0.f)) ||
        !vec3_near(result.max, glm::vec3(3.f, 2.f, 2.f)))
        return 6;

    return 0;
}
