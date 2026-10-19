#version 450
// Pass akhir: salin hasil render ke layar, opsional FXAA (anti-aliasing berbasis luma)
layout(push_constant) uniform PC { vec4 p0; } pc; // xy = 1/ukuran sumber, z = FXAA aktif
layout(set = 0, binding = 0) uniform sampler2D tScene;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 fxaa(vec2 uv, vec2 rcp) {
    const float REDUCE_MIN = 1.0 / 128.0;
    const float REDUCE_MUL = 1.0 / 8.0;
    const float SPAN_MAX = 8.0;
    vec3 rgbNW = texture(tScene, uv + vec2(-1.0, -1.0) * rcp).rgb;
    vec3 rgbNE = texture(tScene, uv + vec2(1.0, -1.0) * rcp).rgb;
    vec3 rgbSW = texture(tScene, uv + vec2(-1.0, 1.0) * rcp).rgb;
    vec3 rgbSE = texture(tScene, uv + vec2(1.0, 1.0) * rcp).rgb;
    vec3 rgbM = texture(tScene, uv).rgb;
    float lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE), lM = luma(rgbM);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 * REDUCE_MUL), REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * rcp;
    vec3 a = 0.5 * (texture(tScene, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(tScene, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(tScene, uv + dir * -0.5).rgb + texture(tScene, uv + dir * 0.5).rgb);
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}

void main() {
    if (pc.p0.z > 0.5) outColor = vec4(fxaa(vUV, pc.p0.xy), 1.0);
    else outColor = vec4(texture(tScene, vUV).rgb, 1.0);
}
