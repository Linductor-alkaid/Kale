/**
 * @file material.hpp
 * @brief 渲染管线层材质基类：纹理/参数绑定、Shader、Pipeline
 *
 * 与 rendering_pipeline_layer_design.md 5.5、phase7-7.7 对齐。
 * 材质级 DescriptorSet：phase7-7.8；实例级池化：phase7-7.9。
 */

#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::pipeline {

/** 实例数据 UBO 最大字节数（满足 Vulkan minUniformBufferOffsetAlignment 常见值 256） */
constexpr std::size_t kInstanceDescriptorDataSize = 256;

/**
 * 材质基类（继承 resource::Material 以支持 ReleaseFrameResources 多态）。
 * - SetTexture / SetParameter：按名称绑定纹理与原始参数。
 * - GetShader() / GetPipeline()：供 Draw 时 BindPipeline / BindDescriptorSet 使用。
 * - parameters_ 存储按名称的原始字节参数（如 float、mat4 等）。
 */
class Material : public kale::resource::Material {
public:
    Material() = default;
    ~Material() override = default;

    /** 按名称设置纹理（非占有，由外部管理生命周期） */
    void SetTexture(const std::string& name, kale::resource::Texture* texture) {
        if (!name.empty())
            textures_[name] = texture;
    }

    /** 按名称设置原始参数（拷贝 data 的 size 字节） */
    void SetParameter(const std::string& name, const void* data, std::size_t size) {
        if (name.empty() || !data || size == 0)
            return;
        std::vector<std::byte>& blob = parameters_[name];
        blob.resize(size);
        std::memcpy(blob.data(), data, size);
    }

    /** 获取绑定的着色器（可为空） */
    kale::resource::Shader* GetShader() const { return shader_; }

    /** 设置着色器（非占有） */
    void SetShader(kale::resource::Shader* shader) { shader_ = shader; }

    /** 获取管线句柄 */
    kale_device::PipelineHandle GetPipeline() const { return pipeline_; }

    /** 设置管线句柄 */
    void SetPipeline(kale_device::PipelineHandle handle) { pipeline_ = handle; }

    /** 按名称获取纹理，不存在返回 nullptr */
    kale::resource::Texture* GetTexture(const std::string& name) const {
        auto it = textures_.find(name);
        return it != textures_.end() ? it->second : nullptr;
    }

    /** 按名称获取参数字节块，不存在返回 nullptr，size 通过 outSize 返回 */
    const void* GetParameter(const std::string& name, std::size_t* outSize) const {
        auto it = parameters_.find(name);
        if (it == parameters_.end()) {
            if (outSize)
                *outSize = 0;
            return nullptr;
        }
        if (outSize)
            *outSize = it->second.size();
        return it->second.empty() ? nullptr : it->second.data();
    }

    /** 材质级 DescriptorSet：同一材质所有实例共享，包含纹理/采样器等不变资源。返回无效句柄表示尚未构建或无纹理。 */
    kale_device::DescriptorSetHandle GetMaterialDescriptorSet() const { return materialDescriptorSet_; }

