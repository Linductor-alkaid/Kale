// Kale 渲染管线层 - 占位源文件

#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/rg_types.hpp>

namespace kale::pipeline {

void placeholder() {
    (void)sizeof(SubmittedDraw);
    RenderPassContext ctx(nullptr);
    (void)ctx.GetSubmittedDraws();
    (void)sizeof(RGResourceHandle);
    (void)kInvalidRGResourceHandle;

    // phase6-6.8: RenderPassBuilder 占位引用
    RenderPassBuilder builder;
    builder.WriteColor(0, 1);
    builder.WriteDepth(2);
    builder.ReadTexture(1);
    builder.WriteSwapchain();
    (void)builder.GetColorOutputs();
    (void)builder.GetDepthOutput();
    (void)builder.GetReadTextures();
    (void)builder.WritesSwapchain();
}

}  // namespace kale::pipeline
