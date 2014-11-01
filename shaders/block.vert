#version 150

uniform int tick;
uniform vec3 eye;
uniform mat4 matrix;
uniform ivec3 cpos;

in ivec3 vertex0;
in ivec3 vertex1;
in ivec3 vertex2;
in ivec3 vertex3;
in int block_texture_with_flag;
in int light;
in int plane;

out ivec3 g_vertex[4];
out int g_block_texture_with_flag;
out int g_light;
out int g_plane;

void main()
{
	g_vertex[0] = vertex0;
	g_vertex[1] = vertex1;
	g_vertex[2] = vertex2;
	g_vertex[3] = vertex3;
	g_block_texture_with_flag = block_texture_with_flag;
	g_light = light;
	g_plane = plane;
}
