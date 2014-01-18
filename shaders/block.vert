#version 120

uniform mat4 matrix;

attribute vec3 position;
attribute vec3 color;
attribute vec2 uv;

varying vec2 fragment_uv;
varying vec3 fragment_color;

void main() {
    gl_Position = matrix * vec4(position, 1);
    fragment_uv = uv;
    fragment_color = color;
}
