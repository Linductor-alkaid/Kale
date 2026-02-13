// Kale 渲染管线层 - 占位源文件

#include <kale_pipeline/material.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_scene/renderable.hpp>
#include <glm/glm.hpp>

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

    // phase6-6.9: RenderGraph 声明式接口占位引用
    RenderGraph rg;
    rg.SetResolution(1920, 1080);
    kale_device::TextureDesc texDesc = {};
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    RGResourceHandle th = rg.DeclareTexture("TestTex", texDesc);
    (void)th;
    kale_device::BufferDesc bufDesc = {};
    bufDesc.size = 256;
    RGResourceHandle bh = rg.DeclareBuffer("TestBuf", bufDesc);
    (void)bh;
    RenderPassHandle ph = rg.AddPass("DummyPass",
        [](RenderPassBuilder& b) { b.WriteSwapchain(); },
        [](const RenderPassContext&, kale_device::CommandList&) {});
    (void)ph;
    (void)rg.GetDeclaredResources();
    (void)rg.GetPasses();
    (void)rg.GetResolutionWidth();
    (void)rg.GetResolutionHeight();

    // phase6-6.10: 应用层显式提交占位引用
    rg.ClearSubmitted();
    rg.SubmitRenderable(nullptr, glm::mat4(1.0f), kale::scene::PassFlags::All);
    rg.SubmitRenderable(static_cast<kale::scene::Renderable*>(nullptr), glm::mat4(1.0f));
    (void)rg.GetSubmittedDraws();

    // phase7-7.7: Material 基类占位引用
    Material mat;
    mat.SetPipeline(kale_device::PipelineHandle{});
    (void)mat.GetShader();
    (void)mat.GetPipeline();
    mat.SetParameter("foo", "bar", 3);
    (void)mat.GetParameter("foo", nullptr);
}

}  // namespace kale::pipeline
