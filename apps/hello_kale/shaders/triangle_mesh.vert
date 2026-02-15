#version 450
// 与 render_demo 一致：使用 push constant MVP，占位符三角形顶点在 NDC 范围内
layout(push_constant) uniform PushConstants {
    mat4 modelViewProj;
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = pc.modelViewProj * vec4(inPosition, 1.0);
}
