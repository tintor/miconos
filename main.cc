// TODO:
// rendering debugging:
// - draw frames around rendered chunks (or just render chunks as cubes)
// - enable rendering of triangle back sides (with different texture!)
// - ability to freeze triangles that are rendered + be able to move through it (debug what is visible)
// BUG - stray long distorted triangle
// BUG - some (chunk) faces missing / some (chunk) faces extra (top of moon)
// BUG - crash during flying

// # large spherical world / spherical gravity / print lat-long-alt
// disk persistence
// client / server
// multi-player
// sky with day/night cycle
// permanent server
// semi-transparent blocks (requires reversing order or rendering)
// textures with transparent pixels
// static cloud voxels
// user chat
// # integrate original marching cubes algo
// # [partial] color and shape pallete
// # semi-transparent alpha color component (0-> 25%) (1->50%) (2->75%) (3->100%)
// # free mouse (but fixed camera) mode for editing
// # texture mipmaps?
// # PERF level-of-detail rendering
// # portals (there is nice youtube demo and open source project!)
// # procedural textures
// # [partial] hidden triangle elimination between two slopes
// # [partial] slope generation
// # collision detection with slopes
// # transparent water blocks
// # real shadows from sun
// # nice sun with lens flare
// # point lights with shadows (for caves)?
// # PERF multi-threaded terrain generation
// # PERF multi-threaded chunk array buffer construction
// # PERF manual occulsion culling (for caves / mountains)
// # PERF OpenGL occulsion culling (for caves / mountains)
// # advanced voxel editing (flood fill, cut/copy/paste, move, drawing shapes)
// # wiki / user change tracking
// # real world elevation data
// # zero-g - use quaternions for orientation
// # translating blocks ?
// # simple water (minecraft) with darker light in depth (flood fill ocean!)
// # psysics: flowing water ?
// # psysics: moving objects / vehicles ?

#include <cstdint>

#include <cmath>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <unordered_map>

#define GLM_SWIZZLE
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/noise.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/fast_square_root.hpp"
#include "glm/gtx/intersect.hpp"

#include "rendering.hh"

// Config

const int VSYNC = 1;

// GUI

int width;
int height;

// Util

template<typename T> T sqr(T a) { return a * a; }

float sqr(glm::vec3 a) { return glm::dot(a, a); }

static const glm::ivec3 ix(1, 0, 0), iy(0, 1, 0), iz(0, 0, 1);

struct IVec2Hash
{
	size_t operator()(glm::ivec2 a) const { return a.x * 7919 + a.y * 7537; }
};

struct IVec3Hash
{
	size_t operator()(glm::ivec3 a) const { return a.x * 7919 + a.y * 7537 + a.z * 7687; }
};

float SimplexNoise(glm::vec2 p, int octaves, float freqf, float ampf, bool turbulent)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = turbulent ? fabs(glm::simplex(p)) : glm::simplex(p);
	for (int i = 1; i < octaves; i++)
	{
		freq *= freqf;
		amp *= ampf;
		max += amp;
		total += (turbulent ? fabs(glm::simplex(p * freq)) : glm::simplex(p * freq)) * amp;
	}
	return total / max;
}

class Sphere
{
public:
	Sphere(int size);
	std::vector<glm::ivec3>::iterator begin() { return m_list.begin(); }
	std::vector<glm::ivec3>::iterator end() { return m_list.end(); }
private:
	std::vector<glm::ivec3> m_list;
};

bool Closer(glm::ivec3 a, glm::ivec3 b) { return glm::dot(a, a) < glm::dot(b, b); }

Sphere::Sphere(int size)
{
	glm::ivec3 d;
	for (d.x = -size; d.x <= size; d.x++)
	{
		for (d.y = -size; d.y <= size; d.y++)
		{
			for (d.z = -size; d.z < size; d.z++)
			{
				if (glm::dot(d, d) <= size * size)
				{
					m_list.push_back(d);
				}
			}
		}
	}
	std::sort(m_list.begin(), m_list.end(), Closer);
}

Sphere render_sphere(31); // render limit

// Log

std::ofstream tracelog("tracelog");

// Map

// ===========================

glm::vec3 color_codes[64];

void InitColorCodes()
{
	for (int r = 0; r < 4; r++)
	{
		for (int g = 0; g < 4; g++)
		{
			for (int b = 0; b < 4; b++)
			{
				color_codes[r + g * 4 + b * 16] = glm::vec3(r / 3.0f, g / 3.0f, b / 3.0f);
			}
		}
	}
}

unsigned char ColorToCode(glm::vec3 color)
{
	int code = 0;
	float err = sqr(color_codes[code] - color);
	for (int i = 1; i < 64; i++)
	{
		float e = sqr(color_codes[i] - color);
		if (e < err)
		{
			err = e;
			code = i;
		}
	}
	return code;
}

struct Block
{
	unsigned char shape;
	unsigned char color; // 0-63
};

// ============================

const int ChunkSizeBits = 4, MapSizeBits = 6;
const int ChunkSize = 1 << ChunkSizeBits, MapSize = 1 << MapSizeBits;
const int ChunkSizeMask = ChunkSize - 1, MapSizeMask = MapSize - 1;

// ============================

struct MapChunk
{
	Block block[ChunkSize][ChunkSize][ChunkSize];

	void Set(int x, int y, int z, unsigned char shape, unsigned char color)
	{
		if (x >= 0 && x < ChunkSize && y >= 0 && y < ChunkSize && y >= 0 && y < ChunkSize)
		{
			block[x][y][z].shape = shape;
			block[x][y][z].color = color;
		}
	}
};

struct Heightmap
{
	int height[ChunkSize * MapSize][ChunkSize * MapSize];
	bool hasTree[ChunkSize * MapSize][ChunkSize * MapSize];
	glm::ivec2 last[MapSize][MapSize];

	Heightmap()
	{
		for (int x = 0; x < ChunkSize; x++)
		{
			for (int y = 0; y < ChunkSize; y++)
			{
				last[x][y] = glm::ivec2(1000000, 1000000);
			}
		}
	}

