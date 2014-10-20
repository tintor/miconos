#version 150

uniform vec3 eye;
uniform sampler2DArray sampler;

in float fragment_light;
in float fragment_texture;
in vec2 fragment_uv;
in vec3 fragment_color;
in float fog_factor;

out vec4 color;

const vec3 fog_color = vec3(0.2, 0.4, 1);

void main()
{
	color = texture(sampler, vec3(fragment_uv.x, fragment_uv.y, fragment_texture));
	if (color.w == 0.00) discard;
	color.xyz *= fragment_light;
	color.xyz = mix(color.xyz, fog_color, fog_factor);
}
