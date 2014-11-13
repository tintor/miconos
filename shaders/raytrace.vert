#version 150

uniform int tick;
uniform vec3 eye;
uniform mat4 matrix;
uniform float foglimit2;

in vec2 position;

void main()
{
	gl_Position = position;
}