	void Populate(int cx, int cy);
	int& Height(int x, int y) { return height[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
	bool& HasTree(int x, int y) { return hasTree[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
};

int GetHeight(int x, int y)
{
	return SimplexNoise(glm::vec2(x, y) * 0.007f, 4, 0.5f, 0.5f, true) * 50;
}

int GetColor(int x, int y)
{
	return floorf((1 + SimplexNoise(glm::vec2(x, y) * -0.044f, 4, 0.5f, 0.5f, false)) * 8);
}

float Tree(int x, int y)
{
	return SimplexNoise(glm::vec2(x+321398, y+8901) * 0.005f, 4, 2.0f, 0.5f, true);
}

bool GetHasTree(int x, int y)
{
	float a = Tree(x, y);
	for (int xx = -1; xx <= 1; xx++)
	{
		for (int yy = -1; yy <= 1; yy++)
		{
			if ((xx != 0 || yy != 0) && a <= Tree(x + xx, y + yy))
				return false;
		}
	}
	return true;
}

void Heightmap::Populate(int cx, int cy)
{
	if (last[cx & MapSizeMask][cy & MapSizeMask] != glm::ivec2(cx, cy))
	{
		for (int x = 0; x < ChunkSize; x++)
		{
			for (int y = 0; y < ChunkSize; y++)
			{
				Height(x + cx * ChunkSize, y + cy * ChunkSize) = GetHeight(x + cx * ChunkSize, y + cy * ChunkSize);
				HasTree(x + cx * ChunkSize, y + cy * ChunkSize) = GetHasTree(x + cx * ChunkSize, y + cy * ChunkSize);
			}
		}
		last[cx & MapSizeMask][cy & MapSizeMask] = glm::ivec2(cx, cy);
	}
}

Heightmap* heightmap = new Heightmap;

int Level1Slope(int dx, int dy)
{
	int height = heightmap->Height(dx, dy);

	bool xa = heightmap->Height(dx-1, dy) > height;
	bool xb = heightmap->Height(dx+1, dy) > height;
	bool ya = heightmap->Height(dx, dy-1) > height;
	bool yb = heightmap->Height(dx, dy+1) > height;

	if ( xa && !xb &&  ya && !yb) return 127;
	if (!xa &&  xb &&  ya && !yb) return 191;
	if ( xa && !xb && !ya &&  yb) return 223;
	if (!xa &&  xb && !ya &&  yb) return 239;

	if (ya && !yb) return 63;
	if (yb && !ya) return 207;
	if (xa && !xb) return 95;
	if (xb && !xa) return 175;
	return 0;
}

// Swap bits P and Q in A
int BS(int a, int p, int q)
{
	return (a & ~(1 << p) & ~(1 << q)) | (((a >> p) & 1) << q) | (((a >> q) & 1) << p);
}

// Flip shape on X axis
int FX(int a)
{
	a = BS(a, 0, 1);
	a = BS(a, 2, 3);
	a = BS(a, 4, 5);
	a = BS(a, 6, 7);
	return a;
}

// Flip shape on Y axis
int FY(int a)
{
	a = BS(a, 0, 2);
	a = BS(a, 1, 3);
	a = BS(a, 4, 6);
	a = BS(a, 5, 7);
	return a;
}

void GenerateTerrain(glm::ivec3 cpos, MapChunk& chunk, bool terrain)
{
	memset(chunk.block, 0, sizeof(Block) * ChunkSize * ChunkSize * ChunkSize);

	heightmap->Populate(cpos.x, cpos.y);
	heightmap->Populate(cpos.x-1, cpos.y);
	heightmap->Populate(cpos.x+1, cpos.y);
	heightmap->Populate(cpos.x, cpos.y-1);
	heightmap->Populate(cpos.x, cpos.y+1);

	for (int x = 0; x < ChunkSize; x++)
	{
		for (int y = 0; y < ChunkSize; y++)
		{
			int dx = x + cpos.x * ChunkSize;
			int dy = y + cpos.y * ChunkSize;

			int height = heightmap->Height(dx, dy);
			if (height >= cpos.z * ChunkSize)
			{
				for (int z = 0; z < ChunkSize; z++)
				{
					if (z + cpos.z * ChunkSize == height)
					{
						chunk.block[x][y][z].shape = 255;
						chunk.block[x][y][z].color = GetColor(dx, dy);
						break;
					}
					chunk.block[x][y][z].shape = 255;
					chunk.block[x][y][z].color = 50;
				}
			}

			// slope on top
			int z = height + 1 - cpos.z * ChunkSize;
			if (z >= 0 && z < ChunkSize)
			{
				bool xa = heightmap->Height(dx-1, dy) > height;
				bool xb = heightmap->Height(dx+1, dy) > height;
				bool ya = heightmap->Height(dx, dy-1) > height;
				bool yb = heightmap->Height(dx, dy+1) > height;


				int shape = Level1Slope(dx, dy);
				if ((Level1Slope(dx-1, dy) == 223 || Level1Slope(dx-1, dy) == 207) && (Level1Slope(dx, dy+1) == 223 || Level1Slope(dx, dy+1) == 95))
				{
					shape = 77;
				}
				else if ((Level1Slope(dx+1, dy) == FX(223) || Level1Slope(dx+1, dy) == FX(207)) && (Level1Slope(dx, dy+1) == FX(223) || Level1Slope(dx, dy+1) == FX(95)))
				{
					shape = 142;
				}
				else if ((Level1Slope(dx-1, dy) == FY(223) || Level1Slope(dx-1, dy) == FY(207)) && (Level1Slope(dx, dy-1) == FY(223) || Level1Slope(dx, dy-1) == FY(95)))
				{
					shape = FY(77);
				}
				else if ((Level1Slope(dx+1, dy) == FX(FY(223)) || Level1Slope(dx+1, dy) == FX(FY(207))) && (Level1Slope(dx, dy-1) == FX(FY(223)) || Level1Slope(dx, dy-1) == FX(FY(95))))
				{
					shape = FX(FY(77));
				}
				if (shape != 0)
				{
					chunk.block[x][y][z].color = GetColor(dx, dy);
					chunk.block[x][y][z].shape = shape;
				}
			}
		}
	}

	for (int x = -2; x < ChunkSize + 2; x++)
	{
		for (int y = -2; y < ChunkSize + 2; y++)
		{
			int dx = x + cpos.x * ChunkSize;
			int dy = y + cpos.y * ChunkSize;

			if (heightmap->HasTree(dx, dy))
			{
				int height = heightmap->Height(dx, dy);

				for (int z = 0; z < ChunkSize; z++)
				{
					int dz = z + cpos.z * ChunkSize;
					if (dz > height && dz < height + 6)
					{
						// trunk
						chunk.block[x][y][z].color = 3*16;
						chunk.block[x][y][z].shape = 255;
					}
					if (dz > height + 2 && dz < height + 6)
					{
						// leaves
						chunk.Set(x-1, y, z, 255, 3*4);
						chunk.Set(x+1, y, z, 255, 3*4);
						chunk.Set(x, y-1, z, 255, 3*4);
						chunk.Set(x, y+1, z, 255, 3*4);
					}
				}
			}
		}
	}

	// simple moon
	int size = 1000;
	for (int x = 0; x < ChunkSize; x++)
	{
		int64_t sx = sqr<int64_t>(x + cpos.x * ChunkSize - size) - sqr<int64_t>(size);
		if (sx > 0) continue;

		for (int y = 0; y < ChunkSize; y++)
		{
			int64_t sy = sx + sqr<int64_t>(y + cpos.y * ChunkSize - size);
			if (sy > 0) continue;

			for (int z = 0; z < ChunkSize; z++)
			{
				int64_t sz = sy + sqr<int64_t>(z + cpos.z * ChunkSize);
				if (sz > 0) continue;

				chunk.block[x][y][z].shape = 255;
				chunk.block[x][y][z].color = 21*2;
			}
		}
	}
}


// ============================

struct Map
{
	Map();

	const MapChunk& Read(glm::ivec3 cpos)
	{
		MapChunk* mc = m_chunks[cpos];
		if (!mc)
		{
			mc = new MapChunk;
			GenerateTerrain(cpos, *mc, true);
			m_chunks[cpos] = mc;
		}
		return *mc;
	}

	void Write(glm::ivec3 cpos, MapChunk chunk)
	{
		MapChunk* mc = m_chunks[cpos];
		if (!mc)
		{
			mc = new MapChunk;
			m_chunks[cpos] = mc;
		}
		*mc = chunk;
	}

	void Set(glm::ivec3 pos, Block block, bool terrain = true)
	{
		MapChunk* mc = (pos >> ChunkSizeBits) == m_cpos ? m_chunk : m_chunks[pos >> ChunkSizeBits];
		if (!mc)
		{
			mc = new MapChunk;
			GenerateTerrain(pos >> ChunkSizeBits, *mc, terrain);
			m_chunk = m_chunks[pos >> ChunkSizeBits] = mc;
			m_cpos = pos >> ChunkSizeBits;
		}
		mc->block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask] = block;
	}

private:
	struct KeyHash
	{
		size_t operator()(glm::ivec3 a) const { return a.x * 7919 + a.y * 7537 + a.z * 7687; }
	};

	std::unordered_map<glm::ivec3, MapChunk*, KeyHash> m_chunks;

	MapChunk* m_chunk;
	glm::ivec3 m_cpos;

	void Set(glm::ivec3 pos, unsigned char shape, glm::vec3 color, bool terrain = true)
	{
		Block block;
		block.shape = shape;
		block.color = ColorToCode(color);
		Set(pos, block, terrain);
	}
};

int S(int a) { return 1 << a; }

Map::Map() : m_chunk(nullptr), m_cpos(0x80000000, 0, 0)
{
	InitColorCodes();

	// sphere test
	for (int x = -1; x <= 1; x++)
	{
		for (int y = -1; y <= 1; y++)
		{
			for (int z = -1; z <= 1; z++)
			{
				int dx = (x == 1) ? 1 : 0;
				int dy = (y == 1) ? 2 : 0;
				int dz = (z == 1) ? 4 : 0;

				unsigned char shape = 255;
				if (x == 0 && y != 0 && z != 0)
				{
					shape = 255 - S(0 + dy + dz) - S(1 + dy + dz);
				}
				if (x != 0 && y == 0 && z != 0)
				{
					shape = 255 - S(0 + dx + dz) - S(2 + dx + dz);
				}
				if (x != 0 && y != 0 && z == 0)
				{
					shape = 255 - S(0 + dx + dy) - S(4 + dx + dy);
				}
				if (x != 0 && y != 0 && z != 0)
				{
					int f = 0;
					if (x == -1) f ^= 1;
					if (y == -1) f ^= 2;
					if (z == -1) f ^= 4;
					shape = S(0^f) + S(1^f) + S(2^f) + S(4^f);
				}

				Set(glm::ivec3(x+1, y-10, z+30), shape, glm::vec3(1, 1, 0));
			}
		}
	}

	// all colors
	for (int x = 0; x < 4; x++)
		for (int y = 0; y < 4; y++)
			for (int z = 0; z < 4; z++)
			{
				Set(glm::ivec3(x*2, y*2, z*2+45), 255, glm::vec3(x*0.3333f, y*0.3333f, z*0.3333f));
			}

	// all blocks test
	for (int a = 0; a < 16; a++)
	{
		Set(glm::ivec3(-2, 0, 10 + a * 2), 255, glm::vec3(1, 1, 0));
	}

	for (int a = 0; a < 8; a++)
	{
		Set(glm::ivec3(0, 0, 10 + a * 2), 255 - S(a), glm::vec3(1, 0, 0));
	}

	for (int a = 0; a < 8; a++)
	{
		Set(glm::ivec3(0, 0, 10 + a * 2 + 16), S(a) + S(a^1) + S(a^2) + S(a^4), glm::vec3(0, 0, 1));
	}

	int q = 0;
	for (int a = 0; a < 8; a += 1)
	{
		for (int b = 0; b < a; b += 1)
		{
			if ((a ^ b) == 1)
			{
				Set(glm::ivec3(-4, 0, 10 + q), 255 - S(a ^ 6) - S(b ^ 6), glm::vec3(0, 1, 0));
				q += 2;
			}
			if ((a ^ b) == 2)
			{
				Set(glm::ivec3(-4, 0, 10 + q), 255 - S(a ^ 5) - S(b ^ 5), glm::vec3(.5, .5, 1));
				q += 2;
			}
			if ((a ^ b) == 4)
			{
				Set(glm::ivec3(-4, 0, 10 + q), 255 - S(a ^ 3) - S(b ^ 3), glm::vec3(1, 0.5, 0));
				q += 2;
			}
		}
	}
}

Map* map = new Map;

// ============================

struct Vertex
{
	GLubyte color;
	GLubyte light;
	GLubyte uv;
	GLubyte pos[3];
};

struct Chunk
{
	Block block[ChunkSize][ChunkSize][ChunkSize];
	glm::ivec3 cpos;
	bool buffered;
	std::vector<Vertex> vertices;

	void ClearBuffer()
	{
		buffered = false;
		std::vector<Vertex> a;
		std::swap(vertices, a);
	}

	Chunk() : buffered(false), cpos(0x80000000, 0, 0) { }
};

Chunk* map_cache[MapSize][MapSize][MapSize];

Chunk& GetChunk(glm::ivec3 cpos)
{
	return *map_cache[cpos.x & MapSizeMask][cpos.y & MapSizeMask][cpos.z & MapSizeMask];
}

Block& map_get_block(glm::ivec3 pos)
{
	return GetChunk(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask];
}

unsigned char map_get(glm::ivec3 pos)
{
	return map_get_block(pos).shape;
}

unsigned char map_get_color(glm::ivec3 pos)
{
	return map_get_block(pos).color;
}

float map_refresh_time_ms = 0;

glm::ivec3 last_cplayer(0x80000000, 0, 0);

// extend world if player moves
void map_refresh(glm::ivec3 player)
{
	glm::ivec3 cplayer = player / ChunkSize;
	if (cplayer == last_cplayer)
		return;
	last_cplayer = cplayer;

	float time_start = glfwGetTime();
	for (glm::ivec3 d : render_sphere)
	{
		glm::ivec3 cpos = cplayer + d;
		Chunk& chunk = GetChunk(cpos);
		if (chunk.cpos != cpos)
		{
			const MapChunk& mc = map->Read(cpos);
			memcpy(chunk.block, mc.block, sizeof(mc.block));
			chunk.ClearBuffer();
			chunk.cpos = cpos;
		}
	}
	map_refresh_time_ms = (glfwGetTime() - time_start) * 1000;
}

// Model

static glm::vec3 player_position(0, 0, 20);
static float player_yaw = 0, player_pitch = 0;
glm::mat4 player_orientation;
float last_time;

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
float sel_dist;
bool selection = false;

bool flying = true;

bool wireframe = false;

int slope_shapes[] = {
	255 - 128 - 64,
	255 - 2 - 1,
	255 - 32 - 16,
	255 - 8 - 4,
	255 - 64 - 16,
	255 - 8 - 2,
	255 - 128 - 32,
	255 - 4 - 1,
	255 - 32 - 2,
	255 - 64 - 4,
	255 - 16 - 1,
	255 - 128 - 8 };

int pyramid_shapes[] = {
	23,
	43,
	13 + 64,
	8 + 4 + 2 + 128,
	16 + 32 + 64 + 1,
	32 + 16 + 128 + 2,
	64 + 128 + 16 + 4,
	128 + 64 + 32 + 8 };

int anti_pyramid_shapes[] = {
	254,
	253,
	251,
	247,
	255 - 16,
	255 - 32,
	255 - 64,
	127 };

int NextShape(int shape)
{
	if (shape == 255) return slope_shapes[0];
	if (shape == slope_shapes[11]) return pyramid_shapes[0];
	if (shape == pyramid_shapes[7]) return anti_pyramid_shapes[0];
	if (shape == anti_pyramid_shapes[7]) return 255;
	for (int i = 0; i < 11; i++) if (shape == slope_shapes[i]) return slope_shapes[i+1];
	for (int i = 0; i < 7; i++) if (shape == pyramid_shapes[i]) return pyramid_shapes[i+1];
	for (int i = 0; i < 7; i++) if (shape == anti_pyramid_shapes[i]) return anti_pyramid_shapes[i+1];
	return 0;
}

int PrevShape(int shape)
{
	if (shape == 255) return anti_pyramid_shapes[7];
	if (shape == slope_shapes[0]) return 255;
	if (shape == pyramid_shapes[0]) return slope_shapes[11];
	if (shape == anti_pyramid_shapes[0]) return pyramid_shapes[7];
	for (int i = 1; i < 12; i++) if (shape == slope_shapes[i]) return slope_shapes[i-1];
	for (int i = 1; i < 8; i++) if (shape == pyramid_shapes[i]) return pyramid_shapes[i-1];
	for (int i = 1; i < 8; i++) if (shape == anti_pyramid_shapes[i]) return anti_pyramid_shapes[i-1];
	return 0;
}

void EditBlock(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	glm::ivec3 p(pos.x & ChunkSizeMask, pos.y & ChunkSizeMask, pos.z & ChunkSizeMask);

	map->Set(pos, block);
	map_get_block(pos) = block;
	GetChunk(cpos).ClearBuffer();

	if (p.x == 0) GetChunk(cpos - ix).ClearBuffer();
	if (p.y == 0) GetChunk(cpos - iy).ClearBuffer();
	if (p.z == 0) GetChunk(cpos - iz).ClearBuffer();
	if (p.x == ChunkSizeMask) GetChunk(cpos + ix).ClearBuffer();
	if (p.y == ChunkSizeMask) GetChunk(cpos + iy).ClearBuffer();
	if (p.z == ChunkSizeMask) GetChunk(cpos + iz).ClearBuffer();
}

bool mode = false;

std::deque<std::string> console_lines;
bool console_active = false;

bool KeyToChar(int key, int mods, char& ch)
{
	bool shift = mods & GLFW_MOD_SHIFT;
	if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
	{
		ch = (shift ? 'A' : 'a') + key - GLFW_KEY_A;
		return true;
	}
	if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
	{
		ch = ((mods & GLFW_MOD_SHIFT) ? ")!@#$%^&*(" : "0123456789")[key - GLFW_KEY_0];
		return true;
	}
	switch (key)
	{
	case GLFW_KEY_COMMA: ch = shift ? '<' : ','; break;
	case GLFW_KEY_PERIOD: ch = shift ? '>' : '.'; break;
	case GLFW_KEY_BACKSLASH: ch = shift ? '?' : '/'; break;
	case GLFW_KEY_SEMICOLON: ch = shift ? ':' : ';'; break;
	case GLFW_KEY_APOSTROPHE: ch = shift ? '"' : '\''; break;
	case GLFW_KEY_EQUAL: ch = shift ? '+' : '='; break;
	case GLFW_KEY_MINUS: ch = shift ? '_' : '-'; break;
	case GLFW_KEY_LEFT_BRACKET: ch = shift ? '{' : '['; break;
	case GLFW_KEY_RIGHT_BRACKET: ch = shift ? '}' : ']'; break;
	case GLFW_KEY_SLASH: ch = shift ? '|' : '\\'; break;
	case GLFW_KEY_GRAVE_ACCENT: ch = shift ? '~' : '`'; break;
	case GLFW_KEY_SPACE: ch = ' '; break;
	default: return false;
	}
	return true;
}

void console_print(const std::string& line)
{
	console_lines.push_back(line);
	if (console_lines.size() == 40)
	{
		console_lines.pop_front();
	}
}

void console_execute(const std::string& command)
{
	console_print("[" + command + "]");
}

void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	if (console_active)
	{
		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			char ch;
			if (key == GLFW_KEY_BACKSPACE)
			{
				std::string& line = console_lines[console_lines.size() - 1];
				if (line.size() > 2)
				{
					line = line.substr(0, line.size() - 1);
					line[line.size() - 1] = '_';
				}
			}
			else if (key == GLFW_KEY_F2)
			{
				console_active = false;
			}
			else if (key == GLFW_KEY_ENTER)
			{
				std::string line = console_lines[console_lines.size() - 1];
				console_lines.pop_back();
				console_execute(line.substr(1, line.size() - 2));
				console_print(">_");
			}
			else if (KeyToChar(key, mods, /*out*/ch))
			{
				std::string& line = console_lines[console_lines.size() - 1];
				if (line.size() < 120)
				{
					line[line.size() - 1] = ch;
					line.append(1, '_');
				}
			}
		}
	}
	else if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_F2)
		{
			console_active = true;
		}
		else if (key == GLFW_KEY_F3)
		{
			wireframe = !wireframe;
		}
		else if (key == GLFW_KEY_TAB)
		{
			flying = !flying;
		}
		else if (key == GLFW_KEY_F1)
		{
			mode = !mode;
			glm::ivec3 player(player_position.x, player_position.y, player_position.z);
			glm::ivec3 cplayer = player / ChunkSize;
			for (glm::ivec3 d : render_sphere)
			{
				GetChunk(cplayer + d).ClearBuffer();
			}
		}
		else if (selection)
		{
			Block block = map_get_block(sel_cube);
			int red = block.color % 4;
			int green = (block.color / 4) % 4;
			int blue = block.color / 16;

			if (key == GLFW_KEY_Z)
			{
				block.color = ((red + 1) % 4) + green * 4 + blue * 16;
				EditBlock(sel_cube, block);
			}
			if (key == GLFW_KEY_X)
			{
				block.color = red + ((green + 1) % 4) * 4 + blue * 16;
				EditBlock(sel_cube, block);
			}
			if (key == GLFW_KEY_C)
			{
				block.color = red + green * 4 + ((blue + 1) % 4) * 16;
				EditBlock(sel_cube, block);
			}
			if (key == GLFW_KEY_V)
			{
				block.shape = NextShape(block.shape);
				EditBlock(sel_cube, block);
			}
			if (key == GLFW_KEY_B)
			{
				block.shape = PrevShape(block.shape);
				EditBlock(sel_cube, block);
			}
		}
	}
}

