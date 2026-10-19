#version 450
// Shader vertex dunia (posisi, UV tekstur, UV lightmap, alpha blend displacement)
layout(push_constant) uniform PC { mat4 mvp; vec4 tint; vec4 params; } pc;
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec2 inLM;
layout(location = 3) in float inAlpha;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec2 vLM;
layout(location = 2) out float vAlpha;
void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vUV = inUV;
    vLM = inLM;
    vAlpha = inAlpha;
}
