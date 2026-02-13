/**
 * @file render_pass_context.hpp
 * @brief 绘制提交与渲染 Pass 上下文：SubmittedDraw、RenderPassContext
 *
 * 与 rendering_pipeline_layer_design.md 5.1 对齐。
 * phase6-6.6：SubmittedDraw 与 RenderPassContext。
 */

#pragma once

#include <kale_device/render_device.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_scene/scene_types.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace kale::pipeline {

/**
 * 单次提交的绘制项，由应用层 SubmitRenderable 填入。
 */
struct SubmittedDraw {
    kale::scene::Renderable* renderable = nullptr;
    glm::mat4 worldTransform{1.0f};
    kale::scene::PassFlags passFlags = kale::scene::PassFlags::All;
};

/**
 * 渲染 Pass 执行时的上下文，提供本帧已提交的绘制列表及按 Pass 过滤。
 */
class RenderPassContext {
public:
    /** @param draws 本帧已提交的绘制项；@param device 当前渲染设备，供 Draw 时绑定实例级 DescriptorSet，可为 nullptr */
    explicit RenderPassContext(const std::vector<SubmittedDraw>* draws,
                              kale_device::IRenderDevice* device = nullptr)
        : draws_(draws), device_(device) {}

    /** 返回本帧所有已提交的绘制项（只读）。 */
    const std::vector<SubmittedDraw>& GetSubmittedDraws() const {
        return draws_ ? *draws_ : empty_;
    }

    /**
     * 按 Pass 过滤绘制项，过滤条件 (draw.passFlags & pass) != 0。
     * @return 符合条件的绘制项副本
     */
    std::vector<SubmittedDraw> GetDrawsForPass(kale::scene::PassFlags pass) const {
        std::vector<SubmittedDraw> result;
        if (!draws_) return result;
        for (const auto& draw : *draws_) {
            if ((draw.passFlags & pass) != kale::scene::PassFlags{0})
                result.push_back(draw);
        }
        return result;
    }

    /** 返回当前渲染设备，供 Renderable::Draw 绑定实例级 DescriptorSet；可为 nullptr。 */
    kale_device::IRenderDevice* GetDevice() const { return device_; }

private:
    const std::vector<SubmittedDraw>* draws_ = nullptr;
    kale_device::IRenderDevice* device_ = nullptr;
    static inline const std::vector<SubmittedDraw> empty_{};
};

}  // namespace kale::pipeline
