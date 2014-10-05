#version 150

uniform vec3 eye;
uniform sampler2D sampler;

//in float material;
in vec2 fragment_uv;
in vec3 fragment_color;
in float fog_factor;

out vec3 color;

const vec3 fog_color = vec3(0.2, 0.4, 1);

void main()
{
	color = texture(sampler, fragment_uv).xyz * fragment_color;
	/*if (material <= 204.5 && material >= 203.5)
        {
            vec2 f = floor(fragment_uv * 4);
	    if (int(f.x + f.y) % 2 == 0)
		color = vec3(1,1,1) - color;
        }*/
	color = mix(color, fog_color, fog_factor);
}
