#version 450
// 从顶点缓冲读取位置，用于占位符三角形（PlaceholderVertex: position.xyz）
layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = vec4(inPosition, 1.0);
}
