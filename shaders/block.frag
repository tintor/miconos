#version 150

uniform vec3 eye;
uniform sampler2DArray sampler;

in float fragment_light;
in float fragment_texture;
in vec2 fragment_uv;
in vec3 fragment_color;
in float fog_factor;

out vec3 color;

const vec3 fog_color = vec3(0.2, 0.4, 1);

void main()
{
	vec4 color4 = texture(sampler, vec3(fragment_uv.x, fragment_uv.y, fragment_texture));
	if (color4.w == 0.00) discard;
	color = color4.xyz * fragment_light;
	color = mix(color, fog_color, fog_factor);
}
