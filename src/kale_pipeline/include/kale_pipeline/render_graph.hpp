/**
 * @file render_graph.hpp
 * @brief Render Graph 声明式接口：声明资源与 Pass，供 Compile/Execute 使用
 *
 * 与 rendering_pipeline_layer_design.md 5.3 对齐。
 * phase6-6.9：RenderGraph 声明式接口。
 *
 * DeclareTexture/DeclareBuffer 声明资源并返回 RGResourceHandle；
 * AddPass 注册 Pass 的 Setup 与 Execute 回调；
 * SetResolution 影响 DeclareTexture 时未指定尺寸的默认宽高。
 * Compile/Execute 在 phase6-6.11 / 6.12 实现。
 */

#pragma once

#include <kale_pipeline/rg_types.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>
#include <kale_scene/scene_types.hpp>
#include <kale_executor/render_task_scheduler.hpp>

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <queue>
#include <thread>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace kale::pipeline {

/**
 * Pass Setup 回调：通过 RenderPassBuilder 声明本 Pass 的读/写依赖。
 */
using RenderPassSetup = std::function<void(RenderPassBuilder&)>;

/**
 * Pass Execute 回调：在录制时执行，使用 RenderPassContext 与 CommandList。
 */
using RenderPassExecute = std::function<void(const RenderPassContext&, kale_device::CommandList&)>;

/**
 * 声明式 Render Graph。
 * 支持 DeclareTexture、DeclareBuffer、AddPass、SetResolution；
 * Compile 与 Execute 由后续 phase 实现。
 */
class RenderGraph {
public:
    RenderGraph() = default;

    /** 设置调度器；非空时 RecordPasses 按 GetTopologicalGroups 并行录制，nullptr 时退化为单线程。 */
    void SetScheduler(kale::executor::RenderTaskScheduler* scheduler) { scheduler_ = scheduler; }
    kale::executor::RenderTaskScheduler* GetScheduler() const { return scheduler_; }

    /**
     * 设置分辨率；DeclareTexture 时若 desc 的 width/height 为 0，将使用此处设置的宽高。
     */
    void SetResolution(std::uint32_t width, std::uint32_t height) {
        resolutionWidth_ = width;
        resolutionHeight_ = height;
    }

    /** 当前分辨率宽 */
    std::uint32_t GetResolutionWidth() const { return resolutionWidth_; }
    /** 当前分辨率高 */
    std::uint32_t GetResolutionHeight() const { return resolutionHeight_; }

    /**
     * 设置退出检查回调；Execute 在等待 GPU 时若超时会调用，返回 true 则跳过本帧并退出。
     * 用于在长时间阻塞时保持窗口响应（如处理关闭事件）。
     */
    void SetQuitCallback(std::function<bool()> cb) { quitCallback_ = std::move(cb); }

    /**
     * 设置覆盖的输出目标（多视口/离屏，phase12-12.6）。
     * 当有效时，WriteSwapchain 类 Pass 写入此纹理而非 swapchain；用于小地图、多相机输出到离屏纹理等。
     * 设为无效句柄后恢复使用 GetBackBuffer()。
     */
    void SetOutputTarget(kale_device::TextureHandle target) { outputTarget_ = target; }
    /** 当前覆盖的输出目标；无效表示使用 swapchain。 */
    kale_device::TextureHandle GetOutputTarget() const { return outputTarget_; }

    /**
     * 声明一张纹理，返回 RG 内部句柄。
     * 若 desc.width 或 desc.height 为 0，则使用 SetResolution 设置的宽高。
     * 同名重复声明返回同一句柄（未定义行为或可规定为返回已有句柄，此处采用返回已有句柄）。
     */
    RGResourceHandle DeclareTexture(const std::string& name, kale_device::TextureDesc desc) {
        auto it = nameToHandle_.find(name);
        if (it != nameToHandle_.end()) {
            auto& r = resources_[it->second - 1];
            if (r.isTexture) return it->second;
            // 同名已存在且为 buffer，未定义；仍返回新 handle 以保持声明式语义简单
        }
        if (desc.width == 0) desc.width = resolutionWidth_;
        if (desc.height == 0) desc.height = resolutionHeight_;
        RGResourceHandle h = nextHandle_++;
        resources_.push_back(DeclaredResource{true, name, desc, {}});
        nameToHandle_[name] = h;
        return h;
    }

