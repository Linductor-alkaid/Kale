#version 450

layout(binding = 0) uniform sampler2D uInput;

layout(push_constant, std430) uniform PushConstants {
    int horizontal;  // 1 = H, 0 = V
} pc;

layout(location = 0) out vec4 outColor;

// 5-tap Gaussian weights (approx)
const float weight[3] = float[](0.227027, 0.316216, 0.070270);

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(uInput, 0));
    vec2 uv = gl_FragCoord.xy * texelSize;
    vec2 off = (pc.horizontal != 0) ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    vec4 c = texture(uInput, uv) * weight[0];
    c += texture(uInput, uv - off) * weight[1];
    c += texture(uInput, uv + off) * weight[1];
    c += texture(uInput, uv - 2.0 * off) * weight[2];
    c += texture(uInput, uv + 2.0 * off) * weight[2];
    outColor = c;
}