    /** 根据当前 textures_ 分配并绑定材质级 DescriptorSet；无 device 或无纹理则跳过。若已有 set 会先销毁再重建。 */
    void EnsureMaterialDescriptorSet(kale_device::IRenderDevice* device) {
        if (!device || textures_.empty()) return;
        if (materialDescriptorSet_.IsValid()) {
            device->DestroyDescriptorSet(materialDescriptorSet_);
            materialDescriptorSet_ = kale_device::DescriptorSetHandle{};
        }
        kale_device::DescriptorSetLayoutDesc layout;
        layout.bindings.reserve(textures_.size());
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(textures_.size()); ++i) {
            layout.bindings.push_back({
                i,
                kale_device::DescriptorType::CombinedImageSampler,
                kale_device::ShaderStage::Fragment,
                1
            });
        }
        materialDescriptorSet_ = device->CreateDescriptorSet(layout);
        if (!materialDescriptorSet_.IsValid()) return;
        std::uint32_t binding = 0;
        for (const auto& [name, tex] : textures_) {
            (void)name;
            if (tex && tex->handle.IsValid())
                device->WriteDescriptorSetTexture(materialDescriptorSet_, binding, tex->handle);
            ++binding;
        }
    }

    /**
     * 从池中取得一个实例级 DescriptorSet，写入 instanceData（如 worldTransform），返回 set 句柄。
     * 用于 per-instance UBO；Draw 时调用，帧末由 ReleaseAllInstanceDescriptorSets 回收。
     * @param device 用于创建 set/buffer（池为空时）
     * @param instanceData 实例数据指针，可为 nullptr（仅分配 set，不写入）
     * @param size 实例数据字节数，不超过 kInstanceDescriptorDataSize
     * @return 有效句柄供 BindDescriptorSet 使用；无效表示 device 为空或分配失败
     */
    kale_device::DescriptorSetHandle AcquireInstanceDescriptorSet(
        kale_device::IRenderDevice* device,
        const void* instanceData,
        std::size_t size) {
        if (!device) return kale_device::DescriptorSetHandle{};
        if (size > kInstanceDescriptorDataSize) size = kInstanceDescriptorDataSize;
        InstanceSetEntry entry;
        if (!instancePool_.empty()) {
            entry = instancePool_.back();
            instancePool_.pop_back();
        } else {
            kale_device::DescriptorSetLayoutDesc layout;
            layout.bindings.push_back({
                0u,
                kale_device::DescriptorType::UniformBuffer,
                kale_device::ShaderStage::Vertex,
                1u
            });
            entry.set = device->CreateDescriptorSet(layout);
            if (!entry.set.IsValid()) return kale_device::DescriptorSetHandle{};
            kale_device::BufferDesc bufDesc;
            bufDesc.size = kInstanceDescriptorDataSize;
            bufDesc.usage = kale_device::BufferUsage::Uniform;
            bufDesc.cpuVisible = true;
            entry.buffer = device->CreateBuffer(bufDesc, nullptr);
            if (!entry.buffer.IsValid()) {
                device->DestroyDescriptorSet(entry.set);
                return kale_device::DescriptorSetHandle{};
            }
            device->WriteDescriptorSetBuffer(entry.set, 0, entry.buffer, 0, kInstanceDescriptorDataSize);
        }
        if (instanceData && size > 0)
            device->UpdateBuffer(entry.buffer, instanceData, size, 0);
        instanceInUse_.push_back(entry);
        return entry.set;
    }

    /** 将本帧通过 AcquireInstanceDescriptorSet 分配的所有 set 回收到池，供下一帧复用。 */
    void ReleaseAllInstanceDescriptorSets() {
        for (auto& e : instanceInUse_)
            instancePool_.push_back(e);
        instanceInUse_.clear();
    }

    /** 帧末由 RenderGraph::ReleaseFrameResources 通过 Renderable 调用。 */
    void ReleaseFrameResources() override { ReleaseAllInstanceDescriptorSets(); }

protected:
    kale::resource::Shader* shader_ = nullptr;
    kale_device::PipelineHandle pipeline_{};
    kale_device::DescriptorSetHandle materialDescriptorSet_{};  // 材质级共享 set，EnsureMaterialDescriptorSet 构建
    std::unordered_map<std::string, kale::resource::Texture*> textures_;
    std::unordered_map<std::string, std::vector<std::byte>> parameters_;

    /** 实例级 DescriptorSet 池：每个条目为 (DescriptorSet, UniformBuffer)。 */
    struct InstanceSetEntry {
        kale_device::DescriptorSetHandle set{};
        kale_device::BufferHandle buffer{};
    };
    std::vector<InstanceSetEntry> instancePool_;
    std::vector<InstanceSetEntry> instanceInUse_;
};

}  // namespace kale::pipeline
