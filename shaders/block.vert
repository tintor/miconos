#version 150

uniform int tick;
uniform vec3 eye;
uniform mat4 matrix;
uniform ivec3 pos;

in ivec3 position;
in uint color;
in uint light;
in uint uv;

//out float material;
out float fog_factor;
out vec3 fragment_color;
out vec2 fragment_uv;

const float FogLimit = 0.80 * 63 * 16;
vec2[4] uv_table = vec2[](vec2(0,0), vec2(0,1), vec2(1,0), vec2(1,1));

const float pi = 3.14159265f;

void main() {
    vec3 p = pos + position;

    //material = color;
    if (color == 255u)
    {
        fragment_color = vec3(0, 3, 0) * light / (3.0f * 255.0f);
		float ftick = tick * 0.2;
                float speed = 0.75;
                float magnitude = (sin((ftick * pi / ((28.0) * speed))) * 0.05 + 0.15)*0.1;
                float d0 = sin(ftick * pi / (122.0 * speed)) * 3.0 - 1.5;
                float d1 = sin(ftick * pi / (142.0 * speed)) * 3.0 - 1.5;
                float d2 = sin(ftick * pi / (162.0 * speed)) * 3.0 - 1.5;
                float d3 = sin(ftick * pi / (112.0 * speed)) * 3.0 - 1.5;                                                                                                                      
                p.x += sin((ftick * pi / (13.0 * speed)) + (p.x + d0)*0.9 + (p.z + d1)*0.9) * magnitude;
                p.z += sin((ftick * pi / (16.0 * speed)) + (p.z + d2)*0.9 + (p.x + d3)*0.9) * magnitude;
                p.y += sin((ftick * pi / (15.0 * speed)) + (p.z + d2) + (p.x + d3)) * (magnitude/1.0);
    }
    else
    {
        fragment_color = vec3(color & 0x3u, (color >> 2) & 0x3u, (color >> 4) & 0x3u) * light / (3.0f * 255.0f);
    }

    gl_Position = matrix * vec4(p, 1);

    float eye_dist_sqr = dot(eye - p, eye - p);
    fog_factor = clamp(eye_dist_sqr / FogLimit / FogLimit, 0.0, 1.0);
    fog_factor *= fog_factor;

    fragment_uv = uv_table[uv];
}
