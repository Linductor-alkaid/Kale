#version 450

layout(binding = 0) uniform sampler2D uInput;

layout(push_constant, std430) uniform PushConstants {
    vec2 rcpFrame;   // 1/width, 1/height
    int quality;     // 0=low, 1=medium, 2=high
} pc;

layout(location = 0) out vec4 outColor;

float rgbToLuma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 uv = gl_FragCoord.xy * pc.rcpFrame;
    vec2 texelSize = pc.rcpFrame;

    vec3 C = texture(uInput, uv).rgb;
    vec3 N = texture(uInput, uv + vec2(0.0, texelSize.y)).rgb;
    vec3 S = texture(uInput, uv - vec2(0.0, texelSize.y)).rgb;
    vec3 E = texture(uInput, uv + vec2(texelSize.x, 0.0)).rgb;
    vec3 W = texture(uInput, uv - vec2(texelSize.x, 0.0)).rgb;

    float lumaC = rgbToLuma(C);
    float lumaN = rgbToLuma(N);
    float lumaS = rgbToLuma(S);
    float lumaE = rgbToLuma(E);
    float lumaW = rgbToLuma(W);

    float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // Edge threshold: quality 0=more blur, 2=sharper
    float edgeThreshold = 0.166;
    float edgeThresholdMin = 0.0833;
    if (pc.quality == 0) {
        edgeThreshold = 0.25;
        edgeThresholdMin = 0.125;
    } else if (pc.quality == 2) {
        edgeThreshold = 0.125;
        edgeThresholdMin = 0.0625;
    }

    if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold)) {
        outColor = vec4(C, 1.0);
        return;
    }

    float lumaNS = lumaN + lumaS;
    float lumaEW = lumaE + lumaW;
    float gradientH = abs(-2.0 * lumaC + lumaEW);
    float gradientV = abs(-2.0 * lumaC + lumaNS);

    bool isHorizontal = gradientH >= gradientV;

    float luma1 = isHorizontal ? lumaW : lumaS;
    float luma2 = isHorizontal ? lumaE : lumaN;
    float gradient1 = abs(luma1 - lumaC);
    float gradient2 = abs(luma2 - lumaC);

    float stepLength = isHorizontal ? texelSize.x : texelSize.y;
    vec2 offset1 = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
    vec2 offset2 = isHorizontal ? vec2(-texelSize.x, 0.0) : vec2(0.0, -texelSize.y);

    if (gradient1 < gradient2) {
        stepLength = -stepLength;
        offset1 = offset2;
    }

    vec2 uv1 = uv + offset1 * 0.5;
    vec2 uv2 = uv + offset1;
    vec2 uv3 = uv + offset1 * 1.5;

    float lumaEnd1 = rgbToLuma(texture(uInput, uv2).rgb);
    float lumaEnd2 = rgbToLuma(texture(uInput, uv3).rgb);
    lumaEnd1 = (lumaEnd1 + luma1) * 0.5;
    lumaEnd2 = (lumaEnd2 + lumaEnd1) * 0.5;

    float gradientScaled = 0.25 * max(gradient1, gradient2);
    if (abs(lumaEnd1 - lumaC) >= gradientScaled)
        uv1 = uv2;
    if (abs(lumaEnd2 - lumaC) >= gradientScaled)
        uv1 = uv3;

    vec2 uvOffset = uv1 - uv;
    uvOffset = clamp(uvOffset, -texelSize * 2.0, texelSize * 2.0);

    vec3 colorA = 0.5 * (texture(uInput, uv + uvOffset).rgb + texture(uInput, uv - uvOffset).rgb);
    vec3 colorB = colorA * 0.5 + 0.25 * (texture(uInput, uv + uvOffset * 0.5).rgb + texture(uInput, uv - uvOffset * 0.5).rgb);

    float lumaB = rgbToLuma(colorB);
    float lumaA = rgbToLuma(colorA);
    if (lumaB < lumaMin || lumaB > lumaMax)
        outColor = vec4(colorA, 1.0);
    else
        outColor = vec4(colorB, 1.0);
}
