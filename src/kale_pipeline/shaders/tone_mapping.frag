#version 450

layout(binding = 0) uniform sampler2D uLighting;

layout(push_constant, std430) uniform PushConstants {
    float exposure;
} pc;

layout(location = 0) out vec4 outColor;

// Reinhard: LDR = HDR / (HDR + 1)，可选曝光
void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uLighting, 0));
    vec4 hdr = texture(uLighting, uv);
    vec3 scaled = hdr.rgb * pc.exposure;
    vec3 ldr = scaled / (scaled + 1.0);
    outColor = vec4(ldr, hdr.a);
}