void OnMouseButton(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && selection)
	{
		Block block;
		block.shape = 0;
		block.color = 0;
		EditBlock(sel_cube, block);
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		EditBlock(sel_cube + dir[sel_face], map_get_block(sel_cube));
	}
}

void model_init(GLFWwindow* window)
{
	glfwSetKeyCallback(window, OnKey);
	glfwSetMouseButtonCallback(window, OnMouseButton);

	last_time = glfwGetTime();

	glfwSetCursorPos(window, 0, 0);

	for (int x = 0; x < MapSize; x++)
	{
		for (int y = 0; y < MapSize; y++)
		{
			for (int z = 0; z < MapSize; z++)
			{
				map_cache[x][y][z] = new Chunk;
				map_cache[x][y][z]->cpos.x = 0x80000000;
			}
		}
	}

	InitColorCodes();

	console_lines.push_back(">_");
}

uint NeighborBit(int dx, int dy, int dz)
{
	return 1u << ((dx + 1) + (dy + 1) * 3 + (dz + 1) * 9);
}

bool IsVertexFree(uint neighbors, int dx, int dy, int dz)
{
	assert(abs(dx) + abs(dy) + abs(dz) == 3);
	uint mask = NeighborBit(dx, dy, dz);
	mask |= NeighborBit(0, dy, dz) | NeighborBit(dx, 0, dz) | NeighborBit(dx, dy, 0);
	mask |= NeighborBit(0, 0, dz) | NeighborBit(dx, 0, 0) | NeighborBit(0, dy, 0);
	return (neighbors & mask) == 0;
}

