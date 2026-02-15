// Hello Kale - 最小示例（phase11-11.9 主循环 Run()）
// 验证 RenderEngine::Initialize + Run(IApplication) 与 Forward Pass + 占位三角形绘制（phase15-15.2）

#include <kale_engine/render_engine.hpp>
#include <kale_device/input_manager.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_pipeline/forward_pass.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/material.hpp>
#include <kale_scene/static_mesh.hpp>
#include <kale_scene/scene_types.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <glm/glm.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> LoadSPIRV(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> code(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(code.data()), size)) return {};
    return code;
}

// 占位符顶点格式与 resource_manager CreatePlaceholders 一致：position(3) + normal(3) + uv(2)
struct PlaceholderVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

/** 仅绑定 pipeline，无 descriptor set，用于占位三角形 */
class TrianglePlaceholderMaterial : public kale::pipeline::Material {
public:
    void BindForDraw(kale_device::CommandList& cmd,
                    kale_device::IRenderDevice* /*device*/,
                    const void* /*instanceData*/,
                    std::size_t /*instanceSize*/) override {
        if (GetPipeline().IsValid())
            cmd.BindPipeline(GetPipeline());
    }
};

}  // namespace

struct HelloKaleApp : kale::IApplication {
    kale::RenderEngine* engine = nullptr;
    std::unique_ptr<kale::scene::StaticMesh> triangleRenderable;
    int frames = 0;

    void OnUpdate(float /*deltaTime*/) override {
        if (!engine) return;
        kale_device::InputManager* input = engine->GetInputManager();
        if (input && input->IsKeyJustPressed(kale_device::KeyCode::Escape))
            engine->RequestQuit();
    }

    void OnRender() override {
        if (!engine) return;
        kale::pipeline::RenderGraph* rg = engine->GetRenderGraph();
        kale_device::IRenderDevice* device = engine->GetRenderDevice();
        if (!rg || !device) return;
        rg->ClearSubmitted();
        if (triangleRenderable)
            rg->SubmitRenderable(triangleRenderable.get(), glm::mat4(1.f), kale::scene::PassFlags::All);
        rg->Execute(device);
        ++frames;
        if (frames <= 3 || frames % 60 == 0)
            std::cout << "Frame " << frames << "\n";
    }
};

int main() {
    kale::RenderEngine engine;
    kale::RenderEngine::Config config;
    config.width = 800;
    config.height = 600;
    config.title = "Hello Kale - Run()";
    config.enableValidation = false;

    if (!engine.Initialize(config)) {
        std::cerr << "RenderEngine::Initialize failed: " << engine.GetLastError() << "\n";
        return 1;
    }
    std::cout << "RenderEngine initialized.\n";

    kale::pipeline::RenderGraph* rg = engine.GetRenderGraph();
    kale_device::IRenderDevice* device = engine.GetRenderDevice();
    kale::resource::ResourceManager* resMgr = engine.GetResourceManager();
    if (!rg || !device || !resMgr) {
        std::cerr << "GetRenderGraph/GetRenderDevice/GetResourceManager failed\n";
        engine.Shutdown();
        return 1;
    }

    kale::resource::Mesh* placeholderMesh = resMgr->GetPlaceholderMesh();
    if (!placeholderMesh) {
        std::cerr << "GetPlaceholderMesh failed\n";
        engine.Shutdown();
        return 1;
    }

    // 加载 triangle_mesh.vert + triangle.frag，创建仅 pipeline 的材质用于占位三角形
    std::string shaderDir = "shaders";
    if (LoadSPIRV(shaderDir + "/triangle_mesh.vert.spv").empty())
        shaderDir = "apps/hello_kale/shaders";
    auto vertCode = LoadSPIRV(shaderDir + "/triangle_mesh.vert.spv");
    auto fragCode = LoadSPIRV(shaderDir + "/triangle.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "Shader load failed (tried " << shaderDir << "). Run from build or build/apps/hello_kale.\n";
        engine.Shutdown();
        return 1;
    }

    kale_device::ShaderDesc vertDesc{kale_device::ShaderStage::Vertex, vertCode};
    kale_device::ShaderDesc fragDesc{kale_device::ShaderStage::Fragment, fragCode};
    auto vertShader = device->CreateShader(vertDesc);
    auto fragShader = device->CreateShader(fragDesc);
    if (!vertShader.IsValid() || !fragShader.IsValid()) {
        std::cerr << "CreateShader failed\n";
        engine.Shutdown();
        return 1;
    }

    const std::uint32_t stride = static_cast<std::uint32_t>(sizeof(PlaceholderVertex));
    kale_device::PipelineDesc pipelineDesc;
    pipelineDesc.shaders = {vertShader, fragShader};
    pipelineDesc.vertexBindings = {{0, stride, false}};
    pipelineDesc.vertexAttributes = {
        {0, 0, kale_device::Format::RGB32F, offsetof(PlaceholderVertex, px)},
        {1, 0, kale_device::Format::RGB32F, offsetof(PlaceholderVertex, nx)},
        {2, 0, kale_device::Format::RG32F, offsetof(PlaceholderVertex, u)},
    };
    pipelineDesc.topology = kale_device::PrimitiveTopology::TriangleList;
    pipelineDesc.colorAttachmentFormats = {kale_device::Format::RGBA8_SRGB};

    auto pipeline = device->CreatePipeline(pipelineDesc);
    if (!pipeline.IsValid()) {
        std::cerr << "CreatePipeline failed\n";
        device->DestroyShader(vertShader);
        device->DestroyShader(fragShader);
        engine.Shutdown();
        return 1;
    }

    auto triangleMaterial = std::make_unique<TrianglePlaceholderMaterial>();
    triangleMaterial->SetPipeline(pipeline);
    auto triangleRenderable = std::make_unique<kale::scene::StaticMesh>(
        placeholderMesh, static_cast<kale::resource::Material*>(triangleMaterial.get()));

    rg->SetResolution(800, 600);
    kale::pipeline::SetupForwardOnlyRenderGraph(*rg);
    if (!rg->Compile(device)) {
        std::cerr << "RenderGraph::Compile failed: " << rg->GetLastError() << "\n";
        engine.Shutdown();
        return 1;
    }
    std::cout << "RenderGraph compiled (Forward Pass only).\n";

    HelloKaleApp app;
    app.engine = &engine;
    app.triangleRenderable = std::move(triangleRenderable);
    // 材质由 triangleRenderable 非占有使用；保持 triangleMaterial 存活
    static std::unique_ptr<TrianglePlaceholderMaterial> s_triangleMaterial = std::move(triangleMaterial);
    engine.Run(&app);

    engine.Shutdown();
    std::cout << "Exited after " << app.frames << " frames.\n";
    return 0;
}
