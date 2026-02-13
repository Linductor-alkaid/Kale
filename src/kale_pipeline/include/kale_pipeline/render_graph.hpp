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

#include <functional>
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

private:
    std::uint32_t resolutionWidth_ = 0;
    std::uint32_t resolutionHeight_ = 0;
    RGResourceHandle nextHandle_ = 1;
    std::vector<DeclaredResource> resources_;
    std::unordered_map<std::string, RGResourceHandle> nameToHandle_;
    std::vector<PassEntry> passes_;
};

}  // namespace kale::pipeline
