/**
 * Render Demo - 完整渲染示例
 *
 * 展示 Kale 引擎完整流程：
 * - RenderEngine 初始化
 * - 程序化立方体网格 + 简单材质
 * - 场景图、相机、SubmitVisibleToRenderGraph
 * - Forward Pass 渲染
 * - 相机旋转、ESC 退出
 */

#include <kale_engine/render_engine.hpp>
#include <kale_device/input_manager.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/window_system.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_pipeline/forward_pass.hpp>
#include <kale_pipeline/submit_visible.hpp>
#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node_factories.hpp>
#include <kale_scene/static_mesh.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_resource/shader_compiler.hpp>
#include <kale_pipeline/material.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {

// 顶点格式：position(3) + normal(3) + uv(2) = 8 float，与 ModelLoader VertexPNT 一致
struct VertexPNT {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// 程序化创建立方体网格
std::unique_ptr<kale::resource::Mesh> CreateCubeMesh(kale_device::IRenderDevice* device) {
    if (!device) return nullptr;

    // 立方体 8 顶点，每面 2 三角形
    const float s = 0.5f;
    const VertexPNT vertices[] = {
        // 前面 (z+)
        {-s, -s, s,  0, 0, 1,  0, 0}, {s, -s, s,  0, 0, 1,  1, 0}, {s, s, s,  0, 0, 1,  1, 1}, {-s, s, s,  0, 0, 1,  0, 1},
        // 后面 (z-)
        {-s, -s, -s,  0, 0, -1,  1, 0}, {-s, s, -s,  0, 0, -1,  1, 1}, {s, s, -s,  0, 0, -1,  0, 1}, {s, -s, -s,  0, 0, -1,  0, 0},
        // 右面 (x+)
        {s, -s, s,  1, 0, 0,  0, 0}, {s, -s, -s,  1, 0, 0,  1, 0}, {s, s, -s,  1, 0, 0,  1, 1}, {s, s, s,  1, 0, 0,  0, 1},
        // 左面 (x-)
        {-s, -s, -s,  -1, 0, 0,  0, 0}, {-s, -s, s,  -1, 0, 0,  1, 0}, {-s, s, s,  -1, 0, 0,  1, 1}, {-s, s, -s,  -1, 0, 0,  0, 1},
        // 顶面 (y+)
        {-s, s, s,  0, 1, 0,  0, 0}, {s, s, s,  0, 1, 0,  1, 0}, {s, s, -s,  0, 1, 0,  1, 1}, {-s, s, -s,  0, 1, 0,  0, 1},
        // 底面 (y-)
        {-s, -s, -s,  0, -1, 0,  0, 0}, {s, -s, -s,  0, -1, 0,  1, 0}, {s, -s, s,  0, -1, 0,  1, 1}, {-s, -s, s,  0, -1, 0,  0, 1},
    };
    const std::uint32_t indices[] = {
        0, 1, 2, 0, 2, 3,       // 前
        4, 5, 6, 4, 6, 7,       // 后
        8, 9, 10, 8, 10, 11,    // 右
        12, 13, 14, 12, 14, 15, // 左
        16, 17, 18, 16, 18, 19, // 上
        20, 21, 22, 20, 22, 23, // 下
    };

    kale_device::BufferDesc vbDesc;
    vbDesc.size = sizeof(vertices);
    vbDesc.usage = kale_device::BufferUsage::Vertex;
    vbDesc.cpuVisible = false;

    kale_device::BufferDesc ibDesc;
    ibDesc.size = sizeof(indices);
    ibDesc.usage = kale_device::BufferUsage::Index;
    ibDesc.cpuVisible = false;

    auto vb = device->CreateBuffer(vbDesc, vertices);
    auto ib = device->CreateBuffer(ibDesc, indices);
    if (!vb.IsValid() || !ib.IsValid()) {
        if (vb.IsValid()) device->DestroyBuffer(vb);
        if (ib.IsValid()) device->DestroyBuffer(ib);
        return nullptr;
    }

    auto mesh = std::make_unique<kale::resource::Mesh>();
    mesh->vertexBuffer = vb;
    mesh->indexBuffer = ib;
    mesh->vertexCount = 24;
    mesh->indexCount = 36;
    mesh->topology = kale_device::PrimitiveTopology::TriangleList;
    mesh->bounds.min = glm::vec3(-s, -s, -s);
    mesh->bounds.max = glm::vec3(s, s, s);
    mesh->subMeshes.push_back({0, 36, 0});
    return mesh;
}

// 加载 SPIR-V 着色器
std::vector<std::uint8_t> LoadSPIRV(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> code(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(code.data()), size)) return {};
    return code;
}