    /**
     * 声明一个缓冲，返回 RG 内部句柄。
     * 同名重复声明返回同一句柄。
     */
    RGResourceHandle DeclareBuffer(const std::string& name, const kale_device::BufferDesc& desc) {
        auto it = nameToHandle_.find(name);
        if (it != nameToHandle_.end()) {
            auto& r = resources_[it->second - 1];
            if (!r.isTexture) return it->second;
        }
        RGResourceHandle h = nextHandle_++;
        resources_.push_back(DeclaredResource{false, name, {}, desc});
        nameToHandle_[name] = h;
        return h;
    }

    /**
     * 添加一个渲染 Pass，返回 RenderPassHandle。
     * setup 在 AddPass 时或 Compile 时被调用以声明依赖；execute 在 Execute 时被调用。
     */
    RenderPassHandle AddPass(const std::string& name,
                             RenderPassSetup setup,
                             RenderPassExecute execute) {
        passes_.push_back(PassEntry{name, std::move(setup), std::move(execute)});
        return static_cast<RenderPassHandle>(passes_.size() - 1);
    }

    /**
     * 应用层显式提交一条绘制项（由 CullScene 后遍历可见节点调用）。
     * @param renderable 可绘制对象（非占有）
     * @param worldTransform 世界变换矩阵
     * @param passFlags 参与哪些 Pass（默认 All）
     */
    void SubmitRenderable(kale::scene::Renderable* renderable,
                         const glm::mat4& worldTransform,
                         kale::scene::PassFlags passFlags = kale::scene::PassFlags::All) {
        if (!renderable) return;
        submittedDraws_.push_back(SubmittedDraw{renderable, worldTransform, passFlags});
    }

    /** 清空本帧已提交的绘制项，每帧开始时由应用层在 SubmitRenderable 前调用。 */
    void ClearSubmitted() { submittedDraws_.clear(); }

    /** 只读访问本帧已提交的绘制列表（供 Execute 中 BuildFrameDrawList / RenderPassContext 使用）。 */
    const std::vector<SubmittedDraw>& GetSubmittedDraws() const { return submittedDraws_; }

    /**
     * 设置当前帧的视图与投影矩阵（供 Forward 等 Pass 计算 MVP）。
     * 应用层每帧在 Execute 前调用，通常从 CameraNode::viewMatrix 与 projectionMatrix 传入。
     */
    void SetViewProjection(const glm::mat4& view, const glm::mat4& projection) {
        viewMatrix_ = view;
        projectionMatrix_ = projection;
    }
    const glm::mat4& GetViewMatrix() const { return viewMatrix_; }
    const glm::mat4& GetProjectionMatrix() const { return projectionMatrix_; }

    /**
     * 编译 Render Graph：依赖分析、资源分配、构建拓扑序。
     * 分辨率/管线变化时调用。失败时返回 false，GetLastError() 返回原因；
     * 资源分配失败时会释放本轮已分配的资源。
     */
    bool Compile(kale_device::IRenderDevice* device);

    /**
     * 执行一帧渲染（每帧由应用层在 SubmitRenderable 后调用）。
     * 流程：WaitFence → ResetFence → AcquireNextImage → BuildFrameDrawList → RecordPasses → Submit → ReleaseFrameResources。
     * Present() 由应用层在 Execute 返回后调用。
     */
    void Execute(kale_device::IRenderDevice* device);

    /**
     * 执行一帧渲染到指定输出目标（多视口/离屏，phase12-12.6）。
     * 本帧 WriteSwapchain 类 Pass 写入 outputTarget 而非 swapchain；调用后不改变 SetOutputTarget 状态（仅本帧生效）。
     * 若 outputTarget 无效，等价于 Execute(device)。
     */
    void Execute(kale_device::IRenderDevice* device, kale_device::TextureHandle outputTarget);

    /**
     * 帧末回收本帧提交的 Renderable 占用的帧内资源（如实例级 DescriptorSet）。
     * Execute() 内部在 Submit 后调用；也可由测试或自定义流程单独调用。
     */
    void ReleaseFrameResources();