bool IsEdgeFree(uint neighbors, int dx, int dy, int dz)
{
	assert(abs(dx) + abs(dy) + abs(dz) == 2);
	uint mask = NeighborBit(dx, dy, dz);
	mask |= NeighborBit(0, dy, dz) | NeighborBit(dx, 0, dz) | NeighborBit(dx, dy, 0);
	return (neighbors & mask) == 0;
}

int Resolve(glm::vec3& center, float k, float x, float y, float z)
{
	glm::vec3 d(x, y, z);
	float ds = sqr(d);
	if (ds < 1e-6) return -1;
	center += d * (k / sqrtf(ds) - 1);
	return 1;
}

// Move sphere so that it is not coliding with cube
//
// Neighbors is 3x3x3 matrix of 27 bits representing nearby cubes.
// Used to turn off vertices and edges that are not exposed.
int SphereVsCube(glm::vec3& center, float radius, glm::ivec3 cube, uint neighbors)
{
	float e = 0.5;
	float x = center.x - cube.x - e;
	float y = center.y - cube.y - e;
	float z = center.z - cube.z - e;
	float k = e + radius;

	if (x >= k || x <= -k || y >= k || y <= -k || z >= k || z <= -k) return 0;

	float s2 = sqr(radius + sqrtf(2) / 2);
	if (x*x + y*y >= s2 || x*x + z*z >= s2 || y*y + z*z >= s2) return 0;

	if (x*x + y*y + z*z >= sqr(radius + sqrt(3) / 2)) return 0;

	// if center outside cube
	if (x > +e)
	{
		if (y > +e)
		{
			if (z > +e) return IsVertexFree(neighbors, 1, 1, +1) ? Resolve(center, radius, x - e, y - e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, 1, 1, -1) ? Resolve(center, radius, x - e, y - e, z + e) : -1;
			return IsEdgeFree(neighbors, 1, 1, 0) ? Resolve(center, radius, x - e, y - e, 0) : -1;
		}
		if (y < -e)
		{
			if (z > +e) return IsVertexFree(neighbors, 1, -1, +1) ? Resolve(center, radius, x - e, y + e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, 1, -1, -1) ? Resolve(center, radius, x - e, y + e, z + e) : -1;
			return IsEdgeFree(neighbors, 1, -1, 0) ? Resolve(center, radius, x - e, y + e, 0) : -1;
		}

		if (z > +e) return IsEdgeFree(neighbors, 1, 0, +1) ? Resolve(center, radius, x - e, 0, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, 1, 0, -1) ? Resolve(center, radius, x - e, 0, z + e) : -1;
		center.x += radius - x + e;
		return 1;
	}

	if (x < -e)
	{
		if (y > +e)
		{
			if (z > +e) return IsVertexFree(neighbors, -1, 1, +1) ? Resolve(center, radius, x + e, y - e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, -1, 1, -1) ? Resolve(center, radius, x + e, y - e, z + e) : -1;
			return IsEdgeFree(neighbors, -1, 1, 0) ? Resolve(center, radius, x + e, y - e, 0) : -1;
		}
		if (y < -e)
		{
			if (z > +e) return IsVertexFree(neighbors, -1, -1, +1) ? Resolve(center, radius, x + e, y + e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, -1, -1, -1) ? Resolve(center, radius, x + e, y + e, z + e) : -1;
			return IsEdgeFree(neighbors, -1, -1, 0) ? Resolve(center, radius, x + e, y + e, 0) : -1;
		}

		if (z > +e) return IsEdgeFree(neighbors, -1, 0, +1) ? Resolve(center, radius, x + e, 0, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, -1, 0, -1) ? Resolve(center, radius, x + e, 0, z + e) : -1;
		center.x += -radius - x - e;
		return 1;
	}

	if (y > +e)
	{
		if (z > +e) return IsEdgeFree(neighbors, 0, 1, +1) ? Resolve(center, radius, 0, y - e, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, 0, 1, -1) ? Resolve(center, radius, 0, y - e, z + e) : -1;
		center.y += radius - y + e;
		return 1;
	}
	if (y < -e)
	{
		if (z > +e) return IsEdgeFree(neighbors, 0, -1, +1) ? Resolve(center, radius, 0, y + e, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, 0, -1, -1) ? Resolve(center, radius, 0, y + e, z + e) : -1;
		center.y += -radius - y - e;
		return 1;
	}

	if (z > +e)
	{
		center.z +=  radius - z + e;
		return 1;
	}
	if (z < -e)
	{
		center.z += -radius - z - e;
		return 1;
	}

	// center inside cube
	float ax = fabs(x), ay = fabs(y);
	if (ax > ay)
	{
		if (ax > fabs(z)) { center.x += (x > 0 ? k : -k) - x; return 1; }
	}
	else
	{
		if (ay > fabs(z)) { center.y += (y > 0 ? k : -k) - y; return 1; }
	}
	center.z += (z > 0 ? k : -k) - z;
	return 1;
}

