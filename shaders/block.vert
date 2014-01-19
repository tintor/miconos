#version 150

uniform vec3 eye;
uniform mat4 matrix;
uniform ivec3 pos;

in ivec3 position;
in uint color;
in uint light;
in uint uv;

out float fog_factor;
out vec3 fragment_color;
out vec2 fragment_uv;

vec2[4] uv_table = vec2[](vec2(0,0), vec2(0,1), vec2(1,0), vec2(1,1));

void main() {
    vec3 p = pos + position;
    gl_Position = matrix * vec4(p, 1);

    float eye_dist_sqr = dot(eye - p, eye - p);
    fog_factor = clamp(eye_dist_sqr / 192.0 / 192.0, 0.0, 1.0);
    fog_factor *= fog_factor;

    fragment_color = vec3(color & 0x3u, (color >> 2) & 0x3u, (color >> 4) & 0x3u) * light / (3.0f * 255.0f);
    fragment_uv = uv_table[uv];
}