    /** Compile 失败时的错误信息（空表示无错误或未执行过 Compile）。 */
    const std::string& GetLastError() const { return compileError_; }

    /** 是否已成功 Compile（有拓扑序且资源已分配）。 */
    bool IsCompiled() const { return !topologicalOrder_.empty(); }

    /** 拓扑序下的 Pass 句柄列表（Execute 时按此顺序录制）。 */
    const std::vector<RenderPassHandle>& GetTopologicalOrder() const { return topologicalOrder_; }

    /**
     * 按依赖分组：同组内 Pass 无依赖可并行录制，组间按拓扑序串行。
     * 仅 Compile 后有效；未 Compile 或存在环时返回空。
     * @return 各组为 std::vector<RenderPassHandle>，groups[i] 为第 i 层，同层可并行。
     */
    std::vector<std::vector<RenderPassHandle>> GetTopologicalGroups() const;

    /**
     * 将 RG 纹理句柄解析为 RDI TextureHandle（仅 Compile 成功后有效）。
     * 若 handle 对应 Buffer 或无效，返回 id=0 的 TextureHandle。
     */
    kale_device::TextureHandle GetCompiledTexture(RGResourceHandle handle) const;

    /**
     * 将 RG 缓冲句柄解析为 RDI BufferHandle（仅 Compile 成功后有效）。
     * 若 handle 对应 Texture 或无效，返回 id=0 的 BufferHandle。
     */
    kale_device::BufferHandle GetCompiledBuffer(RGResourceHandle handle) const;

    /**
     * 某 Pass 编译后的读写信息（Compile 时由 Setup 填充，Execute 时用于绑定 RT）。
     * 下标与 GetPasses() 一致。
     */
    struct CompiledPassInfo {
        std::vector<std::pair<std::uint32_t, RGResourceHandle>> colorOutputs;
        RGResourceHandle depthOutput = kInvalidRGResourceHandle;
        std::vector<RGResourceHandle> readTextures;
        bool writesSwapchain = false;
        bool executeWithoutRenderPass = false;
    };
    const std::vector<CompiledPassInfo>& GetCompiledPassInfo() const { return compiledPassInfo_; }

    // --- 供 Compile/Execute 等后续 phase 使用的访问接口 ---

    struct DeclaredResource {
        bool isTexture = true;
        std::string name;
        kale_device::TextureDesc texDesc;
        kale_device::BufferDesc bufDesc;
    };

    const std::vector<DeclaredResource>& GetDeclaredResources() const { return resources_; }
    const std::unordered_map<std::string, RGResourceHandle>& GetNameToHandle() const {
        return nameToHandle_;
    }
    RGResourceHandle GetHandleByName(const std::string& name) const {
        auto it = nameToHandle_.find(name);
        return it != nameToHandle_.end() ? it->second : kInvalidRGResourceHandle;
    }

    struct PassEntry {
        std::string name;
        RenderPassSetup setup;
        RenderPassExecute execute;
    };

    const std::vector<PassEntry>& GetPasses() const { return passes_; }

    /** 按拓扑序录制所有 Pass（有 scheduler 时同组内并行），返回本帧的 CommandList 列表。供 Execute 与测试使用。 */
    std::vector<kale_device::CommandList*> RecordPasses(kale_device::IRenderDevice* device);

private:
    /** Pass 依赖分析：根据 compiledPassInfo_ 的读写构建有向边，返回拓扑序；若存在环则返回空。 */
    std::vector<RenderPassHandle> BuildTopologicalOrder() const;
    /** 按依赖分层：同层内无依赖，层间按拓扑序。仅 Compile 后有效。 */
    std::vector<std::vector<RenderPassHandle>> BuildTopologicalGroups() const;
    /** 整理本帧绘制列表（当前为 submittedDraws_ 的引用，预留排序等扩展）。 */
    void BuildFrameDrawList();
    /** 录制单个 Pass，返回 CommandList*（由调用方 EndCommandList 后填入列表或 result）。 */
    kale_device::CommandList* RecordOnePass(kale_device::IRenderDevice* device,
                                           RenderPassHandle passIdx,
                                           std::uint32_t threadIndex,
                                           RenderPassContext& ctx);