uint CubeNeighbors(glm::ivec3 cube)
{
	uint neighbors = 0;
	for (int dx = -1; dx <= 1; dx++)
	{
		for (int dy = -1; dy <= 1; dy++)
		{
			for (int dz = -1; dz <= 1; dz++)
			{
				if (map_get(glm::ivec3(cube.x + dx, cube.y + dy, cube.z + dz)) != 0)
				{
					neighbors |= NeighborBit(dx, dy, dz);
				}
			}
		}
	}
	return neighbors;
}

glm::vec3 player_velocity;

void ResolveCollisionsWithBlocks()
{
	int px = roundf(player_position.x);
	int py = roundf(player_position.y);
	int pz = roundf(player_position.z);

	glm::vec3 start = player_position;
	for (int i = 0; i < 100; i++)
	{
		// Resolve all collisions simultaneously
		glm::vec3 sum(0, 0, 0);
		int c = 0;
		for (int x = px - 3; x <= px + 3; x++)
		{
			for (int y = py - 3; y <= py + 3; y++)
			{
				for (int z = pz - 3; z <= pz + 3; z++)
				{
					glm::ivec3 cube(x, y, z);
					if (map_get(cube) != 0)
					{
						glm::vec3 p = player_position;
						if (1 == SphereVsCube(p, 0.8f, cube, CubeNeighbors(cube)))
						{
							sum += p;
							c += 1;
						}
					}
				}
			}
		}
		if (c == 0)
			break;
		player_velocity.z = 0;
		player_position = sum / (float)c;
	}
}

void Simulate(float dt, glm::vec3 dir)
{
	if (!flying) player_velocity.z -= dt * 15;
	player_position += dir * ((flying ? 10 : 4) * dt) + player_velocity * dt;
	ResolveCollisionsWithBlocks();
}

void model_move_player(GLFWwindow* window, float dt)
{
	glm::vec3 dir(0, 0, 0);

	float* m = glm::value_ptr(player_orientation);
	glm::vec3 forward(m[4], m[5], m[6]);
	glm::vec3 right(m[0], m[1], m[2]);

	if (!flying)
	{
		forward.z = 0;
		forward = glm::normalize(forward);
		right.z = 0;
		right = glm::normalize(right);
	}
	if (glfwGetKey(window, 'A')) dir -= right;
	if (glfwGetKey(window, 'D')) dir += right;
	if (glfwGetKey(window, 'W')) dir += forward;
	if (glfwGetKey(window, 'S')) dir -= forward;
	if (glfwGetKey(window, 'Q')) dir[2] += 1;
	if (glfwGetKey(window, 'E')) dir[2] -= 1;

	if (!flying && glfwGetKey(window, GLFW_KEY_SPACE) && player_velocity.z == 0)
	{
		player_velocity.z = 10;
	}

	glm::vec3 p = player_position;
	if (dir.x != 0 || dir.y != 0 || dir.z != 0)
	{
		dir = glm::normalize(dir);
	}
	while (dt > 0)
	{
		if (dt <= 0.01)
		{
			Simulate(dt, dir);
			break;
		}
		Simulate(0.01, dir);
		dt -= 0.01;
	}
	if (p != player_position)
	{
		tracelog << "position = [" << player_position.x << " " << player_position.y << " " << player_position.z << "]" << std::endl;
	}
}

glm::ivec3 Corner(int c) { return glm::ivec3(c&1, (c>>1)&1, (c>>2)&1); }
glm::ivec3 corner[8] = { Corner(0), Corner(1), Corner(2), Corner(3), Corner(4), Corner(5), Corner(6), Corner(7) };

bool IntersectRayTriangle(glm::vec3 orig, glm::vec3 dir, glm::ivec3 cube, float& dist, int triangle[3])
{
	glm::vec3 a = glm::vec3(cube + corner[triangle[0]]);
	glm::vec3 b = glm::vec3(cube + corner[triangle[1]]);
	glm::vec3 c = glm::vec3(cube + corner[triangle[2]]);
	glm::vec3 pos;
	if (!glm::intersectRayTriangle(orig, dir, a, b, c, /*out*/pos)) return false;
	return glm::intersectRayPlane(orig, dir, a, glm::cross(b - a, c - a), /*out*/dist);
}

bool IntersectRayCube(glm::vec3 orig, glm::vec3 dir, glm::ivec3 cube, float& dist, int& face)
{
	int triangles[12][3] = { { 0, 4, 6 }, { 0, 6, 2 },
					 { 1, 3, 7 }, { 1, 7, 5 },
					 { 0, 1, 5 }, { 0, 5, 4 },
					 { 2, 6, 7 }, { 2, 7, 3 },
					 { 0, 2, 3 }, { 0, 3, 1 },
					 { 4, 5, 7 }, { 4, 7, 6 } };
	bool found = false;
	for (int i = 0; i < 12; i++)
	{
		float q_dist;
		if (IntersectRayTriangle(orig, dir, cube, q_dist, triangles[i]) && (!found || q_dist < dist))
		{
			dist = q_dist;
			found = true;
			face = i / 2;
		}
	}
	return found;
}

bool SelectCube(glm::ivec3& sel_cube, float& sel_dist, int& sel_face)
{
	float* ma = glm::value_ptr(player_orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);

	int px = roundf(player_position.x);
	int py = roundf(player_position.y);
	int pz = roundf(player_position.z);

	bool found = false;
	int Dist = 6;
	for (int x = px - Dist; x <= px + Dist; x++)
	{
		for (int y = py - Dist; y <= py + Dist; y++)
		{
			for (int z = pz - Dist; z <= pz + Dist; z++)
			{
				glm::ivec3 i(x, y, z);
				if (map_get(i) != 0)
				{
					float dist; int face;
					if (IntersectRayCube(player_position, dir, i, /*out*/dist, /*out*/face) && dist > 0 && dist <= Dist && (!found || dist < sel_dist))
					{
						found = true;
						sel_cube = i;
						sel_dist = dist;
						sel_face = face;
					}
				}
			}
		}
	}
	return found;
}

