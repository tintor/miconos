#version 150

uniform int tick;
uniform vec3 eye;
uniform mat4 matrix;
uniform ivec3 cpos;

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

in ivec3 g_vertex[1][4];
in int g_block_texture_with_flag[1];
in int g_light[1];
in int g_plane[1];

out float fog_factor;
out float fragment_light;
out vec2 fragment_uv;
out float fragment_texture;
out float fragment_underwater_texture;

uniform float foglimit2;

const float pi = 3.14159265f;

vec3 leaf_transform(vec3 p)
{
	// TODO All of these should be moved to uniform vars
	float ftick = tick * 0.4;
	float speed = 0.75;
	float magnitude = (sin((ftick * pi / ((28.0) * speed))) * 0.05 + 0.15)*0.2;
	float d0 = sin(ftick * pi / (122.0 * speed)) * 3.0 - 1.5;
	float d1 = sin(ftick * pi / (142.0 * speed)) * 3.0 - 1.5;
	float d2 = sin(ftick * pi / (162.0 * speed)) * 3.0 - 1.5;
	float d3 = sin(ftick * pi / (112.0 * speed)) * 3.0 - 1.5;

	p.x += sin((ftick * pi / (13.0 * speed)) + (p.x + d0)*0.9 + (p.z + d1)*0.9) * magnitude;
	p.z += sin((ftick * pi / (16.0 * speed)) + (p.z + d2)*0.9 + (p.x + d3)*0.9) * magnitude;
	p.y += sin((ftick * pi / (15.0 * speed)) + (p.z + d2) + (p.x + d3)) * magnitude;
	return p;
}

void emit(int _texture, int _light, ivec2 _uv, ivec3 _pos)
{
	vec3 p = cpos + _pos / 15.0f;
	//if (_texture <= 5) p = leaf_transform(p);

	gl_Position = matrix * vec4(p, 1);
	fragment_uv = _uv / 15.0f;
	fragment_light = (_light + 1) / 256.0f;

	float eye_dist_sqr = dot(eye - p, eye - p);
	fog_factor = clamp(eye_dist_sqr / foglimit2, 0.0, 1.0);
	fog_factor *= fog_factor;

	EmitVertex();
}

int get_light(int i)
{
	int s = (i % 4) * 4;
	int a = (g_light[0] >> s) & 15;
	return (a + 1) * 16 - 1;
}

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
	// Compute texture
	const int flag = 1 << 15;
	fragment_underwater_texture = ((g_block_texture_with_flag[0] & flag) != 0) ? 70 + ((tick / 8) % 64) : -1.f;
	int block_texture = g_block_texture_with_flag[0] & ~flag;
	fragment_texture = transform_texture(block_texture);

	// Generate face vertices
	ivec3 vertex[4] = g_vertex[0];
	ivec3 d = vertex[2] - vertex[0];
	int u, v;
	int face = g_plane[0] >> 8;
	if (face < 2) { u = d.y; v = d.z; }
	else if (face < 4) { u = d.x; v = d.z; }
	else { u = d.y; v = d.x; }

	// How much to rotate the quad?
	int a = (face == 0 || face == 3 || face == 5) ? 0 : 1;

	int light[4];
	light[0] = get_light(0+a);
	light[1] = get_light(1+a);
	light[2] = get_light(2+a);
	light[3] = get_light(3+a);

	// Emit quad
	if (light[1] != light[3])
	{
		emit(block_texture, light[3], ivec2(0, v), vertex[(3+a)%4]);
		emit(block_texture, light[0], ivec2(u, v), vertex[0+a]);
		emit(block_texture, light[2], ivec2(0, 0), vertex[2+a]);
		emit(block_texture, light[1], ivec2(u, 0), vertex[1+a]);
	}
	else
	{
		emit(block_texture, light[0], ivec2(u, v), vertex[0+a]);
		emit(block_texture, light[1], ivec2(u, 0), vertex[1+a]);
		emit(block_texture, light[3], ivec2(0, v), vertex[(3+a)%4]);
		emit(block_texture, light[2], ivec2(0, 0), vertex[2+a]);
	}
}