    std::uint32_t resolutionWidth_ = 0;
    std::uint32_t resolutionHeight_ = 0;
    RGResourceHandle nextHandle_ = 1;
    std::vector<DeclaredResource> resources_;
    std::unordered_map<std::string, RGResourceHandle> nameToHandle_;
    std::vector<PassEntry> passes_;
    /** 每帧由应用层 SubmitRenderable 填入，ClearSubmitted 清空；Execute 时供 RenderPassContext 使用。 */
    std::vector<SubmittedDraw> submittedDraws_;

    /** Compile 产物：拓扑序、每 Pass 读写信息、RG -> RDI 句柄映射。 */
    std::vector<RenderPassHandle> topologicalOrder_;
    std::vector<CompiledPassInfo> compiledPassInfo_;
    std::vector<kale_device::TextureHandle> compiledTextures_;
    std::vector<kale_device::BufferHandle> compiledBuffers_;
    std::string compileError_;

    /** 帧流水线：Fence 在 Compile 时创建，Execute 中 Wait/Reset，Submit 时传入以 signal。 */
    static constexpr std::uint32_t kMaxFramesInFlight = 3;
    std::vector<kale_device::FenceHandle> frameFences_;
    std::uint32_t currentFrameIndex_ = 0;

    kale::executor::RenderTaskScheduler* scheduler_ = nullptr;

    /** 覆盖的输出目标；有效时 WriteSwapchain 写入此纹理而非 GetBackBuffer()（phase12-12.6）。 */
    kale_device::TextureHandle outputTarget_;

    /** 当前帧视图/投影矩阵，供 Pass 计算 MVP；由 SetViewProjection 设置。 */
    glm::mat4 viewMatrix_{1.f};
    glm::mat4 projectionMatrix_{1.f};

    std::function<bool()> quitCallback_;
};

// -----------------------------------------------------------------------------
// Compile 与句柄解析实现
// -----------------------------------------------------------------------------

inline bool RenderGraph::Compile(kale_device::IRenderDevice* device) {
    compileError_.clear();
    if (!device) {
        compileError_ = "Compile: device is null";
        return false;
    }

    // 释放上一轮 Compile 创建的资源，再清空状态
    for (size_t i = 0; i < compiledTextures_.size(); ++i) {
        if (compiledTextures_[i].IsValid()) device->DestroyTexture(compiledTextures_[i]);
    }
    for (size_t i = 0; i < compiledBuffers_.size(); ++i) {
        if (compiledBuffers_[i].IsValid()) device->DestroyBuffer(compiledBuffers_[i]);
    }
    topologicalOrder_.clear();
    compiledPassInfo_.clear();
    compiledTextures_.clear();
    compiledBuffers_.clear();

    // 1) 运行每个 Pass 的 Setup，填充 compiledPassInfo_
    compiledPassInfo_.resize(passes_.size());
    for (size_t i = 0; i < passes_.size(); ++i) {
        RenderPassBuilder builder;
        if (passes_[i].setup) passes_[i].setup(builder);
        auto& info = compiledPassInfo_[i];
        info.colorOutputs = builder.GetColorOutputs();
        info.depthOutput = builder.GetDepthOutput();
        info.readTextures = builder.GetReadTextures();
        info.writesSwapchain = builder.WritesSwapchain();
        info.executeWithoutRenderPass = builder.ExecuteWithoutRenderPass();
    }

    // 2) 依赖分析，构建拓扑序
    topologicalOrder_ = BuildTopologicalOrder();
    if (topologicalOrder_.empty() && !passes_.empty()) {
        compileError_ = "Compile: pass dependency cycle detected";
        return false;
    }

    // 3) 资源分配：按 resources_ 创建 RDI 资源，建立 RG -> RDI 映射
    const size_t nRes = resources_.size();
    compiledTextures_.resize(nRes, kale_device::TextureHandle{});
    compiledBuffers_.resize(nRes, kale_device::BufferHandle{});

    for (size_t i = 0; i < nRes; ++i) {
        const auto& r = resources_[i];
        if (r.isTexture) {
            auto h = device->CreateTexture(r.texDesc, nullptr);
            if (!h.IsValid()) {
                compileError_ = "Compile: CreateTexture failed for resource '" + r.name + "'";
                for (size_t j = 0; j < i; ++j) {
                    if (resources_[j].isTexture)
                        device->DestroyTexture(compiledTextures_[j]);
                    else
                        device->DestroyBuffer(compiledBuffers_[j]);
                }
                compiledTextures_.clear();
                compiledBuffers_.clear();
                topologicalOrder_.clear();
                compiledPassInfo_.clear();
                return false;
            }
            compiledTextures_[i] = h;
        } else {
            auto h = device->CreateBuffer(r.bufDesc, nullptr);
            if (!h.IsValid()) {
                compileError_ = "Compile: CreateBuffer failed for resource '" + r.name + "'";
                for (size_t j = 0; j < i; ++j) {
                    if (resources_[j].isTexture)
                        device->DestroyTexture(compiledTextures_[j]);
                    else
                        device->DestroyBuffer(compiledBuffers_[j]);
                }
                compiledTextures_.clear();
                compiledBuffers_.clear();
                topologicalOrder_.clear();
                compiledPassInfo_.clear();
                return false;
            }
            compiledBuffers_[i] = h;
        }
    }

    // 4) 帧流水线 Fence：首次 Compile 时创建，供 Execute 使用
    if (frameFences_.size() != kMaxFramesInFlight) {
        frameFences_.clear();
        for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            auto f = device->CreateFence(true);  // signaled 避免首帧 Wait 阻塞
            if (!f.IsValid()) {
                frameFences_.clear();
                return true;  // 不因 Fence 创建失败而整体失败，Execute 时可不传 fence
            }
            frameFences_.push_back(f);
        }
    }

    return true;
}

