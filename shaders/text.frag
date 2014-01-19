#version 150 core

uniform sampler2D sampler;

in vec2 fragment_uv;

out vec4 color;

void main() {
    color = texture(sampler, fragment_uv);
    color.a = max(color.a, 0.4);
}
