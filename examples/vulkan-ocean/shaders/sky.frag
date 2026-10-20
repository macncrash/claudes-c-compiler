#version 450

layout(push_constant) uniform PC {
    mat4 view_proj;
    vec4 cam_pos_time;
    vec4 sun_dir_aspect;
    vec4 cam_fwd_tan;
    vec4 cam_up;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 sky(vec3 d, vec3 sun) {
    float h = max(d.y, 0.0);
    vec3 col = mix(vec3(0.62, 0.74, 0.86), vec3(0.10, 0.25, 0.55), pow(h, 0.5));
    float s = max(dot(d, sun), 0.0);
    col += vec3(1.0, 0.82, 0.55) * (pow(s, 900.0) * 12.0 + pow(s, 10.0) * 0.30);
    return col;
}

void main() {
    // Rebuild the view ray with the same basis the C code's look_at uses.
    vec3 f = normalize(pc.cam_fwd_tan.xyz);
    vec3 s = normalize(cross(f, pc.cam_up.xyz));
    vec3 u = cross(s, f);
    float t = pc.cam_fwd_tan.w;
    // Vulkan NDC has y pointing down.
    vec3 d = normalize(f + v_uv.x * pc.sun_dir_aspect.w * t * s - v_uv.y * t * u);
    out_color = vec4(sky(d, normalize(pc.sun_dir_aspect.xyz)), 1.0);
}
