#version 450

layout(push_constant) uniform PC {
    mat4 view_proj;
    vec4 cam_pos_time;
    vec4 sun_dir_aspect;
    vec4 cam_fwd_tan;
    vec4 cam_up;
} pc;

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;

layout(location = 0) out vec4 out_color;

vec3 sky(vec3 d, vec3 sun) {
    float h = max(d.y, 0.0);
    vec3 col = mix(vec3(0.62, 0.74, 0.86), vec3(0.10, 0.25, 0.55), pow(h, 0.5));
    float s = max(dot(d, sun), 0.0);
    col += vec3(1.0, 0.82, 0.55) * (pow(s, 900.0) * 12.0 + pow(s, 10.0) * 0.30);
    return col;
}

void main() {
    vec3 sun = normalize(pc.sun_dir_aspect.xyz);
    vec3 eye = pc.cam_pos_time.xyz;
    vec3 n = normalize(v_normal);
    vec3 v = normalize(eye - v_pos);

    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 r = reflect(-v, n);
    r.y = abs(r.y);
    vec3 reflected = sky(r, sun);

    float crest = clamp(v_pos.y * 0.25 + 0.5, 0.0, 1.0);
    vec3 body = mix(vec3(0.00, 0.05, 0.10), vec3(0.02, 0.30, 0.34), crest);
    body *= 0.4 + 0.6 * max(dot(n, sun), 0.0);
    vec3 col = mix(body, reflected, fresnel);

    // Foam on the steepest crests.
    float foam = smoothstep(0.55, 0.85, crest) * smoothstep(0.85, 0.6, n.y);
    col = mix(col, vec3(0.9), foam * 0.6);

    float dist = length(eye - v_pos);
    vec3 haze = sky(normalize(vec3(-v.x, 0.02, -v.z)), sun);
    col = mix(col, haze, 1.0 - exp(-dist * 0.006));
    out_color = vec4(col, 1.0);
}
