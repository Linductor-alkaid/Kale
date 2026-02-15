#version 450

layout(binding = 0) uniform sampler2D uLighting;

layout(push_constant, std430) uniform PushConstants {
    float threshold;
} pc;

layout(location = 0) out vec4 outColor;

float luminance(vec3 rgb) {
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uLighting, 0));
    vec4 hdr = texture(uLighting, uv);
    float L = luminance(hdr.rgb);
    if (L > pc.threshold)
        outColor = vec4(hdr.rgb, 1.0);
    else
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
}
