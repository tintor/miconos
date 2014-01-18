#version 120

uniform sampler2D sampler;

varying vec2 fragment_uv;
varying vec3 fragment_color;

void main() {
    gl_FragColor = vec4(texture2D(sampler, fragment_uv).rgb * fragment_color, 1);
}
