/**
 * @file render_engine.cpp
 * @brief RenderEngine 初始化顺序实现
 *
 * 顺序：SDL(WindowSystem::Create) → CreateRenderDevice(DeviceConfig 来自 window)
 * → InputManager::Initialize → executor → scheduler → sceneManager
 * → entityManager(scheduler, sceneManager) → resourceManager(scheduler, device) → renderGraph
 */

#include <kale_engine/render_engine.hpp>
#include <kale_device/window_system.hpp>
#include <kale_device/input_manager.hpp>
#include <kale_device/render_device.hpp>
#include <kale_executor/render_task_scheduler.hpp>
#include <kale_scene/scene_manager.hpp>
#include <kale_scene/entity_manager.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <executor/executor.hpp>

#include <chrono>
#include <mutex>

namespace kale {

namespace {

struct RenderEngineImpl {
    std::unique_ptr<kale_device::WindowSystem> windowSystem;
    std::unique_ptr<kale_device::IRenderDevice> renderDevice;
    std::unique_ptr<kale_device::InputManager> inputManager;
    std::unique_ptr<::executor::Executor> executor;
    std::unique_ptr<kale::executor::RenderTaskScheduler> scheduler;
    std::unique_ptr<kale::scene::SceneManager> sceneManager;
    std::unique_ptr<kale::scene::EntityManager> entityManager;
    std::unique_ptr<kale::resource::ResourceManager> resourceManager;
    std::unique_ptr<kale::pipeline::RenderGraph> renderGraph;
    bool runRequestedQuit = false;
};

}  // namespace

namespace {

static std::mutex g_lastErrorMutex;

kale_device::Backend ToDeviceBackend(RenderEngine::Config::BackendType b) {
    return b == RenderEngine::Config::OpenGL ? kale_device::Backend::OpenGL
                                             : kale_device::Backend::Vulkan;
}

}  // namespace

bool RenderEngine::Initialize(const Config& config) {
    Shutdown();
    impl_ = new RenderEngineImpl();
    RenderEngineImpl& impl = *static_cast<RenderEngineImpl*>(impl_);

    // 1. WindowSystem::Create (内部会 SDL_Init 若未初始化)
    impl.windowSystem = std::make_unique<kale_device::WindowSystem>();
    kale_device::WindowConfig wc;
    wc.width = config.width;
    wc.height = config.height;
    wc.title = config.title;
    wc.fullscreen = config.fullscreen;
    wc.resizable = true;
    if (!impl.windowSystem->Create(wc)) {
        lastError_ = "WindowSystem::Create failed";
        Shutdown();
        return false;
    }

    // 2. CreateRenderDevice + DeviceConfig 从 window 传入
    impl.renderDevice = kale_device::CreateRenderDevice(ToDeviceBackend(config.backend));
    if (!impl.renderDevice) {
        lastError_ = "CreateRenderDevice failed";
        Shutdown();
        return false;
    }
    kale_device::DeviceConfig devConfig;
    devConfig.windowHandle = impl.windowSystem->GetNativeHandle();
    devConfig.width = impl.windowSystem->GetWidth();
    devConfig.height = impl.windowSystem->GetHeight();
    devConfig.enableValidation = config.enableValidation;
    devConfig.vsync = config.vsync;
    devConfig.backBufferCount = config.backBufferCount;
    if (!impl.renderDevice->Initialize(devConfig)) {
        std::lock_guard<std::mutex> lock(g_lastErrorMutex);
        lastError_ = impl.renderDevice->GetLastError();
        Shutdown();
        return false;
    }

    // 3. InputManager::Initialize(window)
    impl.inputManager = std::make_unique<kale_device::InputManager>();
    impl.inputManager->Initialize(impl.windowSystem->GetWindow());

    // 4. scheduler（需先创建 executor）
    impl.executor = std::make_unique<::executor::Executor>();
    impl.executor->initialize(::executor::ExecutorConfig{});
    impl.scheduler =
        std::make_unique<kale::executor::RenderTaskScheduler>(impl.executor.get());

    // 5. sceneManager
    impl.sceneManager = std::make_unique<kale::scene::SceneManager>();

    // 6. entityManager(scheduler, sceneManager)
    impl.entityManager = std::make_unique<kale::scene::EntityManager>(
        impl.scheduler.get(), impl.sceneManager.get());

    // 7. resourceManager(scheduler, device, stagingMgr=nullptr)
    impl.resourceManager = std::make_unique<kale::resource::ResourceManager>(
        impl.scheduler.get(), impl.renderDevice.get(), nullptr);

    // 8. renderGraph（注入 scheduler 以支持多线程 RecordPasses）
    impl.renderGraph = std::make_unique<kale::pipeline::RenderGraph>();
    impl.renderGraph->SetScheduler(impl.scheduler.get());

    return true;
}

std::string RenderEngine::GetLastError() const {
    std::lock_guard<std::mutex> lock(g_lastErrorMutex);
    return lastError_;
}

void RenderEngine::Shutdown() {
    if (!impl_) return;
    RenderEngineImpl& impl = *static_cast<RenderEngineImpl*>(impl_);
    // 释放顺序：先依赖资源的，再底层
    impl.renderGraph.reset();
    impl.resourceManager.reset();
    impl.entityManager.reset();
    impl.sceneManager.reset();
    impl.scheduler.reset();
    if (impl.executor) {
        impl.executor->shutdown(true);  // 等待工作线程结束，避免关闭时卡死
    }
    impl.executor.reset();
    impl.inputManager.reset();
    impl.renderDevice.reset();
    impl.windowSystem.reset();
    delete static_cast<RenderEngineImpl*>(impl_);
    impl_ = nullptr;
}

kale_device::IRenderDevice* RenderEngine::GetRenderDevice() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->renderDevice.get();
}

