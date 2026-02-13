#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));
    float NdotL = max(dot(N, L), 0.0);
    vec3 ambient = vec3(0.2, 0.25, 0.35);
    vec3 diffuse = vec3(0.6, 0.7, 0.9) * NdotL;
    vec3 color = ambient + diffuse;
    outColor = vec4(color, 1.0);
}
