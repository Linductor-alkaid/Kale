#version 450

layout(binding = 0) uniform sampler2D uLighting;
layout(binding = 1) uniform sampler2D uBloom;

layout(push_constant, std430) uniform PushConstants {
    float exposure;
    float bloomStrength;
} pc;

layout(location = 0) out vec4 outColor;

// Reinhard: LDR = HDR / (HDR + 1)
void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uLighting, 0));
    vec4 hdr = texture(uLighting, uv);
    vec4 bloom = texture(uBloom, uv);
    vec3 combined = hdr.rgb + pc.bloomStrength * bloom.rgb;
    vec3 scaled = combined * pc.exposure;
    vec3 ldr = scaled / (scaled + 1.0);
    outColor = vec4(ldr, hdr.a);
}
