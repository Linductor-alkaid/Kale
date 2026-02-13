// Kale 渲染管线层 - 占位源文件

#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/rg_types.hpp>

namespace kale::pipeline {

void placeholder() {
    (void)sizeof(SubmittedDraw);
    RenderPassContext ctx(nullptr);
    (void)ctx.GetSubmittedDraws();
    (void)sizeof(RGResourceHandle);
    (void)kInvalidRGResourceHandle;
}

}  // namespace kale::pipeline
