#version 150

uniform vec3 eye;
uniform sampler2DArray sampler;

in float fragment_light;
in float fragment_texture;
in float fragment_underwater_texture;
in vec2 fragment_uv;
in vec3 fragment_color;
in float fog_factor;

out vec4 color;

const vec3 fog_color = vec3(0.2, 0.4, 1);

void main()
{
	color = texture(sampler, vec3(fragment_uv.x, fragment_uv.y, fragment_texture));
	if (color.w == 0.00) discard;
	if (fragment_underwater_texture >= 0.f)
	{
		color.xyz = mix(color.xyz, texture(sampler, vec3(fragment_uv.x, fragment_uv.y, fragment_underwater_texture)).xyz, 0.5f) * 0.5f;
	}
	color.xyz *= fragment_light;
	color.xyz = mix(color.xyz, fog_color, fog_factor);
}
