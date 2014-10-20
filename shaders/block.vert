#version 150

uniform int tick;
uniform vec3 eye;
uniform mat4 matrix;
uniform ivec3 pos;

in ivec3 position;
in int block_texture;
in uint light;
in ivec2 uv;

out float fog_factor;
out float fragment_light;
out vec2 fragment_uv;
out float fragment_texture;

uniform float foglimit2;

const float pi = 3.14159265f;

void main()
{
    vec3 p = pos + position;

    fragment_texture = block_texture;
    if (block_texture <= 5) // leaves_***
    {
		float ftick = tick * 0.4;
                float speed = 0.75;
                float magnitude = (sin((ftick * pi / ((28.0) * speed))) * 0.05 + 0.15)*0.2;
                float d0 = sin(ftick * pi / (122.0 * speed)) * 3.0 - 1.5;
                float d1 = sin(ftick * pi / (142.0 * speed)) * 3.0 - 1.5;
                float d2 = sin(ftick * pi / (162.0 * speed)) * 3.0 - 1.5;
                float d3 = sin(ftick * pi / (112.0 * speed)) * 3.0 - 1.5;                                                                                                                      
                p.x += sin((ftick * pi / (13.0 * speed)) + (p.x + d0)*0.9 + (p.z + d1)*0.9) * magnitude;
                p.z += sin((ftick * pi / (16.0 * speed)) + (p.z + d2)*0.9 + (p.x + d3)*0.9) * magnitude;
        p.y += sin((ftick * pi / (15.0 * speed)) + (p.z + d2) + (p.x + d3)) * (magnitude/1.0);
    }
    if (block_texture == 6 || block_texture == 38 || block_texture == 70) // lava_flow / lava_still / water_still
    {
        fragment_texture = block_texture + ((tick / 10) % 32);
    }
    if (block_texture == 102) // pumpkin_face
    {
        fragment_texture = block_texture + ((tick / 40) % 2);
    }
    if (block_texture == 104) // furnace_front_on
    {
        fragment_texture = block_texture + ((tick / 10) % 16);
    }
    if (block_texture == 120) // sea_lantern
    {
        fragment_texture = block_texture + ((tick / 40) % 5);
    }
    fragment_light = light / 255.0f;

    gl_Position = matrix * vec4(p, 1);

    float eye_dist_sqr = dot(eye - p, eye - p);
    fog_factor = clamp(eye_dist_sqr / foglimit2, 0.0, 1.0);
    fog_factor *= fog_factor;

    fragment_uv = uv;
}
