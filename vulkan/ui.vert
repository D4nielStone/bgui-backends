#version 450
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
layout(location = 0) out vec2 uv;
void main() {
    const vec2 positions[6] = vec2[6](
        vec2(0, 1), vec2(1, 0), vec2(0, 0),
        vec2(0, 1), vec2(1, 1), vec2(1, 0));
    vec2 p = positions[gl_VertexIndex];
    uv = p;
    gl_Position = pc.projection * vec4(pc.rect.xy + p * pc.rect.zw, 0, 1);
}
