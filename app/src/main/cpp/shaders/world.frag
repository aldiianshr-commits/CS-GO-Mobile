#version 450
// params.x = alpha ref (0 = tanpa alpha test)
// params.y = 1 jika campur 2 tekstur (WorldVertexTransition)
// params.z = mode lightmap: 0 mati (fullbright), 1 rendah, 2 sedang, 3 tinggi (tonemap)
// params.w = skala kecerahan
layout(push_constant) uniform PC { mat4 mvp; vec4 tint; vec4 params; } pc;
layout(set = 0, binding = 0) uniform sampler2D tBase;
layout(set = 0, binding = 1) uniform sampler2D tBase2;
layout(set = 0, binding = 2) uniform sampler2D tLight;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec2 vLM;
layout(location = 2) in float vAlpha;
layout(location = 0) out vec4 outColor;

vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec4 c = texture(tBase, vUV);
    if (pc.params.y > 0.5) {
        vec4 c2 = texture(tBase2, vUV);
        c = mix(c, c2, clamp(vAlpha, 0.0, 1.0));
    }
    if (pc.params.x > 0.0 && c.a < pc.params.x) discard;
    vec3 albedo = pow(c.rgb, vec3(2.2)) * pc.tint.rgb;
    vec3 col;
    if (pc.params.z < 0.5) {
        col = albedo;
    } else {
        vec3 light = texture(tLight, vLM).rgb * pc.params.w;
        col = albedo * light;
        if (pc.params.z > 2.5) col = acesFilm(col * 1.6);
        else col = min(col, vec3(1.0));
    }
    outColor = vec4(pow(col, vec3(1.0 / 2.2)), c.a * pc.tint.a);
}
