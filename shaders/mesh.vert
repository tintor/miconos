#version 150

uniform int tick;
uniform vec3 eye;
uniform mat4 matrix;
uniform float foglimit2;

uniform vec3 mesh_pos;
uniform mat3 mesh_rot;

in vec3 vertex_pos;
in vec2 vertex_uv;
in int texture_with_flag;

out float fog_factor;
out vec2 fragment_uv;
out float fragment_texture;
out float fragment_underwater_texture;

int transform_texture(int block_texture)
{
        if (block_texture == 6 || block_texture == 38) // lava_flow / lava_still
        {
                return block_texture + ((tick / 8) % 32);
        }
        if (block_texture == 70) // water_still
        {
                return block_texture + ((tick / 8) % 64);
        }
        if (block_texture == 134) // pumpkin_face
        {
                return block_texture + ((tick / 40) % 2);
        }
        if (block_texture == 136) // furnace_front_on
        {
                return block_texture + ((tick / 10) % 16);
        }
        if (block_texture == 152) // sea_lantern
        {
                return block_texture + ((tick / 20) % 5);
        }
        if (block_texture == 157 || block_texture == 159) // redstone_lamp
        {
                return block_texture + ((tick / 40) % 2);
        }
        if (block_texture == 161) // water_flow
        {
                return block_texture + ((tick / 8) % 32);
        }
        return block_texture;
}

void main()
{
	const int flag = 1 << 15;
	fragment_underwater_texture = ((texture_with_flag & flag) != 0) ? 70 + ((tick / 8) % 64) : -1.f;
	int texture = texture_with_flag & ~flag;
	fragment_texture = transform_texture(texture);

	vec3 v = vertex_pos * mesh_rot + mesh_pos;

	gl_Position = matrix * vec4(v, 1);
	fragment_uv = vertex_uv;

	// PERF: make it per-mesh, and not per-vertex fog computation
	float eye_dist_sqr = dot(eye - v, eye - v);
	fog_factor = clamp(eye_dist_sqr / foglimit2, 0.0, 1.0);
	fog_factor *= fog_factor;
}
