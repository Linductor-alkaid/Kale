#version 450

// 全屏三角形：无顶点缓冲，gl_VertexIndex 0,1,2
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
