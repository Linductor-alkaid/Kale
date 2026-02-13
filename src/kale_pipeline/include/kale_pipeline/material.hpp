/**
 * @file material.hpp
 * @brief 渲染管线层材质基类：纹理/参数绑定、Shader、Pipeline
 *
 * 与 rendering_pipeline_layer_design.md 5.5、phase7-7.7 对齐。
 * 材质级 DescriptorSet、实例级池化由 phase7-7.8 / phase7-7.9 实现。
 */

#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::pipeline {

/**
 * 材质基类。
 * - SetTexture / SetParameter：按名称绑定纹理与原始参数。
 * - GetShader() / GetPipeline()：供 Draw 时 BindPipeline / BindDescriptorSet 使用。
 * - parameters_ 存储按名称的原始字节参数（如 float、mat4 等）。
 */
class Material {
public:
    Material() = default;
    virtual ~Material() = default;

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

protected:
    kale::resource::Shader* shader_ = nullptr;
    kale_device::PipelineHandle pipeline_{};
    std::unordered_map<std::string, kale::resource::Texture*> textures_;
    std::unordered_map<std::string, std::vector<std::byte>> parameters_;
};

}  // namespace kale::pipeline