double last_cursor_x = 0;
double last_cursor_y = 0;

void model_frame(GLFWwindow* window)
{
	double time = glfwGetTime();
	double dt = (time - last_time) < 0.5 ? (time - last_time) : 0.5;
	last_time = time;

	double cursor_x, cursor_y;
	glfwGetCursorPos(window, &cursor_x, &cursor_y);
	if (cursor_x != last_cursor_x || cursor_y != last_cursor_y)
	{
		player_yaw += (cursor_x - last_cursor_x) / 150;
		player_pitch += (cursor_y - last_cursor_y) / 150;
		if (player_pitch > M_PI / 2) player_pitch = M_PI / 2;
		if (player_pitch < -M_PI / 2) player_pitch = -M_PI / 2;
		//glfwSetCursorPos(window, 0, 0);
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
		player_orientation = glm::rotate(glm::rotate(glm::mat4(), -player_yaw, glm::vec3(0, 0, 1)), -player_pitch, glm::vec3(1, 0, 0));

		float* m = glm::value_ptr(player_orientation);
		glm::vec3 forward(m[4], m[5], m[6]);
		glm::vec3 right(m[0], m[1], m[2]);
		tracelog << "orientation = [" << m[0] << " " << m[1] << " " << m[2] << ", " << m[4] << " " << m[5] << " " << m[6] << ", " << m[8] << " " << m[9] << " " << m[10] << "]" << std::endl;

		perspective_rotation = glm::rotate(perspective, player_pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, player_yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
	}
	
	if (!console_active)
	{
		model_move_player(window, dt);
	}

	map_refresh(glm::ivec3(player_position.x, player_position.y, player_position.z));

	selection = SelectCube(/*out*/sel_cube, /*out*/sel_dist, /*out*/sel_face);
}

// Render

GLuint block_texture;
glm::vec3 light_direction(0.5, 1, -1);

GLubyte light_cache[8][8][8];

float clampf(float a, float min, float max)
{
	if (a > max) return max;
	if (a < min) return min;
	return a;
}

void InitLightCache()
{
	for (int a = 0; a < 8; a++)
	{
		for (int b = 0; b < 8; b++)
		{
			for (int c = 0; c < 8; c++)
			{
				glm::vec3 normal = glm::normalize((glm::vec3)glm::cross(glm::vec3(corner[b] - corner[a]), glm::vec3(corner[c] - corner[a])));
				float cos_angle = std::max<float>(0, -glm::dot(normal, light_direction));
				float light = clampf(0.3f + cos_angle * 0.7f, 0.0f, 1.0f);
				light_cache[a][b][c] = (uint) floorf(clampf(light * 256.f, 0.0f, 255.0f));
			}
		}
	}
}

Text* text = nullptr;

int line_program;
GLuint line_matrix_loc;
GLuint line_position_loc;

int block_program;
GLuint block_matrix_loc;
GLuint block_sampler_loc;
GLuint block_pos_loc;
GLuint block_eye_loc;
GLuint block_position_loc;
GLuint block_color_loc;
GLuint block_light_loc;
GLuint block_uv_loc;

float simplex2(glm::vec2 p, int octaves, float persistence)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = glm::simplex(p);
	for (int i = 1; i < octaves; i++)
	{
	    freq /= sqrt(2);
	    amp *= persistence;
	    max += amp;
	    total += glm::simplex(p * freq) * amp;
	}
	return total / max;
}

