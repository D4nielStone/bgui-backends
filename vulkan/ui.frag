#version 450
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Push {
    mat4 projection;
    vec4 rect;
    vec2 uv_min;
    vec2 uv_max;
    vec4 bg_color;
    vec4 border_color;
    vec4 text_color;
    float border_radius;
    float border_size;
    int bordered;
    int use_tex;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    if (pc.use_tex != 0) {
        vec2 sample_uv = mix(pc.uv_min, pc.uv_max, uv);
        float alpha = texture(tex, sample_uv).r;
        color = vec4(pc.text_color.rgb, alpha * pc.text_color.a);
        return;
    }
    if (pc.bordered != 0) {
        vec2 p = uv * pc.rect.zw - pc.rect.zw * 0.5;
        vec2 q = abs(p) - (pc.rect.zw * 0.5 - vec2(pc.border_radius));
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - pc.border_radius;
        if (distance > 0.0) discard;
        color = distance > -pc.border_size ? pc.border_color : pc.bg_color;
    } else {
        color = pc.bg_color;
    }
}
