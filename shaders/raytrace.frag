#version 150

uniform vec3 eye;
uniform sampler2DArray sampler;

out vec3 color;

uniform uimageBuffer block_data;

uint get_block(ivec3 pos)
{
	// TODO read from uniform buffer
	if (pos.z == 0) return
}

#ifdef DEBUG
    uniform int *buffer;
    #define assert(X) { if (!(X)) { atomicExchange(buffer, __LINE__); } }
#else
    #define assert(X) 
#endif

vec3 get_color(uint block)
{
	if (block == 0/*None*/) return vec3(0.2f, 0.4f, 1.0f);

}

// v must be on unit cube centered at 0
vec3 get_surface_color(uint block, vec3 v)
{
	//
}

float get_alpha(uint block)
{
	if (block == 0/*None*/) return 1.0f / 45;
	// TODO: check if blended block
	return 2; // default for solid blocks
}

// TODO - how to enable solid blocks with textures?
// compute face on the block entrance
// compute ray position on the block entrance
// compute uv on the block face
// read texel and use it as color with get_alpha = inf

// TODO - how to enable solid blocks with textures with holes AND transparent blocks with textures?
// will need volumetric textures and visit each volumetric texel on ray's path

// TODO - reflection and refraction rays (water)
// TODO - sun shadow
// TODO - per vertex ambient shadow (how to encode this info?)

void main()
{
	vec3 origin = gl_Position * inv_matrix;
	vec3 dir = normalize(origin - eye);

	ivec3 pos = floor(origin);
	ivec3 id = ((dir.x > 0) ? 1 : -1, (dir.y > 0) ? 1 : -1, (dir.z > 0) ? 1 : -1);
	vec3 dd(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z); // should be zero safe

	vec3 crossing; // next distance / time of crossing of each axis
	crossing.x = (dir.x > 0) ? ceil(origin.x) : floor(origin.x);
	crossing.y = (dir.y > 0) ? ceil(origin.y) : floor(origin.y);
	crossing.z = (dir.z > 0) ? ceil(origin.z) : floor(origin.z);
	crossing = (crossing - origin) * dd;
	// TODO: ensure that crossing[i] is +inf if dir[i] is very close to zero
	assert(crossing.x >= 0 && !isnan(crossing.x));
	assert(crossing.y >= 0 && !isnan(crossing.y));
	assert(crossing.z >= 0 && !isnan(crossing.z));

	float dist = 0; // current distance / time traveled
	vec3 color(0, 0, 0);
	float ma = 1; // remaining ray strength

	while (true)
	{
		float dist2;
		uint b = get_block(pos);

		if (crossing.x < crossing.y)
		{
			if (crossing.x < crossing.z)
			{
				dist2 = crossing.x;
				pos.x += id.x;
				crossing.x += dd.x;
			}
			else
			{
				dist2 = crossing.z;
				pos.z += id.z;
				crossing.z += dd.z;
			}
		}
		else
		{
			if (crossing.y < crossing.z)
			{
				dist2 = crossing.y;
				pos.y += id.y;
				crossing.y += dd.y;
			}
			else
			{
				dist2 = crossing.z;
				pos.z += id.z;
				crossing.z += dd.z;
			}
		}

		float da = (dist2 - dist) * get_alpha(b); // ray strength consumed by passing through block
		if (da >= ma) return color + get_color(b) * ma);
		color += get_color(b) * da;
		ma -= da;
		dist = dist2;
	}
}