inline kale_device::TextureHandle RenderGraph::GetCompiledTexture(RGResourceHandle handle) const {
    if (handle == kInvalidRGResourceHandle || handle == 0) return kale_device::TextureHandle{};
    size_t idx = static_cast<size_t>(handle - 1);
    if (idx >= resources_.size() || !resources_[idx].isTexture) return kale_device::TextureHandle{};
    return compiledTextures_[idx];
}

inline kale_device::BufferHandle RenderGraph::GetCompiledBuffer(RGResourceHandle handle) const {
    if (handle == kInvalidRGResourceHandle || handle == 0) return kale_device::BufferHandle{};
    size_t idx = static_cast<size_t>(handle - 1);
    if (idx >= resources_.size() || resources_[idx].isTexture) return kale_device::BufferHandle{};
    return compiledBuffers_[idx];
}

inline std::vector<RenderPassHandle> RenderGraph::BuildTopologicalOrder() const {
    const size_t n = passes_.size();
    if (n == 0) return {};

    // 收集每个 Pass 写入的资源（含 color/depth），以及读取的资源
    auto writersOf = [this](RGResourceHandle h) -> std::vector<RenderPassHandle> {
        std::vector<RenderPassHandle> out;
        for (size_t i = 0; i < compiledPassInfo_.size(); ++i) {
            const auto& info = compiledPassInfo_[i];
            for (const auto& p : info.colorOutputs) if (p.second == h) { out.push_back(static_cast<RenderPassHandle>(i)); break; }
            if (info.depthOutput == h) { out.push_back(static_cast<RenderPassHandle>(i)); break; }
        }
        return out;
    };
    auto readersOf = [this](RGResourceHandle h) -> std::vector<RenderPassHandle> {
        std::vector<RenderPassHandle> out;
        for (size_t i = 0; i < compiledPassInfo_.size(); ++i) {
            const auto& info = compiledPassInfo_[i];
            for (RGResourceHandle r : info.readTextures) if (r == h) { out.push_back(static_cast<RenderPassHandle>(i)); break; }
        }
        return out;
    };

    // 建图：边 writer -> reader 表示 writer 必须在 reader 前；同一纹理多写者按 AddPass 顺序显式排序
    std::set<std::pair<RenderPassHandle, RenderPassHandle>> edges;
    for (size_t i = 0; i < resources_.size(); ++i) {
        RGResourceHandle h = static_cast<RGResourceHandle>(i + 1);
        std::vector<RenderPassHandle> writers = writersOf(h);
        std::vector<RenderPassHandle> readers = readersOf(h);
        // 写者先于读者
        for (RenderPassHandle w : writers)
            for (RenderPassHandle r : readers)
                if (w != r) edges.insert({w, r});
        // 同一纹理多写者：按 Pass 下标（AddPass 顺序）显式顺序，前者先于后者
        if (writers.size() > 1) {
            std::sort(writers.begin(), writers.end());
            for (size_t wi = 0; wi + 1 < writers.size(); ++wi)
                edges.insert({writers[wi], writers[wi + 1]});
        }
    }

    std::vector<std::vector<RenderPassHandle>> outEdges(n);
    std::vector<int> inDegree(n, 0);
    for (const auto& e : edges) {
        outEdges[e.first].push_back(e.second);
        inDegree[e.second]++;
    }

    std::queue<RenderPassHandle> q;
    for (size_t i = 0; i < n; ++i)
        if (inDegree[i] == 0) q.push(static_cast<RenderPassHandle>(i));

    std::vector<RenderPassHandle> order;
    order.reserve(n);
    while (!q.empty()) {
        RenderPassHandle u = q.front();
        q.pop();
        order.push_back(u);
        for (RenderPassHandle v : outEdges[u]) {
            if (--inDegree[v] == 0) q.push(v);
        }
    }

    if (order.size() != n) return {};  // 存在环
    return order;
}

