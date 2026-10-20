#version 450

layout(push_constant) uniform PC {
    mat4 view_proj;
    vec4 cam_pos_time;
    vec4 sun_dir_aspect;
    vec4 cam_fwd_tan;
    vec4 cam_up;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec3 v_normal;

void main() {
    v_pos = in_pos;
    v_normal = in_normal;
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