void load_noise_texture(int size)
{
	GLubyte* image = new GLubyte[size * size * 3];
	for (int x = 0; x < size; x++)
	{
		for (int y = 0; y < size; y++)
		{
			glm::vec2 p = glm::vec2(x+32131, y+908012) * (1.0f / size);
			float f = simplex2(p * 32.f, 10, 1) * 3;
			f = fabs(f);
			f = std::min<float>(f, 1);
			f = std::max<float>(f, -1);
			//f = (f + 1) / 2;
			GLubyte a = (int) std::min<float>(255, floorf(f * 256));
			image[(x + y * size) * 3] = a;


			p = glm::vec2(x+9420234, y+9808312) * (1.0f / size);
			f = simplex2(p * 32.f, 10, 1) * 3;
			f = fabs(f);
			f = std::min<float>(f, 1);
			f = std::max<float>(f, -1);
			//f = (f + 1) / 2;
			a = (int) std::min<float>(255, floorf(f * 256));
			image[(x + y * size) * 3 + 1] = a;


			p = glm::vec2(x+983322, y+1309329) * (1.0f / size);
			f = simplex2(p * 32.f, 10, 1) * 3;
			f = fabs(f);
			f = std::min<float>(f, 1);
			f = std::max<float>(f, -1);
			//f = (f + 1) / 2;
			a = (int) std::min<float>(255, floorf(f * 256));
			image[(x + y * size) * 3 + 2] = a;
		}
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	delete[] image;
}

void render_init()
{
	printf("OpenGL version: [%s]\n", glGetString(GL_VERSION));
	glEnable(GL_CULL_FACE);
	glClearColor(0.2, 0.4, 1, 1.0);

	glGenTextures(1, &block_texture);
	glBindTexture(GL_TEXTURE_2D, block_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	//load_noise_texture(512);
	load_png_texture("noise.png");

	light_direction = glm::normalize(light_direction);
	InitLightCache();

	perspective = glm::perspective<float>(M_PI / 180 * 75, width / (float)height, 0.03, 1000);
	text = new Text;

	line_program = load_program("line");
	line_matrix_loc = glGetUniformLocation(line_program, "matrix");
	line_position_loc = glGetAttribLocation(line_program, "position");

	block_program = load_program("block");
	block_matrix_loc = glGetUniformLocation(block_program, "matrix");
	block_sampler_loc = glGetUniformLocation(block_program, "sampler");
	block_pos_loc = glGetUniformLocation(block_program, "pos");
	block_eye_loc = glGetUniformLocation(block_program, "eye");

	Error("inita2");
	block_position_loc = glGetAttribLocation(block_program, "position");
	block_color_loc = glGetAttribLocation(block_program, "color");
	block_light_loc = glGetAttribLocation(block_program, "light");
	block_uv_loc = glGetAttribLocation(block_program, "uv");
	Error("inita");
}

int triangle_count = 0;
int chunk_count = 0;

GLuint block_buffer;
GLuint line_buffer;

GLfloat* line_data = new GLfloat[24 * 3];

void render_general_init()
{
	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &line_buffer);
}

std::vector<Vertex>* vertex_data;

void vertex(glm::ivec3 pos, GLubyte color, GLubyte light, GLubyte uv, int c)
{
	Vertex v;
	v.color = color;
	v.light = light;
	v.uv = uv;
	v.pos[0] = (pos.x & ChunkSizeMask) + corner[c].x;
	v.pos[1] = (pos.y & ChunkSizeMask) + corner[c].y;
	v.pos[2] = (pos.z & ChunkSizeMask) + corner[c].z;
	vertex_data->push_back(v);
}

void draw_quad(glm::ivec3 pos, int a, int b, int c, int d, GLubyte color)
{
	GLubyte light = light_cache[a][b][c];
	vertex(pos, color, light, 0, a);
	vertex(pos, color, light, 1, b);
	vertex(pos, color, light, 3, c);
	vertex(pos, color, light, 0, a);
	vertex(pos, color, light, 3, c);
	vertex(pos, color, light, 2, d);
}

void draw_triangle(glm::ivec3 pos, int a, int b, int c, GLubyte color)
{
	GLubyte light = light_cache[a][b][c];
	vertex(pos, color, light, 0, a);
	vertex(pos, color, light, 1, b);
	vertex(pos, color, light, 3, c);
}

int B(int a, int i) { return (a & (1 << i)) >> i; }

void draw_face(glm::ivec3 pos, int block, int a, int b, int c, int d, GLubyte color)
{
	int vertices = B(block, a) + B(block, b) + B(block, c) + B(block, d);

	if (vertices == 4)
	{
		draw_quad(pos, a, b, c, d, color);
	}
	else if (vertices == 3)
	{
		GLubyte light = light_cache[a][b][c];
		if (block & (1 << a)) vertex(pos, color, light, 0, a);
		if (block & (1 << b)) vertex(pos, color, light, 1, b);
		if (block & (1 << c)) vertex(pos, color, light, 3, c);
		if (block & (1 << d)) vertex(pos, color, light, 2, d);
	}
}

void render_block(glm::ivec3 pos, GLubyte color)
{
	if (map_get(pos - ix) != 255)
	{
		draw_quad(pos, 0, 4, 6, 2, color);
	}
	if (map_get(pos + ix) != 255)
	{
		draw_quad(pos, 1, 3, 7, 5, color);
	}
	if (map_get(pos - iy) != 255)
	{
		draw_quad(pos, 0, 1, 5, 4, color);
	}
	if (map_get(pos + iy) != 255)
	{
		draw_quad(pos, 2, 6, 7, 3, color);
	}
	if (map_get(pos - iz) != 255)
	{
		draw_quad(pos, 0, 2, 3, 1, color);
	}
	if (map_get(pos + iz) != 255)
	{
		draw_quad(pos, 4, 5, 7, 6, color);
	}
}

int MinX(int a)
{
	return a & ~(1 << 1) & ~(1 << 3) & ~(1 << 5) & ~(1 << 7);
}

int MaxX(int a)
{
	return (a & ~(1 << 0) & ~(1 << 2) & ~(1 << 4) & ~(1 << 6)) >> 1;
}

// every bit from 0 to 7 in block represents once vertex (can be off or on)
// cube is 255, empty is 0, prisms / pyramids / anti-pyramids are in between
void render_general(glm::ivec3 pos, int block, GLubyte color)
{
	if (block == 0)
	{
		return;
	}
	if (block == 255)
	{
		render_block(pos, color);
		return;
	}

	// common faces
	if (mode)
	{
		if (MaxX(map_get(pos - ix)) != MinX(block))
		{
			draw_face(pos, block, 0, 4, 6, 2, color);
		}
		if (MinX(map_get(pos + ix)) != MaxX(block))
		{
			draw_face(pos, block, 1, 3, 7, 5, color);
		}
	}
	else
	{
		if (map_get(pos - ix) != 255)
		{
			draw_face(pos, block, 0, 4, 6, 2, color);
		}
		if (map_get(pos + ix) != 255)
		{
			draw_face(pos, block, 1, 3, 7, 5, color);
		}
	}
	if (map_get(pos - iy) != 255)
	{
		draw_face(pos, block, 0, 1, 5, 4, color);
	}
	if (map_get(pos + iy) != 255)
	{
		draw_face(pos, block, 2, 6, 7, 3, color);
	}
	if (map_get(pos - iz) != 255)
	{
		draw_face(pos, block, 0, 2, 3, 1, color);
	}
	if (map_get(pos + iz) != 255)
	{
		draw_face(pos, block, 4, 5, 7, 6, color);
	}
/*
	if (map_get(pos - ix) != 255)
	{
		draw_face(pos, block, 0, 4, 6, 2, color);
	}
	if (map_get(pos + ix) != 255)
	{
		draw_face(pos, block, 1, 3, 7, 5, color);
	}
	if (map_get(pos - iy) != 255)
	{
		draw_face(pos, block, 0, 1, 5, 4, color);
	}
	if (map_get(pos + iy) != 255)
	{
		draw_face(pos, block, 2, 6, 7, 3, color);
	}
	if (map_get(pos - iz) != 255)
	{
		draw_face(pos, block, 0, 2, 3, 1, color);
	}
	if (map_get(pos + iz) != 255)
	{
		draw_face(pos, block, 4, 5, 7, 6, color);
	}
*/
	// prism faces
	if (block == 255 - 128 - 64)
	{
		draw_quad(pos, 2, 4, 5, 3, color);
	}
	if (block == 255 - 2 - 1)
	{
		draw_quad(pos, 4, 2, 3, 5, color);
	}
	if (block == 255 - 32 - 16)
	{
		draw_quad(pos, 1, 7, 6, 0, color);
	}
	if (block == 255 - 8 - 4)
	{
		draw_quad(pos, 7, 1, 0, 6, color);
	}
	if (block == 255 - 64 - 16)
	{
		draw_quad(pos, 0, 5, 7, 2, color);
	}
	if (block == 255 - 8 - 2)
	{
		draw_quad(pos, 5, 0, 2, 7, color);
	}
	if (block == 255 - 128 - 32)
	{
		draw_quad(pos, 3, 6, 4, 1, color);
	}
	if (block == 255 - 4 - 1)
	{
		draw_quad(pos, 6, 3, 1, 4, color);
	}
	if (block == 255 - 32 - 2)
	{
		draw_quad(pos, 0, 3, 7, 4, color);
	}
	if (block == 255 - 64 - 4)
	{
		draw_quad(pos, 3, 0, 4, 7, color);
	}
	if (block == 255 - 16 - 1)
	{
		draw_quad(pos, 5, 6, 2, 1, color);
	}
	if (block == 255 - 128 - 8)
	{
		draw_quad(pos, 6, 5, 1, 2, color);
	}

	// pyramid faces
	if (block == 23)
	{
		draw_triangle(pos, 1, 2, 4, color);
	}
	if (block == 43)
	{
		draw_triangle(pos, 0, 5, 3, color);
	}
	if (block == 13 + 64)
	{
		draw_triangle(pos, 3, 6, 0, color);
	}
	if (block == 8 + 4 + 2 + 128)
	{
		draw_triangle(pos, 2, 1, 7, color);
	}
	if (block == 16 + 32 + 64 + 1)
	{
		draw_triangle(pos, 5, 0, 6, color);
	}
	if (block == 32 + 16 + 128 + 2)
	{
		draw_triangle(pos, 4, 7, 1, color);
	}
	if (block == 64 + 128 + 16 + 4)
	{
		draw_triangle(pos, 7, 4, 2, color);
	}
	if (block == 128 + 64 + 32 + 8)
	{
		draw_triangle(pos, 6, 3, 5, color);
	}

	// anti-pyramid faces
	if (block == 254)
	{
		draw_triangle(pos, 1, 4, 2, color);
	}
	if (block == 253)
	{
		draw_triangle(pos, 0, 3, 5, color);
	}
	if (block == 251)
	{
		draw_triangle(pos, 3, 0, 6, color);
	}
	if (block == 247)
	{
		draw_triangle(pos, 2, 7, 1, color);
	}
	if (block == 255 - 16)
	{
		draw_triangle(pos, 5, 6, 0, color);
	}
	if (block == 255 - 32)
	{
		draw_triangle(pos, 4, 1, 7, color);
	}
	if (block == 255 - 64)
	{
		draw_triangle(pos, 7, 2, 4, color);
	}
	if (block == 127)
	{
		draw_triangle(pos, 6, 5, 3, color);
	}
}

#include <array>
std::array<glm::vec4, 4> frustum;

void InitFrustum(const glm::mat4& matrix)
{
	const float* clip = glm::value_ptr(matrix);
	// left
	frustum[0][0] = clip[ 3] - clip[ 0];
	frustum[0][1] = clip[ 7] - clip[ 4];
	frustum[0][2] = clip[11] - clip[ 8];
	frustum[0][3] = clip[15] - clip[12];
	// right
	frustum[1][0] = clip[ 3] + clip[ 0];
	frustum[1][1] = clip[ 7] + clip[ 4];
	frustum[1][2] = clip[11] + clip[ 8];
	frustum[1][3] = clip[15] + clip[12];
	// bottom
	frustum[2][0] = clip[ 3] + clip[ 1];
	frustum[2][1] = clip[ 7] + clip[ 5];
	frustum[2][2] = clip[11] + clip[ 9];
	frustum[2][3] = clip[15] + clip[13];
	// top
	frustum[3][0] = clip[ 3] - clip[ 1];
	frustum[3][1] = clip[ 7] - clip[ 5];
	frustum[3][2] = clip[11] - clip[ 9];
	frustum[3][3] = clip[15] - clip[13];

	for (int i = 0; i < 4; i++)
	{
		frustum[i] *= glm::fastInverseSqrt(sqr(frustum[i].xyz()));
	}
}

bool SphereCompletelyOutsideFrustum(glm::vec3 p, float radius)
{
	for (int i = 0; i < 4; i++)
	{
		if (glm::dot(p, frustum[i].xyz()) + frustum[i].w < -radius)
			return true;
	}
	return false;
}

float block_render_time_ms = 0;
float block_render_time_ms_avg = 0;

const float BlockRadius = sqrtf(3) / 2;

int buffer_budget = 0;

void buffer_chunk(Chunk& chunk)
{
	vertex_data = &chunk.vertices;
	for (int x = 0; x < ChunkSize; x++)
	{
		for (int y = 0; y < ChunkSize; y++)
		{
			for (int z = 0; z < ChunkSize; z++)
			{
				Block block = chunk.block[x][y][z];
				render_general(chunk.cpos * ChunkSize + glm::ivec3(x, y, z), block.shape, block.color);
			}
		}
	}
	vertex_data = nullptr;

	chunk.buffered = true;
	if (chunk.vertices.capacity() > chunk.vertices.size())
	{
		std::vector<Vertex> vertices(chunk.vertices);
		std::swap(vertices, chunk.vertices);
	}
}

bool render_buffered_chunk(Chunk& chunk)
{
	// Empty and underground chunks have no triangles to render
	if (chunk.vertices.size() == 0)
		return false;

	// TODO very cheap integer behind-the-camera check for chunk!
	/*if (glm::dot(direction, delta) < 0)
		return;*/

	glm::ivec3 pos = chunk.cpos * ChunkSize;
	glm::ivec3 p = pos + (ChunkSize / 2);
	if (SphereCompletelyOutsideFrustum(glm::vec3(p.x, p.y, p.z), ChunkSize * BlockRadius))
		return false;

	triangle_count += chunk.vertices.size() / 3;
	chunk_count += 1;
	glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * chunk.vertices.size(), &chunk.vertices[0], GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, chunk.vertices.size());
	return true;
}