inline std::vector<std::vector<RenderPassHandle>> RenderGraph::GetTopologicalGroups() const {
    if (!IsCompiled() || topologicalOrder_.empty()) return {};
    return BuildTopologicalGroups();
}

inline std::vector<std::vector<RenderPassHandle>> RenderGraph::BuildTopologicalGroups() const {
    const size_t n = passes_.size();
    if (n == 0 || compiledPassInfo_.size() != n) return {};

    auto writersOf = [this](RGResourceHandle h) -> std::vector<RenderPassHandle> {
        std::vector<RenderPassHandle> out;
        for (size_t i = 0; i < compiledPassInfo_.size(); ++i) {
            const auto& info = compiledPassInfo_[i];
            for (const auto& p : info.colorOutputs) if (p.second == h) { out.push_back(static_cast<RenderPassHandle>(i)); break; }
            if (info.depthOutput == h) { out.push_back(static_cast<RenderPassHandle>(i)); break; }
        }
        return out;
    };
    auto readersOf = [this](RGResourceHandle h) -> std::vector<RenderPassHandle> {
        std::vector<RenderPassHandle> out;
        for (size_t i = 0; i < compiledPassInfo_.size(); ++i) {
            const auto& info = compiledPassInfo_[i];
            for (RGResourceHandle r : info.readTextures) if (r == h) { out.push_back(static_cast<RenderPassHandle>(i)); break; }
        }
        return out;
    };

    std::set<std::pair<RenderPassHandle, RenderPassHandle>> edges;
    for (size_t i = 0; i < resources_.size(); ++i) {
        RGResourceHandle h = static_cast<RGResourceHandle>(i + 1);
        std::vector<RenderPassHandle> writers = writersOf(h);
        std::vector<RenderPassHandle> readers = readersOf(h);
        for (RenderPassHandle w : writers)
            for (RenderPassHandle r : readers)
                if (w != r) edges.insert({w, r});
        if (writers.size() > 1) {
            std::sort(writers.begin(), writers.end());
            for (size_t wi = 0; wi + 1 < writers.size(); ++wi)
                edges.insert({writers[wi], writers[wi + 1]});
        }
    }

    std::vector<std::vector<RenderPassHandle>> inEdges(n);
    for (const auto& e : edges)
        inEdges[e.second].push_back(e.first);

    std::vector<int> level(static_cast<size_t>(n), 0);
    for (RenderPassHandle passIdx : topologicalOrder_) {
        const auto& preds = inEdges[passIdx];
        if (preds.empty())
            level[passIdx] = 0;
        else {
            int maxPred = -1;
            for (RenderPassHandle p : preds) {
                int lp = level[p];
                if (lp > maxPred) maxPred = lp;
            }
            level[passIdx] = maxPred + 1;
        }
    }

    int maxLevel = -1;
    for (int l : level) if (l > maxLevel) maxLevel = l;
    std::vector<std::vector<RenderPassHandle>> groups(static_cast<size_t>(maxLevel + 1));
    for (size_t i = 0; i < n; ++i)
        groups[static_cast<size_t>(level[i])].push_back(static_cast<RenderPassHandle>(i));

    return groups;
}

