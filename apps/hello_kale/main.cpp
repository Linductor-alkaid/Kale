// Hello Kale - 最小示例（phase11-11.9 主循环 Run()）
// 验证 RenderEngine::Initialize + Run(IApplication) 与 Forward Pass + 输入

#include <kale_engine/render_engine.hpp>
#include <kale_device/input_manager.hpp>
#include <kale_pipeline/forward_pass.hpp>
#include <iostream>

struct HelloKaleApp : kale::IApplication {
    kale::RenderEngine* engine = nullptr;
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
        if (rg && device) {
            rg->ClearSubmitted();
            rg->Execute(device);
        }
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
    if (!rg || !device) {
        std::cerr << "GetRenderGraph/GetRenderDevice failed\n";
        engine.Shutdown();
        return 1;
    }
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
    engine.Run(&app);

    engine.Shutdown();
    std::cout << "Exited after " << app.frames << " frames.\n";
    return 0;
}