/**
 * 仅使用 push constant 的简单材质，不绑定 descriptor set。
 * 标准 Material 会绑定 instance descriptor set，但我们的 pipeline 无 set 布局，会导致 Vulkan 崩溃。
 */
class SimplePushConstantMaterial : public kale::pipeline::Material {
public:
    void BindForDraw(kale_device::CommandList& cmd,
                     kale_device::IRenderDevice* device,
                     const void* instanceData,
                     std::size_t instanceSize) override {
        (void)device;
        (void)instanceSize;
        if (GetPipeline().IsValid())
            cmd.BindPipeline(GetPipeline());
        // 仅 push constant，不绑定 descriptor set（pipeline 无 set 布局）
        if (instanceData && instanceSize > 0)
            cmd.SetPushConstants(instanceData, instanceSize, 0);
    }
};

// 创建简单无纹理材质（使用 push constant 的 MVP）
std::unique_ptr<kale::pipeline::Material> CreateCubeMaterial(
    kale_device::IRenderDevice* device,
    const std::string& shaderDir) {
    if (!device) return nullptr;

    auto vertCode = LoadSPIRV(shaderDir + "/cube.vert.spv");
    auto fragCode = LoadSPIRV(shaderDir + "/cube.frag.spv");
    if (vertCode.empty() || fragCode.empty()) return nullptr;

    kale_device::ShaderDesc vertDesc;
    vertDesc.stage = kale_device::ShaderStage::Vertex;
    vertDesc.code = vertCode;
    kale_device::ShaderDesc fragDesc;
    fragDesc.stage = kale_device::ShaderStage::Fragment;
    fragDesc.code = fragCode;

    auto vertShader = device->CreateShader(vertDesc);
    auto fragShader = device->CreateShader(fragDesc);
    if (!vertShader.IsValid() || !fragShader.IsValid()) return nullptr;

    kale_device::PipelineDesc pipelineDesc;
    pipelineDesc.shaders = {vertShader, fragShader};
    pipelineDesc.vertexBindings = {{0, sizeof(VertexPNT), false}};
    pipelineDesc.vertexAttributes = {
        {0, 0, kale_device::Format::RGB32F, offsetof(VertexPNT, px)},
        {1, 0, kale_device::Format::RGB32F, offsetof(VertexPNT, nx)},
        {2, 0, kale_device::Format::RG32F, offsetof(VertexPNT, u)},
    };
    pipelineDesc.topology = kale_device::PrimitiveTopology::TriangleList;
    pipelineDesc.rasterization.cullEnable = true;
    pipelineDesc.rasterization.frontFaceCCW = true;
    // 当前 Vulkan BeginRenderPass 在 writesSwapchain 时忽略 depth attachment，无深度缓冲。
    // 禁用 depth test 以显示立方体；后续需在设备层支持 color+depth 的 render pass。
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    pipelineDesc.colorAttachmentFormats = {kale_device::Format::RGBA8_SRGB};
    pipelineDesc.depthAttachmentFormat = kale_device::Format::D24S8;

    auto pipeline = device->CreatePipeline(pipelineDesc);
    if (!pipeline.IsValid()) return nullptr;

    auto mat = std::make_unique<SimplePushConstantMaterial>();
    mat->SetPipeline(pipeline);
    return mat;
}

}  // namespace

struct RenderDemoApp : kale::IApplication {
    kale::RenderEngine* engine = nullptr;
    kale::scene::SceneNode* sceneRoot = nullptr;
    kale::scene::CameraNode* camera = nullptr;
    std::unique_ptr<kale::resource::Mesh> cubeMesh;
    std::unique_ptr<kale::pipeline::Material> cubeMaterial;
    float cameraAngle = 0.f;

    void OnUpdate(float deltaTime) override {
        if (!engine) return;
        auto* input = engine->GetInputManager();
        if (input && input->IsKeyJustPressed(kale_device::KeyCode::Escape))
            engine->RequestQuit();

        cameraAngle += deltaTime * 0.5f;
        if (camera) {
            float r = 4.f;
            glm::vec3 pos(std::sin(cameraAngle) * r, 1.5f, std::cos(cameraAngle) * r);
            camera->SetLocalTransform(glm::translate(glm::mat4(1.f), pos));
        }
    }