inline void RenderGraph::BuildFrameDrawList() {
    // 当前 submittedDraws_ 已由应用层每帧填入，此处预留排序/分组等扩展，无需拷贝。
}

inline void RenderGraph::ReleaseFrameResources() {
    for (const auto& draw : submittedDraws_) {
        if (draw.renderable)
            draw.renderable->ReleaseFrameResources();
    }
}

inline kale_device::CommandList* RenderGraph::RecordOnePass(kale_device::IRenderDevice* device,
                                                            RenderPassHandle passIdx,
                                                            std::uint32_t threadIndex,
                                                            RenderPassContext& ctx) {
    if (passIdx >= passes_.size()) return nullptr;
    const auto& pass = passes_[passIdx];
    const auto& info = compiledPassInfo_[passIdx];

    kale_device::CommandList* cmd = device->BeginCommandList(threadIndex);
    if (!cmd) return nullptr;

    if (info.executeWithoutRenderPass) {
        if (pass.execute) pass.execute(ctx, *cmd);
        device->EndCommandList(cmd);
        return cmd;
    }

    std::vector<kale_device::TextureHandle> colorAttachments;
    kale_device::TextureHandle depthAttachment;
    if (info.writesSwapchain) {
        kale_device::TextureHandle outTex = ctx.GetOutputTarget().IsValid() ? ctx.GetOutputTarget() : device->GetBackBuffer();
        colorAttachments.push_back(outTex);
    } else {
        for (const auto& p : info.colorOutputs) {
            auto th = GetCompiledTexture(p.second);
            if (th.IsValid()) colorAttachments.push_back(th);
        }
        if (info.depthOutput != kInvalidRGResourceHandle)
            depthAttachment = GetCompiledTexture(info.depthOutput);
    }

    const bool hasRenderTarget = !colorAttachments.empty() || depthAttachment.IsValid();
    if (hasRenderTarget) {
        cmd->BeginRenderPass(colorAttachments, depthAttachment);
        std::uint32_t vpW = resolutionWidth_;
        std::uint32_t vpH = resolutionHeight_;
        if (vpW > 0 && vpH > 0) {
            cmd->SetViewport(0.f, 0.f, static_cast<float>(vpW), static_cast<float>(vpH));
            cmd->SetScissor(0, 0, vpW, vpH);
        }
        if (pass.execute) pass.execute(ctx, *cmd);
        cmd->EndRenderPass();
    } else if (pass.execute) {
        pass.execute(ctx, *cmd);
    }

    device->EndCommandList(cmd);
    return cmd;
}

