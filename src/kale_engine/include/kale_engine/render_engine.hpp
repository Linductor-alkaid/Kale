/**
 * @file render_engine.hpp
 * @brief RenderEngine 引擎主入口：初始化顺序、子系统聚合
 *
 * 与 rendering_engine_design.md、device_abstraction_layer_design.md 8.1 对齐。
 * 初始化顺序：SDL/Window → CreateRenderDevice → InputManager → scheduler → sceneManager
 * → entityManager(scheduler, sceneManager) / resourceManager(scheduler) / renderGraph
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

// 前向声明，避免引擎层依赖各层具体头文件在头文件中展开
namespace kale_device {
class WindowSystem;
class InputManager;
class IRenderDevice;
struct DeviceConfig;
enum class Backend;
}  // namespace kale_device

namespace kale::executor {
class RenderTaskScheduler;
}  // namespace kale::executor

namespace kale::scene {
class SceneManager;
class EntityManager;
}  // namespace kale::scene

namespace kale::resource {
class ResourceManager;
class StagingMemoryManager;
}  // namespace kale::resource

namespace kale::pipeline {
class RenderGraph;
}  // namespace kale::pipeline

namespace kale {

/**
 * 渲染引擎：聚合各层，提供统一初始化顺序与子系统访问。
 * 初始化失败时 Initialize() 返回 false，GetLastError() 返回原因。
 */
class RenderEngine {
public:
    struct Config {
        uint32_t width = 1920;
        uint32_t height = 1080;
        std::string title = "Kale";
        bool fullscreen = false;
        bool vsync = true;
        bool enableValidation = false;
        uint32_t backBufferCount = 3;
        enum BackendType { Vulkan, OpenGL } backend = Vulkan;
        std::string assetPath = "./assets";
        std::string shaderPath = "./shaders";
    };

    RenderEngine() = default;
    /** 内联析构仅调用 Shutdown()，实际释放在 .cpp 中完成 */
    ~RenderEngine() { Shutdown(); }

    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    /**
     * 按设计顺序初始化各层。
     * 顺序：WindowSystem::Create (含 SDL_Init) → CreateRenderDevice(DeviceConfig 来自 window)
     * → InputManager::Initialize → scheduler → sceneManager → entityManager(scheduler, sceneManager)
     * → resourceManager(scheduler, device, staging) → renderGraph
     * @return 成功 true，失败 false，此时 GetLastError() 可获取原因
     */
    bool Initialize(const Config& config);

    /** 初始化失败时的详细错误信息（线程安全） */
    std::string GetLastError() const;

    /** 反初始化并释放所有子系统 */
    void Shutdown();

    kale_device::IRenderDevice* GetRenderDevice();
    kale_device::InputManager* GetInputManager();
    kale_device::WindowSystem* GetWindowSystem();
    kale::resource::ResourceManager* GetResourceManager();
    kale::scene::EntityManager* GetEntityManager();
    kale::scene::SceneManager* GetSceneManager();
    kale::pipeline::RenderGraph* GetRenderGraph();
    kale::executor::RenderTaskScheduler* GetScheduler();

private:
    std::string lastError_;
    void* impl_ = nullptr;  // 实际为 Impl*，在 .cpp 中分配/释放，避免头文件依赖完整 Impl
};

}  // namespace kale