    void OnRender() override {
        if (!engine) return;
        auto* sceneMgr = engine->GetSceneManager();
        auto* rg = engine->GetRenderGraph();
        auto* device = engine->GetRenderDevice();
        if (!sceneMgr || !rg || !device) return;

        if (camera) {
            float aspect = static_cast<float>(engine->GetWindowSystem()->GetWidth()) /
                          static_cast<float>(std::max(1u, engine->GetWindowSystem()->GetHeight()));
            camera->UpdateViewProjection(aspect, true);  // true = Vulkan NDC Y 翻转
            rg->SetViewProjection(camera->viewMatrix, camera->projectionMatrix);
        }

        rg->ClearSubmitted();
        kale::pipeline::SubmitVisibleToRenderGraph(sceneMgr, rg, camera);
        rg->Execute(device);
    }
};

int main() {
    kale::RenderEngine engine;
    kale::RenderEngine::Config config;
    config.width = 1024;
    config.height = 768;
    config.title = "Kale Render Demo - 完整渲染示例";
    config.enableValidation = false;

    if (!engine.Initialize(config)) {
        std::cerr << "RenderEngine::Initialize failed: " << engine.GetLastError() << "\n";
        return 1;
    }

    auto* device = engine.GetRenderDevice();
    auto* sceneMgr = engine.GetSceneManager();
    auto* rg = engine.GetRenderGraph();
    if (!device || !sceneMgr || !rg) {
        std::cerr << "GetRenderDevice/GetSceneManager/GetRenderGraph failed\n";
        engine.Shutdown();
        return 1;
    }

    // 着色器路径：优先运行目录下的 shaders，其次 build 输出目录
    std::string shaderDir = "shaders";
    if (std::ifstream(shaderDir + "/cube.vert.spv").fail())
        shaderDir = "apps/render_demo/shaders";
#ifdef RENDER_DEMO_SHADER_DIR
    if (std::ifstream(shaderDir + "/cube.vert.spv").fail())
        shaderDir = RENDER_DEMO_SHADER_DIR;
#endif

    auto cubeMesh = CreateCubeMesh(device);
    auto cubeMaterial = CreateCubeMaterial(device, shaderDir);
    if (!cubeMesh || !cubeMaterial) {
        std::cerr << "CreateCubeMesh or CreateCubeMaterial failed. "
                  << "Ensure shaders are compiled to " << shaderDir << "/cube.vert.spv and cube.frag.spv\n";
        engine.Shutdown();
        return 1;
    }

    // 创建场景
    auto sceneRoot = sceneMgr->CreateScene();
    if (!sceneRoot) {
        std::cerr << "CreateScene failed\n";
        engine.Shutdown();
        return 1;
    }

    // 添加立方体节点（3x3 网格）
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            auto node = kale::scene::CreateStaticMeshNode(cubeMesh.get(), cubeMaterial.get());
            node->SetLocalTransform(
                glm::translate(glm::mat4(1.f), glm::vec3(x * 1.5f, 0.f, z * 1.5f)));
            node->SetPassFlags(kale::scene::PassFlags::Opaque);
            sceneRoot->AddChild(std::move(node));
        }
    }

    // 添加相机
    auto cameraNode = kale::scene::CreateCameraNode();
    cameraNode->SetLocalTransform(glm::translate(glm::mat4(1.f), glm::vec3(0.f, 1.5f, 4.f)));
    kale::scene::CameraNode* camera = static_cast<kale::scene::CameraNode*>(sceneRoot->AddChild(std::move(cameraNode)));

    sceneMgr->SetActiveScene(std::move(sceneRoot));

    // 配置 Render Graph（Forward Pass）
    rg->SetResolution(config.width, config.height);
    rg->SetScheduler(nullptr);  // 单线程录制，避免关闭时 executor 卡死
    kale::pipeline::SetupForwardPassWithCamera(*rg);
    if (!rg->Compile(device)) {
        std::cerr << "RenderGraph::Compile failed: " << rg->GetLastError() << "\n";
        engine.Shutdown();
        return 1;
    }

    RenderDemoApp app;
    app.engine = &engine;
    app.sceneRoot = sceneMgr->GetActiveRoot();
    app.camera = camera;
    app.cubeMesh = std::move(cubeMesh);
    app.cubeMaterial = std::move(cubeMaterial);
    engine.Run(&app);

    engine.Shutdown();
    std::cout << "Render Demo exited.\n";
    return 0;
}
