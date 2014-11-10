#version 150 core

uniform sampler2D sampler;
uniform vec4 fg_color;
uniform vec4 bg_color;

in vec2 fragment_uv;

out vec4 color;

void main() {
    color = texture(sampler, fragment_uv);
    color = (color.a > 0.5) ? fg_color : bg_color;
}