kale_device::InputManager* RenderEngine::GetInputManager() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->inputManager.get();
}

kale_device::WindowSystem* RenderEngine::GetWindowSystem() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->windowSystem.get();
}

kale::resource::ResourceManager* RenderEngine::GetResourceManager() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->resourceManager.get();
}

kale::scene::EntityManager* RenderEngine::GetEntityManager() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->entityManager.get();
}

kale::scene::SceneManager* RenderEngine::GetSceneManager() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->sceneManager.get();
}

kale::pipeline::RenderGraph* RenderEngine::GetRenderGraph() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->renderGraph.get();
}

kale::executor::RenderTaskScheduler* RenderEngine::GetScheduler() {
    if (!impl_) return nullptr;
    return static_cast<RenderEngineImpl*>(impl_)->scheduler.get();
}

void RenderEngine::RequestQuit() {
    if (!impl_) return;
    static_cast<RenderEngineImpl*>(impl_)->runRequestedQuit = true;
}

void RenderEngine::Run(IApplication* app) {
    if (!impl_ || !app) return;
    RenderEngineImpl& impl = *static_cast<RenderEngineImpl*>(impl_);
    impl.runRequestedQuit = false;
    kale_device::InputManager* inputManager = impl.inputManager.get();
    kale::scene::EntityManager* entityManager = impl.entityManager.get();
    kale::scene::SceneManager* sceneManager = impl.sceneManager.get();
    kale_device::IRenderDevice* device = impl.renderDevice.get();
    kale_device::WindowSystem* windowSystem = impl.windowSystem.get();
    if (!inputManager || !device) return;

    std::uint32_t lastWidth = 0;
    std::uint32_t lastHeight = 0;

    using Clock = std::chrono::steady_clock;
    auto tPrev = Clock::now();
    while (!inputManager->QuitRequested() && !impl.runRequestedQuit) {
        auto tNow = Clock::now();
        float deltaTime = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;

        inputManager->Update();

        if (windowSystem) {
            std::uint32_t w = windowSystem->GetWidth();
            std::uint32_t h = windowSystem->GetHeight();
            if (w > 0 && h > 0 && (w != lastWidth || h != lastHeight)) {
                device->SetExtent(w, h);
                lastWidth = w;
                lastHeight = h;
            }
        }

        if (entityManager) entityManager->Update(deltaTime);
        if (sceneManager) sceneManager->Update(deltaTime);
        app->OnUpdate(deltaTime);

        bool skipRender = windowSystem && (windowSystem->GetWidth() == 0 || windowSystem->GetHeight() == 0);
        if (!skipRender) {
            app->OnRender();
            device->Present();
            // 帧边界同步：主线程帧末对 FrameData 调用 end_frame，使本帧写入在下一帧对 read_buffer() 可见
            kale::executor::RenderTaskScheduler* sched = impl.scheduler.get();
            if (sched) {
                kale::executor::FrameData<kale::executor::VisibleObjectList>* fd = sched->GetVisibleObjectsFrameData();
                if (fd) fd->end_frame();
            }
        }
    }
}

}  // namespace kale
