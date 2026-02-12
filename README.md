# Kale

Vulkan + SDL3 渲染引擎，采用模块化分层架构，支持 Scene Graph、ECS、Render Graph 等现代渲染技术。

## 特性

- **分层架构**：设备抽象层、执行器层、资源管理层、场景管理层、渲染管线层
- **Vulkan 优先**：现代图形 API，支持多线程命令录制
- **场景图 + ECS**：Transform 层级与游戏逻辑分离，通过 SceneNodeRef 桥接
- **Render Graph**：声明式渲染管线，Pass 依赖自动分析
- **异步加载**：基于 executor 的非阻塞资源加载

## 目录结构

```
kale/
├── CMakeLists.txt              # 总 CMake 配置
├── README.md
├── src/
│   ├── kale_device/            # 设备抽象层 (RDI、窗口、输入)
│   │   ├── CMakeLists.txt
│   │   └── ...
│   ├── kale_executor/          # 执行器层 (RenderTaskScheduler)
│   ├── kale_resource/         # 资源管理层
│   ├── kale_scene/             # 场景管理层 (Scene Graph、ECS)
│   ├── kale_pipeline/          # 渲染管线层 (Render Graph、Material)
│   └── kale_engine/            # 引擎主入口
├── apps/                       # 示例应用
│   └── hello_kale/
└── docs/                       # 设计文档与任务清单
    ├── design/
    └── todolists/
```

## 构建

### 依赖

| 依赖 | 说明 |
|------|------|
| CMake 3.16+ | 构建系统 |
| C++20 | 编译器 |
| glm | 数学库（未找到时自动 FetchContent） |
| Vulkan | 图形 API（可选，设备层需要） |
| SDL3 | 窗口与输入（可选，设备层需要） |
| executor | 任务调度库（需指定路径） |

### executor 集成

executor 为 Kale 的任务调度基础，需在配置时指定路径：

```bash
cmake -B build -DKALE_EXECUTOR_PATH=/path/to/executor
cmake --build build
```

或安装后使用 `find_package(executor)`：

```bash
cmake -B build
cmake --build build
```

### 使用 vcpkg

```bash
cmake -B build -DKALE_USE_VCPKG=ON -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 构建选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `KALE_BUILD_APPS` | ON | 构建示例应用 |
| `KALE_ENABLE_VALIDATION` | ON | Vulkan Validation Layer |
| `KALE_ENABLE_OPENGL_BACKEND` | OFF | 构建 OpenGL 后端 |
| `KALE_EXECUTOR_PATH` | - | executor 源码路径 |
| `KALE_USE_VCPKG` | OFF | 使用 vcpkg 管理依赖 |

## 模块独立构建

各子模块均可独立配置与构建，依赖关系如下：

```
kale_device     (无依赖)
kale_executor   → executor
kale_resource   → kale_device, kale_executor
kale_scene      → kale_device, kale_executor, kale_resource
kale_pipeline   → kale_device, kale_executor, kale_resource, kale_scene
kale_engine     → 所有上述模块
```

仅构建指定模块（例如设备层）：

```bash
cmake -B build -DKALE_EXECUTOR_PATH=/path/to/executor
cmake --build build --target kale_device
```

## 文档

- [架构设计](docs/design/rendering_engine_design.md)
- [渲染管线层设计](docs/design/rendering_pipeline_layer_design.md)
- [场景管理层设计](docs/design/scene_management_layer_design.md)
- [设备抽象层设计](docs/design/device_abstraction_layer_design.md)
- [资源管理层设计](docs/design/resource_management_layer_design.md)
- [执行器层设计](docs/design/executor_layer_design.md)
- [代码示例](docs/design/rendering_engine_code_examples.md)
- [任务清单](docs/todolists/)

## 技术栈

- **Vulkan**：图形 API
- **SDL3**：窗口与输入
- **executor**：任务调度
- **VMA**：Vulkan 内存分配
- **glm**：数学库
- **tinygltf**：glTF 加载
- **stb_image**：图像加载

## 许可证

本项目采用 [GNU Affero General Public License v3.0](LICENSE) 许可。
