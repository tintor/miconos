#version 120

uniform sampler2D sampler;

varying vec2 fragment_uv;

void main() {
    vec4 color = texture2D(sampler, fragment_uv);
    color.a = max(color.a, 0.4);
    gl_FragColor = color;
}