glm::vec3 Q(glm::ivec3 a, glm::vec3 c)
{
	return (glm::vec3(a) - c) * 1.02f + c;
}

void render_world_blocks(const glm::mat4& matrix)
{
	float time_start = glfwGetTime();
	glBindTexture(GL_TEXTURE_2D, block_texture);
	glm::ivec3 cplayer = glm::ivec3(player_position) >> ChunkSizeBits;

	InitFrustum(matrix);

	float* ma = glm::value_ptr(player_orientation);
	glm::ivec3 direction(ma[4] * (1 << 20), ma[5] * (1 << 20), ma[6] * (1 << 20));

	if (wireframe)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	glUseProgram(block_program);

	glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform1i(block_sampler_loc, 0/*block_texture*/);
	glUniform3fv(block_eye_loc, 1, glm::value_ptr(player_position));

	glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
	glEnableVertexAttribArray(block_position_loc);
	glEnableVertexAttribArray(block_color_loc);
	glEnableVertexAttribArray(block_light_loc);
	glEnableVertexAttribArray(block_uv_loc);

	glVertexAttribIPointer(block_position_loc, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->pos);
	glVertexAttribIPointer(block_color_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->color);
	glVertexAttribIPointer(block_light_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->light);
	glVertexAttribIPointer(block_uv_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->uv);

	int buffer_budget = 5000;
	for (glm::ivec3 d : render_sphere)
	{
		Chunk& chunk = GetChunk(cplayer + d);
		if (!chunk.buffered)
		{
			buffer_chunk(chunk);
			if (--buffer_budget == 0) break;
		}
	}
	for (glm::ivec3 d : render_sphere)
	{
		Chunk& chunk = GetChunk(cplayer + d);
		if (chunk.buffered)
		{
			if (render_buffered_chunk(chunk))
				if (triangle_count > 4000000)
					break;
		}
	}

	glUseProgram(0);
	if (wireframe)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	block_render_time_ms = (glfwGetTime() - time_start) * 1000;
	block_render_time_ms_avg = block_render_time_ms_avg * 0.8f + block_render_time_ms * 0.2f;

	if (selection)
	{
		glUseProgram(line_program);
		glUniformMatrix4fv(line_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));

		glBindBuffer(GL_ARRAY_BUFFER, line_buffer);
		glEnableVertexAttribArray(line_position_loc);

		glVertexAttribPointer(line_position_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glm::vec3* vline = reinterpret_cast<glm::vec3*>(line_data);
		glm::vec3 c = glm::vec3(sel_cube) + 0.5f;
		for (int i = 0; i <= 1; i++)
		{
			for (int j = 0; j <= 1; j++)
			{
				glm::ivec3 sel = sel_cube;
				*vline++ = Q(sel + i*iy + j*iz, c); *vline++ = Q(sel + ix + i*iy + j*iz, c);
				*vline++ = Q(sel + i*ix + j*iz, c); *vline++ = Q(sel + iy + i*ix + j*iz, c);
				*vline++ = Q(sel + i*ix + j*iy, c); *vline++ = Q(sel + iz + i*ix + j*iy, c);
			}
		}

		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 3 * 24, line_data, GL_STREAM_DRAW);
		glDrawArrays(GL_LINES, 0, 24);

		glDisableVertexAttribArray(line_position_loc);
	}
}

void render_world()
{
	glm::mat4 matrix = glm::translate(perspective_rotation, -player_position);
	
	glEnable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	render_world_blocks(matrix);

	glDisable(GL_DEPTH_TEST);
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);
	
	// Text test
	text->Reset(height, matrix);
	text->Printf("position: %.1f %.1f %.1f, chunks: %d, triangles: %d, blocks: %.0fms, map: %.0fms",
			 player_position[0], player_position[1], player_position[2], chunk_count, triangle_count, block_render_time_ms_avg, map_refresh_time_ms);
	triangle_count = 0;
	chunk_count = 0;

	if (selection)
	{
		Block block = map_get_block(sel_cube);
		int red = block.color % 4;
		int green = (block.color / 4) % 4;
		int blue = block.color / 16;
		text->Printf("selected: cube [%d %d %d], shape %u, color [%u %u %u], face %d, distance %.1f", sel_cube.x, sel_cube.y, sel_cube.z, (uint)block.shape, red, green, blue, sel_face, sel_dist);
	}

	if (console_active)
	{
		for (const std::string& line : console_lines)
		{
			text->Printf("%.*s", line.size(), &line[0]);
		}
	}

	glUseProgram(0);

	if (selection)
	{
		glUseProgram(line_program);
		glUniformMatrix4fv(line_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));

		glBindBuffer(GL_ARRAY_BUFFER, line_buffer);
		glEnableVertexAttribArray(line_position_loc);

		glVertexAttribPointer(line_position_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);

		glm::vec2* vline = reinterpret_cast<glm::vec2*>(line_data);
		*vline++ = glm::vec2(width/2 - 15, height / 2);
		*vline++ = glm::vec2(width/2 + 15, height / 2);
		*vline++ = glm::vec2(width/2, height / 2 - 15);
		*vline++ = glm::vec2(width/2, height / 2 + 15);

		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 2 * 4, line_data, GL_STREAM_DRAW);
		glDrawArrays(GL_LINES, 0, 4);

		glDisableVertexAttribArray(line_position_loc);
	}

}

void render_frame()
{
	glViewport(0, 0, width, height);
	render_world();
	render_gui();
}

void OnError(int error, const char* message)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, message);
}

int main(int argc, char** argv)
{
	int a = -1, b = -7, c = -8;
	if (a >> 3 != -1 || b >> 3 != -1 || c >> 3 != -1)
	{
		std::cerr << "Shift test failed!" << std::endl;
	}

	if (!glfwInit())
	{
		return 0;
	}

	glfwSetErrorCallback(OnError);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode->width*2, mode->height*2, "Arena", glfwGetPrimaryMonitor(), NULL);
	if (!window)
	{
		glfwTerminate();
		return 0;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(VSYNC);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);

	model_init(window);
	render_init();
	render_general_init();

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	while (!glfwWindowShouldClose(window))
	{
		model_frame(window);
		render_frame();
		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwTerminate();
	return 0;
}
