#version 450

void main() {
    // 顶点顺序必须为逆时针（CCW）以通过 VK_FRONT_FACE_COUNTER_CLOCKWISE 的背面剔除
    vec2 positions[3] = vec2[](
        vec2( 0.0, -0.5),   // 顶部
        vec2(-0.5,  0.5),   // 左下
        vec2( 0.5,  0.5)    // 右下
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