inline std::vector<kale_device::CommandList*> RenderGraph::RecordPasses(kale_device::IRenderDevice* device) {
    std::vector<kale_device::CommandList*> cmdLists;
    if (!device || topologicalOrder_.empty()) return cmdLists;

    RenderPassContext ctx(&submittedDraws_, device, this);

    if (!scheduler_) {
        for (RenderPassHandle passIdx : topologicalOrder_) {
            kale_device::CommandList* cmd = RecordOnePass(device, passIdx, 0, ctx);
            if (cmd) cmdLists.push_back(cmd);
        }
        return cmdLists;
    }

    std::vector<std::vector<RenderPassHandle>> groups = GetTopologicalGroups();
    if (groups.empty()) {
        for (RenderPassHandle passIdx : topologicalOrder_) {
            kale_device::CommandList* cmd = RecordOnePass(device, passIdx, 0, ctx);
            if (cmd) cmdLists.push_back(cmd);
        }
        return cmdLists;
    }

    std::unordered_map<RenderPassHandle, size_t> passToTopoIndex;
    for (size_t i = 0; i < topologicalOrder_.size(); ++i)
        passToTopoIndex[topologicalOrder_[i]] = i;

    std::vector<kale_device::CommandList*> result(topologicalOrder_.size(), nullptr);
    const std::uint32_t maxRec = std::max(1u, device->GetCapabilities().maxRecordingThreads);
    std::vector<std::shared_future<void>> prevLevelFutures;

    for (const auto& group : groups) {
        std::vector<std::shared_future<void>> groupFutures;
        for (size_t chunkStart = 0; chunkStart < group.size(); chunkStart += maxRec) {
            const size_t chunkEnd = std::min(chunkStart + static_cast<size_t>(maxRec), group.size());
            std::vector<std::shared_future<void>> chunkFutures;
            for (size_t i = chunkStart; i < chunkEnd; ++i) {
                const RenderPassHandle passIdx = group[i];
                const size_t topoPos = passToTopoIndex.at(passIdx);
                const std::uint32_t threadIndex = static_cast<std::uint32_t>(i - chunkStart);
                chunkFutures.push_back(scheduler_->SubmitRenderTask(
                    [this, device, passIdx, topoPos, threadIndex, &result]() {
                        RenderPassContext taskCtx(&submittedDraws_, device, this);
                        kale_device::CommandList* cmd = RecordOnePass(device, passIdx, threadIndex, taskCtx);
                        if (cmd) result[topoPos] = cmd;
                    },
                    prevLevelFutures));
            }
            for (auto& f : chunkFutures)
                if (f.valid()) f.wait();
            for (auto& f : chunkFutures) groupFutures.push_back(std::move(f));
        }
        prevLevelFutures = std::move(groupFutures);
    }

    for (kale_device::CommandList* cmd : result)
        if (cmd) cmdLists.push_back(cmd);
    return cmdLists;
}

inline void RenderGraph::Execute(kale_device::IRenderDevice* device) {
    if (!device || !IsCompiled()) return;

    const std::uint32_t frameIndex = currentFrameIndex_ % kMaxFramesInFlight;

    if (frameIndex < frameFences_.size() && frameFences_[frameIndex].IsValid()) {
        while (!device->IsFenceSignaled(frameFences_[frameIndex])) {
            if (quitCallback_ && quitCallback_()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        device->ResetFence(frameFences_[frameIndex]);
    }

    if (device->AcquireNextImage() == kale_device::IRenderDevice::kInvalidSwapchainImageIndex) return;  // 失败则跳过本帧

    BuildFrameDrawList();

    std::vector<kale_device::CommandList*> cmdLists = RecordPasses(device);

    kale_device::FenceHandle submitFence;
    if (frameIndex < frameFences_.size()) submitFence = frameFences_[frameIndex];
    if (!cmdLists.empty())
        device->Submit(cmdLists, {}, {}, submitFence);

    ReleaseFrameResources();

    if (!cmdLists.empty())
        currentFrameIndex_ = (currentFrameIndex_ + 1) % kMaxFramesInFlight;
}

inline kale_device::TextureHandle RenderPassContext::GetCompiledTexture(RGResourceHandle handle) const {
    return graph_ ? graph_->GetCompiledTexture(handle) : kale_device::TextureHandle{};
}

inline kale_device::TextureHandle RenderPassContext::GetOutputTarget() const {
    return graph_ ? graph_->GetOutputTarget() : kale_device::TextureHandle{};
}

inline glm::mat4 RenderPassContext::GetViewMatrix() const {
    return graph_ ? graph_->GetViewMatrix() : glm::mat4(1.f);
}

inline glm::mat4 RenderPassContext::GetProjectionMatrix() const {
    return graph_ ? graph_->GetProjectionMatrix() : glm::mat4(1.f);
}

inline void RenderGraph::Execute(kale_device::IRenderDevice* device, kale_device::TextureHandle outputTarget) {
    kale_device::TextureHandle prev = outputTarget_;
    if (outputTarget.IsValid()) outputTarget_ = outputTarget;
    Execute(device);
    outputTarget_ = prev;
}

}  // namespace kale::pipeline
