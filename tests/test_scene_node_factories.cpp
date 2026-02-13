/**
 * @file test_scene_node_factories.cpp
 * @brief 单元测试：phase10-10.4 工厂函数 CreateStaticMeshNode、CreateCameraNode
 */

#include <kale_scene/scene_node_factories.hpp>
#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/static_mesh.hpp>
#include <kale_resource/resource_types.hpp>
#include <glm/glm.hpp>

#include <cassert>
#include <iostream>

using namespace kale::scene;
using namespace kale::resource;

static void test_create_camera_node() {
    std::unique_ptr<CameraNode> cam = CreateCameraNode();
    assert(cam && "CreateCameraNode() must return non-null");
    assert(cam->fov == 60.0f && "default fov");
    assert(cam->nearPlane == 0.1f && "default nearPlane");
    assert(cam->farPlane == 1000.0f && "default farPlane");
    assert(cam->GetChildren().empty() && "no children");
    assert(cam->GetRenderable() == nullptr && "camera has no renderable");

    SceneManager mgr;
    std::unique_ptr<SceneNode> root = mgr.CreateScene();
    SceneNode* camPtr = root->AddChild(std::move(cam));
    assert(camPtr && "AddChild(CreateCameraNode()) must succeed");
    assert(dynamic_cast<CameraNode*>(camPtr) != nullptr && "child is CameraNode");
    assert(root->GetChildren().size() == 1u);
}

static void test_create_static_mesh_node_null() {
    std::unique_ptr<SceneNode> node = CreateStaticMeshNode(nullptr, nullptr);
    assert(node && "CreateStaticMeshNode(nullptr, nullptr) must return non-null");
    Renderable* r = node->GetRenderable();
    assert(r && "node must have owned StaticMesh renderable");
    assert(r->GetMesh() == nullptr && "mesh is null");
    assert(r->GetMaterial() == nullptr && "material is null");
    kale::resource::BoundingBox b = r->GetBounds();
    assert(b.min == b.max && "default empty bounds");
}

static void test_create_static_mesh_node_with_mesh() {
    Mesh mesh;
    mesh.bounds.min = glm::vec3(-1.f, -1.f, -1.f);
    mesh.bounds.max = glm::vec3(1.f, 1.f, 1.f);
    mesh.vertexCount = 3;
    mesh.indexCount = 0;

    std::unique_ptr<SceneNode> node = CreateStaticMeshNode(&mesh, nullptr);
    assert(node && "CreateStaticMeshNode(&mesh, nullptr) must return non-null");
    Renderable* r = node->GetRenderable();
    assert(r && "node must have renderable");
    assert(r->GetMesh() == &mesh && "GetMesh() must return same pointer");
    assert(r->GetMaterial() == nullptr && "material is null");
    kale::resource::BoundingBox b = r->GetBounds();
    assert(b.min == mesh.bounds.min && b.max == mesh.bounds.max && "GetBounds() from mesh");
}

static void test_factory_node_add_child() {
    Mesh mesh;
    mesh.vertexCount = 1;
    std::unique_ptr<SceneNode> child = CreateStaticMeshNode(&mesh, nullptr);
    SceneManager mgr;
    std::unique_ptr<SceneNode> root = mgr.CreateScene();
    SceneNode* childPtr = root->AddChild(std::move(child));
    assert(childPtr && "AddChild(factory node) must succeed");
    assert(childPtr->GetRenderable() != nullptr && "renderable still valid after AddChild");
    assert(childPtr->GetRenderable()->GetMesh() == &mesh);
    assert(root->GetChildren().size() == 1u);
}

int main() {
    test_create_camera_node();
    test_create_static_mesh_node_null();
    test_create_static_mesh_node_with_mesh();
    test_factory_node_add_child();
    std::cout << "test_scene_node_factories: all checks passed.\n";
    return 0;
}
