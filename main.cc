// TODO:
// BUG - stray long distorted triangles
// BUG - crash during flying (writing to object after free())
// BUG - leaking occlusion culling query objects
// BUG - when changing orientation only recompute occlusion for chunks that are entering into frustum (unable to get it working)

// # large spherical world
// - print lat-long-alt coordinates
// - use quaternions for orientation + adjust UP vector using gravity
// - spherical terrain generation

// # PERF level-of-detail rendering
// - be able to simplify spherical surface
// - store low triangle buffers in chunks to render from distance
// - convert chunk to cube with low resolution generated texture?

// render blocks back to front
// - semi-transparent alpha color component (0-> 25%) (1->50%) (2->75%) (3->100%)
// - textures with transparent pixels

// client / server:
// - disk persistence on server
// - terrain generation on server
// - server sends: chunks, block updates and player position
// - client sends: edits, player commands

// multi-player:
// - render other players
// - login users
// - chat

// permanent server
// - where to host?
// - hostname / domain name?

// sky:
// - day/night cycle
// - static cloud voxels (+ transparency + marching cubes)
// - nice sun with lens flare

// # advanced editor:
// # - [partial] color and shape pallete as part of GUI
// # - free mouse (but fixed camera) mode for editing
// # - flood fill tool
// # - cut/copy/paste/move tool
// # - drawing shapes
// # - wiki / user change tracking

// # more support for slopes:
// # - integrate original marching cubes algo (will be usefull for water rendering!)
// # - [partial] hidden triangle elimination between two slopes
// # - [partial] slope generation
// # - collision detection with slopes
// # - light and shadows

// # water:
// # - animated surface
// # - simple water (minecraft) with darker light in depth (flood fill ocean!)

// # animated grass

// # shadows:
// # - real shadows from sun (no bounce)
// # - point lights with shadows (for caves)?

// # portals (there is nice youtube demo and open source project!)

// # more basic shapes: cones, cylinders (with level-of-detail!)

// # PERF multi-threaded terrain generation
// # PERF multi-threaded chunk array buffer construction
// # faster occlusion culling when player is moving
// # PERF manual occulsion culling - potentially visible set (for caves / mountains)

// # real world elevation data
// # translating blocks ?
// # psysics: moving objects / vehicles ?
// # texture mipmaps?
// # procedural textures

#include "util.hh"
#include "callstack.hh"
#include "rendering.hh"
#include <thread>
#include <mutex>

// Config

const int VSYNC = 1;

// GUI

int width;
int height;

// Util

static const glm::ivec3 ix(1, 0, 0), iy(0, 1, 0), iz(0, 0, 1);
static const glm::ivec3 Axis[3] = { ix, iy, iz };

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

bool Closer(glm::i8vec3 a, glm::i8vec3 b) { return glm::length2(glm::ivec3(a)) < glm::length2(glm::ivec3(b)); }

struct Sphere : public std::vector<glm::i8vec3>
{
	Sphere(int size)
	{
		glm::ivec3 d;
		for (d.x = -size; d.x <= size; d.x++)
		{
			for (d.y = -size; d.y <= size; d.y++)
			{
				for (d.z = -size; d.z < size; d.z++)
				{
					if (glm::dot(d, d) > 0 && glm::dot(d, d) <= size * size)
					{
						push_back(glm::i8vec3(d));
					}
				}
			}
		}
		std::sort(begin(), end(), Closer);
	}
};


// ============================

const int ChunkSizeBits = 4, MapSizeBits = 6;
const int ChunkSize = 1 << ChunkSizeBits, MapSize = 1 << MapSizeBits;
const int ChunkSizeMask = ChunkSize - 1, MapSizeMask = MapSize - 1;

static const int RenderDistance = 30;
Sphere render_sphere(RenderDistance);
static_assert(RenderDistance < MapSize / 2, "");

// ============================

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

struct MapChunk
{
	glm::ivec3 cpos;
	MapChunk* next;
	Block block[ChunkSize][ChunkSize][ChunkSize];

	void Set(int x, int y, int z, unsigned char shape, unsigned char color)
	{
		if (x >= 0 && x < ChunkSize && y >= 0 && y < ChunkSize && z >= 0 && z < ChunkSize)
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

const int CraterRadius = 500;
const glm::ivec3 CraterCenter(CraterRadius * -0.8, CraterRadius * -0.8, 0);

const int MoonRadius = 500;
const glm::ivec3 MoonCenter(MoonRadius * 0.8, MoonRadius * 0.8, 0);

void GenerateTerrain(glm::ivec3 cpos, MapChunk& chunk, bool)
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

	FOR(x, ChunkSize) FOR(y, ChunkSize)
	{
		int dx = x + cpos.x * ChunkSize;
		int dy = y + cpos.y * ChunkSize;

		if (heightmap->HasTree(dx, dy))
		{
			int height = heightmap->Height(dx, dy);
			FOR(z, ChunkSize)
			{
				int dz = z + cpos.z * ChunkSize;
				if (dz > height && dz < height + 6)
				{
					// trunk
					chunk.block[x][y][z] = { 255, 3*16 };
				}
			}
		}
		if (heightmap->HasTree(dx, dy-1))
		{
			int height = heightmap->Height(dx, dy-1);
			FOR(z, ChunkSize)
			{
				int dz = z + cpos.z * ChunkSize;
				if (dz > height + 2 && dz < height + 6)
				{
					chunk.block[x][y][z] = { 255, 3*4 };
				}
			}
		}
		if (heightmap->HasTree(dx, dy+1))
		{
			int height = heightmap->Height(dx, dy+1);
			FOR(z, ChunkSize)
			{
				int dz = z + cpos.z * ChunkSize;
				if (dz > height + 2 && dz < height + 6)
				{
					chunk.block[x][y][z] = { 255, 3*4 };
				}
			}
		}
		if (heightmap->HasTree(dx+1, dy))
		{
			int height = heightmap->Height(dx+1, dy);
			FOR(z, ChunkSize)
			{
				int dz = z + cpos.z * ChunkSize;
				if (dz > height + 2 && dz < height + 6)
				{
					chunk.block[x][y][z] = { 255, 3*4 };
				}
			}
		}
		if (heightmap->HasTree(dx-1, dy))
		{
			int height = heightmap->Height(dx-1, dy);
			FOR(z, ChunkSize)
			{
				int dz = z + cpos.z * ChunkSize;
				if (dz > height + 2 && dz < height + 6)
				{
					chunk.block[x][y][z] = { 255, 3*4 };
				}
			}
		}
	}

	// moon
	for (int x = 0; x < ChunkSize; x++)
	{
		int64_t sx = sqr<int64_t>(x + cpos.x * ChunkSize - MoonCenter.x) - sqr<int64_t>(MoonRadius);
		if (sx > 0) continue;

		for (int y = 0; y < ChunkSize; y++)
		{
			int64_t sy = sx + sqr<int64_t>(y + cpos.y * ChunkSize - MoonCenter.y);
			if (sy > 0) continue;

			for (int z = 0; z < ChunkSize; z++)
			{
				int64_t sz = sy + sqr<int64_t>(z + cpos.z * ChunkSize - MoonCenter.z);
				if (sz > 0) continue;

				chunk.block[x][y][z].shape = 255;
				chunk.block[x][y][z].color = 21*2;
			}
		}
	}

	// crater
	for (int x = 0; x < ChunkSize; x++)
	{
		int64_t sx = sqr<int64_t>(x + cpos.x * ChunkSize - CraterCenter.x) - sqr<int64_t>(CraterRadius);
		if (sx > 0) continue;

		for (int y = 0; y < ChunkSize; y++)
		{
			int64_t sy = sx + sqr<int64_t>(y + cpos.y * ChunkSize - CraterCenter.y);
			if (sy > 0) continue;

			for (int z = 0; z < ChunkSize; z++)
			{
				int64_t sz = sy + sqr<int64_t>(z + cpos.z * ChunkSize - CraterCenter.z);
				if (sz > 0) continue;

				chunk.block[x][y][z].shape = 0;
				chunk.block[x][y][z].color = 0;
			}
		}
	}
}

// ============================

#include <string>
const char* str(glm::ivec3 a)
{
	char buffer[100];
	snprintf(buffer, 100, "[%d %d %d]", a.x, a.y, a.z);
	int size = strlen(buffer);
	char* p = new char[size + 1];
	memcpy(p, buffer, size + 1);
	return p;
}
const char* str(glm::dvec3 a)
{
	char buffer[100];
	snprintf(buffer, sizeof(buffer), "[%lf %lf %lf]", a.x, a.y, a.z);
	int size = strlen(buffer);
	char* p = new char[size + 1];
	memcpy(buffer, p, size + 1);
	return p;
}

struct Map
{
	Map();

	MapChunk& Get(glm::ivec3 cpos)
	{
		int h = KeyHash()(cpos) % MapSize;
		MapChunk* head = m_map[h].load(std::memory_order_relaxed);
		for (MapChunk* i = head; i; i = i->next)
		{
			if (i->cpos == cpos) return *i;
		}
		MapChunk* p = new MapChunk();
		p->cpos = cpos;
		GenerateTerrain(cpos, *p, false);
		//m_lock[std::abs(cpos.x ^ cpos.y ^ cpos.z) % 64].lock();
		while (true)
		{
			p->next = m_map[h];
			for (MapChunk* i = p->next; i != head; i = i->next)
			{
				if (i->cpos == cpos)
				{
					//m_lock[std::abs(cpos.x ^ cpos.y ^ cpos.z) % 64].unlock();
					delete p;
					return *i;
				}
			}
			head = p->next;
			//m_map[h].store(p, std::memory_order_relaxed);
			//break;
			if(m_map[h].compare_exchange_strong(p->next, p, std::memory_order_release, std::memory_order_relaxed)) break;
		}
		//m_lock[std::abs(cpos.x ^ cpos.y ^ cpos.z) % 64].unlock();
		return *p;
	}

	void Set(glm::ivec3 pos, Block block)
	{
		Get(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask] = block;
	}

	void Print()
	{
		int m = 0, c = 0, nz = 0;
		for (int i = 0; i < MapSize; i++)
		{
			int n = 0;
			for (MapChunk* p = m_map[i]; p; p=p->next)
			{
				n += 1;
			}
			if (n != 0) nz += 1;
			if (n > 1)
			{
				fprintf(stderr, "chain\n");
				for (MapChunk* p = m_map[i]; p; p=p->next)
				{
					fprintf(stderr, "%s\n", str(p->cpos));
				}
			}
			c += n;
			m = std::max(m, n);
		}
		fprintf(stderr, "map hash table: size %d, chains %d, max chain %d, average chain %lf\n", c, nz, m, (double)c / nz);
	}
	
private:
	struct KeyHash
	{
		size_t operator()(glm::ivec3 a) const { a &= 127; return (a.x * 128 * 128) + (a.y * 128) + (a.z); }
	};

	static const int MapSize = 128 * 128 * 128;
	std::atomic<MapChunk*> m_map[MapSize];
	//std::mutex m_lock[64];
	
	glm::ivec3 m_cpos;

	void Set(glm::ivec3 pos, unsigned char shape, glm::vec3 color)
	{
		Block block;
		block.shape = shape;
		block.color = ColorToCode(color);
		Set(pos, block);
	}
};

int S(int a) { return 1 << a; }

Map::Map() : m_cpos(0x80000000, 0, 0)
{
	FOR(i, MapSize) m_map[i] = nullptr;
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

struct Cube
{
	static glm::ivec3 corner[8];
	static const int faces[6][4];
	static Plucker face_edges[6][4];

	static void Init()
	{
		FOR(i, 8) corner[i] = glm::ivec3(i&1, (i>>1)&1, (i>>2)&1);
		FOR(f, 6) FOR(c, 4) face_edges[f][c] = Plucker::points(glm::dvec3(corner[faces[f][c]]), glm::dvec3(corner[faces[f][(c+1)%4]]));
	}
};

glm::ivec3 Cube::corner[8];
const int Cube::faces[6][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };
Plucker Cube::face_edges[6][4];

// ============================

struct Vertex
{
	GLubyte color;
	GLubyte light;
	GLubyte uv;
	GLubyte pos[3];
};

struct VisibleChunks
{
	std::vector<glm::i8vec3>::iterator it; // are visible chunks fully computed or partially?
	std::vector<glm::i8vec3> chunks;
	bool dirty_plus; // list might contain some unnecessary chunk
	bool dirty_minus; // list might be missing some chunk

	VisibleChunks(): it(render_sphere.begin()), dirty_plus(false), dirty_minus(false) { }

	void clear()
	{
		it = render_sphere.begin();
		release(chunks);
		dirty_plus = false;
		dirty_minus = false;
	}
};


struct Chunk
{
	Block block[ChunkSize][ChunkSize][ChunkSize];
	glm::ivec3 cpos;
	bool buffered;
	std::vector<Vertex> vertices;
	std::vector<glm::ivec4> planes; // Overflow? // TODO: compress as normals are 5bits
	GLuint query;

	bool obstructor;
	VisibleChunks visible;

	void clear()
	{
		buffered = false;
		release(vertices);
		release(planes);
		visible.clear();
	}

	int init(glm::ivec3 _cpos)
	{
		if (cpos == _cpos) return 0;
		cpos = _cpos;
		MapChunk& mc = map->Get(cpos);
		memcpy(block, mc.block, sizeof(mc.block));
		clear();

		obstructor = true;
		FOR(z, 2)
		{
			z *= ChunkSize - 1;
			FOR(x, ChunkSize) FOR(y, ChunkSize)
			{
				// TODO: check only the outside face! it doesn't have to be cube!
				if (block[x][y][z].shape != 255 || block[x][z][y].shape != 255 || block[z][x][y].shape != 255) { obstructor = false; break; }
			}
		}
		return 1;
	}

	void init_planes()
	{
		assert(buffered);
		FOR(i, vertices.size() / 3)
		{
			glm::ivec3 a = unpack(i * 3);
			glm::ivec3 b = unpack(i * 3 + 1);
			glm::ivec3 c = unpack(i * 3 + 2);

			glm::ivec3 normal = glm::cross(c - b, a - b);
			bool xy = (normal.x == 0 || normal.y == 0 || abs(normal.x) == abs(normal.y));
			bool xz = (normal.x == 0 || normal.z == 0 || abs(normal.x) == abs(normal.z));
			bool yz = (normal.y == 0 || normal.z == 0 || abs(normal.y) == abs(normal.z));
			if (!xy || !xz || !yz)
			{
				fprintf(stderr, "normal %s, a %s, b %s, c %s\n", str(normal), str(a), str(b), str(c));
				assert(false);
			}
			normal /= glm::max(abs(normal.x), abs(normal.y), abs(normal.z));
			int w = glm::dot(normal, a + (cpos * ChunkSize)); // Overflow?

			glm::ivec4 plane(normal, w);
			if (!contains(planes, plane)) planes.push_back(plane);
		}
		//compress(planes);
	}

	glm::ivec3 unpack(int i)
	{
		auto pos = vertices[i].pos;
		return glm::ivec3(pos[0], pos[1], pos[2]);
	}

	// at least one of the points
	bool contains_face_visible_from(const glm::ivec3 point[], int count)
	{
		assert(buffered);
		switch (count)
		{
		case 1:
			for(auto p : planes) if (glm::dot(point[0], p.xyz()) >= p.w) return true;
			return false;
		case 2:
			for(auto p : planes) if (glm::dot(point[0], p.xyz()) >= p.w || glm::dot(point[1], p.xyz()) >= p.w) return true;
			return false;
		case 4:
			for(auto p : planes) if (glm::dot(point[0], p.xyz()) >= p.w || glm::dot(point[1], p.xyz()) >= p.w || glm::dot(point[2], p.xyz()) >= p.w || glm::dot(point[3], p.xyz()) >= p.w) return true;
			return false;
		}
		assert(false);
		return false;
	}
	
	bool contains_face_visible_from_chunk(glm::ivec3 cpos2)
	{
		assert(buffered);
		if (vertices.size() == 0) return false;

		assert(cpos != cpos2);
		glm::ivec3 d = cpos - cpos2;
		std::ivector<glm::ivec3, 8> points;
		FOR(x, 2) FOR(y, 2) FOR(z, 2)
		{
			unless(d.x == 0 || (x == 0 && d.x < 0) || (x == 1 && d.x > 0)) continue;
			unless(d.y == 0 || (y == 0 && d.y < 0) || (y == 1 && d.y > 0)) continue;
			unless(d.z == 0 || (z == 0 && d.z < 0) || (z == 1 && d.z > 0)) continue;
			points.push_back((cpos2 << ChunkSize) + glm::ivec3(x, y, z) * ChunkSize);
		}
		return contains_face_visible_from(points.begin(), points.size());
	}
	
	Chunk() : buffered(false), obstructor(false), cpos(0x80000000, 0, 0), query(-1) { }
};

Console console;

#include <functional>
#include <atomic>
class Chunks
{
public:
	Chunks(): m_last_cpos(0x80000000, 0, 0), m_map(new Chunk[MapSize * MapSize * MapSize]) { }

	Chunk& operator()(glm::ivec3 cpos)
	{
		Chunk& chunk = get(cpos);
		if (chunk.cpos != cpos) { fprintf(stderr, "cpos = [%d %d %d] %lg\n", cpos.x, cpos.y, cpos.z, sqrt(glm::length2(cpos))); PrintCallStack(); assert(false); }
		return chunk;
	}

	static void refresh_worker(Chunks* chunks, glm::ivec3 cpos, int k, int n)
	{
		//float time_start = glfwGetTime();
		for (int i = k; i < render_sphere.size(); i += n)
		{
			glm::ivec3 c = cpos + glm::ivec3(render_sphere[i]);
			chunks->get(c).init(c);
		}
		//float time = (glfwGetTime() - time_start) * 1000;
		//fprintf(stderr, "refresh worker %d = %lf\n", k, time);
	}
	
	bool loaded(glm::ivec3 cpos) { return get(cpos).cpos == cpos; }

	// load new chunks as player moves
	bool refresh(glm::ivec3 cpos)
	{
		if (cpos == m_last_cpos) return false;
		m_last_cpos = cpos;
		// PERF: if cpos is next to m_last_cpos, then only iterate chunks on sphere boundary!

		fprintf(stderr, "refresh begin\n");
		float time_start = glfwGetTime();

		const int N = 7;
		std::thread workers[N];
		FOR(i, N)
		{
			std::thread w(refresh_worker, this, cpos, i, N+1);
			std::swap(w, workers[i]);
		}
		get(cpos).init(cpos);
		refresh_worker(this, cpos, N, N+1);
		FOR(i, N) workers[i].join();
		
		map_refresh_time_ms = (glfwGetTime() - time_start) * 1000;
		//map->Print();
		//fprintf(stderr, "refresh end %lf\n", map_refresh_time_ms);

		/*for (auto d : render_sphere)
		{
			glm::ivec3 c = cpos + glm::ivec3(d);
			assert(get(c).cpos == c);
		}
		assert(get(cpos).cpos == cpos);*/
		return true;
	}
	
	glm::ivec3 last_cpos() { return m_last_cpos; }

private:
	Chunk& get(glm::ivec3 a)
	{
		a &= MapSizeMask;
		assert(0 <= a.x && a.x < MapSize);
		assert(0 <= a.y && a.y < MapSize);
		assert(0 <= a.z && a.z < MapSize);
		return m_map[((a.x * MapSize) + a.y) * MapSize + a.z];
	}

private:
	glm::ivec3 m_last_cpos;
	Chunk* m_map; // Huge array in memory!
public:
	float map_refresh_time_ms;
};

Chunks g_chunks;

Block& map_get_block(glm::ivec3 pos)
{
	return g_chunks(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask];
}

unsigned char map_get(glm::ivec3 pos)
{
	if (g_chunks.loaded(pos >> ChunkSizeBits)) return map_get_block(pos).shape;
	MapChunk& mc = map->Get(pos >> ChunkSizeBits);
	return mc.block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask].shape;
}

unsigned char map_get_color(glm::ivec3 pos)
{
	if (g_chunks.loaded(pos >> ChunkSizeBits)) return map_get_block(pos).color;
	MapChunk& mc = map->Get(pos >> ChunkSizeBits);
	return mc.block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask].color;
}

double distance2_point_and_line(glm::dvec3 point, glm::dvec3 orig, glm::dvec3 dir)
{
	assert(is_unit_length(dir));
	return glm::distance2(point, orig + dir * glm::dot(point - orig, dir));
}

class LineOfSight
{
public:
	int chunk_visible_fast(glm::ivec3 a, glm::ivec3 d);

private:
	BitCube<RenderDistance + 1> m_marked;
	std::vector<glm::ivec3> m_stack;
};

template<int size>
bool intersects_line_polygon(Plucker line, const Plucker edges[size])
{
	double a = line_crossing(line, edges[0]);
	for (auto e = edges + 1; e != edges + size; e++)
	{
		if (opposite_sign_strict(a, line_crossing(line, *e))) return false;
	}
	return true;
}

bool intersects_line_cube(Plucker line)
{
	for (auto face : Cube::face_edges) if (intersects_line_polygon<4>(line, face)) return true;
	return false;
}

bool intersects_line_cube(glm::dvec3 orig, glm::dvec3 dir, glm::ivec3 cube)
{
	assert(is_unit_length(dir));
	orig -= glm::dvec3(cube);
	glm::dvec3 cube_center(0.5, 0.5, 0.5);
	double pos = glm::dot(cube_center - orig, dir);
	double dist2 = glm::distance2(cube_center, orig + dir * pos);
	return dist2 < 0.25 || (dist2 < 0.75 && intersects_line_cube(Plucker::orig_dir(orig, dir)));
}

bool intersects_line_cubes(glm::dvec3 orig, glm::dvec3 dir, std::vector<glm::ivec4>& cubes)
{
	for (int i = 0; i < cubes.size(); i++)
	{
		if (intersects_line_cube(orig, dir, cubes[i].xyz()))
		{
			if (i > 0) std::swap(cubes[i], cubes[i - 1]);
			return true;
		}
	}
	return false;
}

// Is there any empty block in chunk A from which at least one face from chunk A+D is visible?
int LineOfSight::chunk_visible_fast(glm::ivec3 a, glm::ivec3 d)
{
	std::ivector<glm::ivec3, 3> moves;
	FOR(i, 3) if (d[i] < 0) moves.push_back(Axis[i]); else if (d[i] > 0) moves.push_back(-Axis[i]);
	
	glm::dvec3 orig = glm::dvec3(d << ChunkSizeBits) + ChunkSize * 0.5;
	glm::dvec3 dir = glm::normalize(-orig + ChunkSize * 0.5); 
	double D2 = sqr(ChunkSize + ChunkSize)*0.75;

	m_marked.clear();		
	m_stack.clear();
	m_stack.push_back(d);	
	while (!m_stack.empty())
	{
		glm::ivec3 p = m_stack.back();
		m_stack.pop_back();
		for (glm::ivec3 m : moves)
		{
			glm::ivec3 q = p + m;
			if (q == glm::ivec3(0, 0, 0)) return 1;
			if (!between(d, q, glm::ivec3(0, 0, 0))) continue;
			if (m_marked[glm::abs(q)]) continue;
			/*DEBUG*/ if (!g_chunks.loaded(a + q)) { fprintf(stderr, "WARNING! a=%s q=%s d=%s last_cpos=%s\n", str(a), str(q), str(d), str(g_chunks.last_cpos())); return -1; }
			if (g_chunks(a + q).obstructor) continue;
			if (distance2_point_and_line(glm::dvec3(q) + glm::dvec3(0.5, 0.5, 0.5), orig, dir) > D2) continue;
			m_stack.push_back(q);
			m_marked.set(glm::abs(q));
		}
	}
	return 0;
}

namespace stats
{
	int triangle_count = 0;
	int chunk_count = 0;

	float frame_time_ms = 0;
	float block_render_time_ms = 0;
	float model_time_ms = 0;
	float buffer_time_ms = 0;
	float pvc_time_ms = 0;
}

// Model

namespace player
{
//	glm::vec3 position = glm::vec3(MoonCenter) + glm::vec3(0, 0, MoonRadius + 10);
	glm::vec3 position(0, 0, 20);
	float yaw = 0;
	float pitch = 0;
	glm::mat4 orientation;
	glm::vec3 velocity;
	bool flying = true;
}

float last_time;

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
float sel_dist;
bool selection = false;

bool wireframe = false;
bool occlusion = true;
int pvs = 2;

const int slope_shapes[]        = { 63, 252, 207, 243, 175, 245, 95, 250, 221, 187, 238, 119 };
const int pyramid_shapes[]      = { 23, 43, 77, 142, 113, 178, 212, 232 };
const int anti_pyramid_shapes[] = { 254, 253, 251, 247, 239, 223, 191, 127 };

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

std::vector<GLuint> free_queries;

void ReleaseChunkQueries(glm::vec3 p)
{
	glm::ivec3 cplayer = glm::ivec3(glm::floor(p)) >> ChunkSizeBits;
	// TODO: should go across entire cached set of chunks, not just sphere!
	// TODO: use versioning to avoid O(RenderDistance^3) clearing?
	for (glm::i8vec3 d : render_sphere)
	{
		Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
		if (chunk.query != (GLuint)(-1))
		{
			free_queries.push_back(chunk.query);
			chunk.query = -1;
		}
	}
}

void EditBlock(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	glm::ivec3 p(pos.x & ChunkSizeMask, pos.y & ChunkSizeMask, pos.z & ChunkSizeMask);

	Block prev_block = map_get_block(sel_cube);
	map->Set(pos, block);
	map_get_block(pos) = block;
	g_chunks(cpos).clear();

	if (p.x == 0) g_chunks(cpos - ix).clear();
	if (p.y == 0) g_chunks(cpos - iy).clear();
	if (p.z == 0) g_chunks(cpos - iz).clear();
	if (p.x == ChunkSizeMask) g_chunks(cpos + ix).clear();
	if (p.y == ChunkSizeMask) g_chunks(cpos + iy).clear();
	if (p.z == ChunkSizeMask) g_chunks(cpos + iz).clear();

	if (block.shape != prev_block.shape && block.shape != 255)
	{
		ReleaseChunkQueries(player::position);
	}
}

bool mode = false;

void OnEditKey(int key)
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

void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	if (console.IsVisible())
	{
		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			if (console.OnKey(key, mods))
			{
				// handled by console
			}
			else if (key == GLFW_KEY_F2)
			{
				console.Hide();
			}
		}
	}
	else if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_F2)
		{
			console.Show();
		}
		else if (key == GLFW_KEY_F3)
		{
			wireframe = !wireframe;
		}
		else if (key == GLFW_KEY_F4)
		{
			occlusion = !occlusion;
		}
		else if (key == GLFW_KEY_F5)
		{
			pvs = (pvs + 1) % 3;
		}
		else if (key == GLFW_KEY_TAB)
		{
			player::flying = !player::flying;
			player::velocity = glm::vec3(0, 0, 0);
		}
		else if (key == GLFW_KEY_F1)
		{
			mode = !mode;
			glm::ivec3 pos(glm::floor(player::position));
			glm::ivec3 cpos = pos >> ChunkSizeBits;
			for (glm::i8vec3 d : render_sphere)
			{
				g_chunks(cpos + glm::ivec3(d)).clear();
			}
		}
		else if (selection)
		{
			OnEditKey(key);
		}
	}
	else if (action == GLFW_REPEAT)
	{
		if (selection)
		{
			OnEditKey(key);
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

	g_chunks.refresh(glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits);
	
	InitColorCodes();
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

void ResolveCollisionsWithBlocks()
{
	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	FOR(i, 100)
	{
		// Resolve all collisions simultaneously
		glm::vec3 sum(0, 0, 0);
		int c = 0;
		FOR2(x, p.x - 3, p.x + 3) FOR2(y, p.y - 3, p.y + 3) FOR2(z, p.z - 3, p.z + 3)
		{
			glm::ivec3 cube(x, y, z);
			if (map_get(cube) != 0 && 1 == SphereVsCube(player::position, 0.8f, cube, CubeNeighbors(cube)))
			{
				sum += player::position;
				c += 1;
			}
		}
		if (c == 0) break;
		player::velocity = glm::vec3(0, 0, 0);
		player::position = sum / (float)c;
	}
}

glm::vec3 Gravity(glm::vec3 pos)
{
	return glm::vec3(0, 0, -15);

	glm::vec3 dir = glm::vec3(MoonCenter) - pos;
	double dist2 = glm::dot(dir, dir);
	double a = 10000000;

	if (dist2 > MoonRadius * MoonRadius)
	{
		return dir * (float)(a / (sqrt(dist2) * dist2));
	}
	else
	{
		return dir * (float)(a / (MoonRadius * MoonRadius * MoonRadius));
	}
}

void Simulate(float dt, glm::vec3 dir)
{
	if (!player::flying)
	{
		player::velocity += Gravity(player::position) * dt;
	}
	player::position += dir * ((player::flying ? 10 : 4) * dt) + player::velocity * dt;
	ResolveCollisionsWithBlocks();
}

bool new_orientation = true;

void model_move_player(GLFWwindow* window, float dt)
{
	glm::vec3 dir(0, 0, 0);

	float* m = glm::value_ptr(player::orientation);
	glm::vec3 forward(m[4], m[5], m[6]);
	glm::vec3 right(m[0], m[1], m[2]);

	if (!player::flying)
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

	if (!player::flying && glfwGetKey(window, GLFW_KEY_SPACE))
	{
		player::velocity = Gravity(player::position) * -0.5f;
	}

	glm::vec3 p = player::position;
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
	if (p != player::position)
	{
		ReleaseChunkQueries(p);
		new_orientation = false;
		tracelog << "position = [" << player::position.x << " " << player::position.y << " " << player::position.z << "]" << std::endl;
	}
}

bool IntersectRayTriangle(glm::vec3 orig, glm::vec3 dir, glm::ivec3 cube, float& dist, int triangle[3])
{
	glm::vec3 a = glm::vec3(cube + Cube::corner[triangle[0]]);
	glm::vec3 b = glm::vec3(cube + Cube::corner[triangle[1]]);
	glm::vec3 c = glm::vec3(cube + Cube::corner[triangle[2]]);
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
	FOR(i, 12)
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
	float* ma = glm::value_ptr(player::orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);
	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	bool found = false;
	int Dist = 6;
	// TODO: use Sphere class here to avoid unnecessary Intersect calls!
	FOR2(x, p.x - Dist, p.x + Dist) FOR2(y, p.y - Dist, p.y + Dist) FOR2(z, p.z - Dist, p.z + Dist)
	{
		glm::ivec3 i(x, y, z);
		float dist; int face;
		if (map_get(i) != 0 && IntersectRayCube(player::position, dir, i, /*out*/dist, /*out*/face) && dist > 0 && dist <= Dist && (!found || dist < sel_dist))
		{
			found = true;
			sel_cube = i;
			sel_dist = dist;
			sel_face = face;
		}
	}
	return found;
}

auto buffer_it = render_sphere.begin();

void model_frame(GLFWwindow* window)
{
	double time = glfwGetTime();
	double dt = (time - last_time) < 0.5 ? (time - last_time) : 0.5;
	last_time = time;

	double cursor_x, cursor_y;
	glfwGetCursorPos(window, &cursor_x, &cursor_y);
	static double last_cursor_x = 0;
	static double last_cursor_y = 0;
	if (cursor_x != last_cursor_x || cursor_y != last_cursor_y)
	{
		player::yaw += (cursor_x - last_cursor_x) / 150;
		player::pitch += (cursor_y - last_cursor_y) / 150;
		if (player::pitch > M_PI / 2) player::pitch = M_PI / 2;
		if (player::pitch < -M_PI / 2) player::pitch = -M_PI / 2;
		//glfwSetCursorPos(window, 0, 0);
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
		player::orientation = glm::rotate(glm::rotate(glm::mat4(), -player::yaw, glm::vec3(0, 0, 1)), -player::pitch, glm::vec3(1, 0, 0));

		float* m = glm::value_ptr(player::orientation);
		glm::vec3 forward(m[4], m[5], m[6]);
		glm::vec3 right(m[0], m[1], m[2]);
		tracelog << "orientation = [" << m[0] << " " << m[1] << " " << m[2] << ", " << m[4] << " " << m[5] << " " << m[6] << ", " << m[8] << " " << m[9] << " " << m[10] << "]" << std::endl;

		perspective_rotation = glm::rotate(perspective, player::pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, player::yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));

		new_orientation = true;
	}
	
	if (!console.IsVisible())
	{
		model_move_player(window, dt);
	}

	if (g_chunks.refresh(glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits))
	{
		buffer_it = render_sphere.begin();
	}

	selection = SelectCube(/*out*/sel_cube, /*out*/sel_dist, /*out*/sel_face);
	double end = std::max<float>(0, (glfwGetTime() - time) * 1000);
	stats::model_time_ms = stats::model_time_ms * 0.75f + end * 0.25f;
}

// Render

const int MaxTriangles = 4000000;

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
				glm::vec3 normal = glm::normalize((glm::vec3)glm::cross(glm::vec3(Cube::corner[b] - Cube::corner[a]), glm::vec3(Cube::corner[c] - Cube::corner[a])));
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

const int OcclusionQueryCount = 32 * 32 * 32;
GLuint occlusion_query[OcclusionQueryCount];

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

GLuint GetFreeQuery()
{
	if (free_queries.size() == 0)
	{
		tracelog << "Panic: Out of free queries!" << std::endl;
		exit(1);
	}
	GLuint query = free_queries[free_queries.size() - 1];
	if (query == (GLuint)-1)
	{
		tracelog << "Panic: query == -1" << std::endl;
		exit(1);
	}
	free_queries.pop_back();
	return query;
}

void render_init()
{
	printf("OpenGL version: [%s]\n", glGetString(GL_VERSION));
	glEnable(GL_CULL_FACE);

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

	glGenQueries(OcclusionQueryCount, occlusion_query);
	for (int i = 0; i < OcclusionQueryCount; i++)
	{
		free_queries.push_back(occlusion_query[i]);
	}

	block_program = load_program("block");
	block_matrix_loc = glGetUniformLocation(block_program, "matrix");
	block_sampler_loc = glGetUniformLocation(block_program, "sampler");
	block_pos_loc = glGetUniformLocation(block_program, "pos");
	block_eye_loc = glGetUniformLocation(block_program, "eye");
	block_position_loc = glGetAttribLocation(block_program, "position");
	block_color_loc = glGetAttribLocation(block_program, "color");
	block_light_loc = glGetAttribLocation(block_program, "light");
	block_uv_loc = glGetAttribLocation(block_program, "uv");
}

GLuint block_buffer;
GLuint line_buffer;

GLfloat* line_data = new GLfloat[24 * 3];

void render_general_init()
{
	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &line_buffer);
}

void vertex(std::vector<Vertex>& vertices, glm::ivec3 pos, GLubyte color, GLubyte light, GLubyte uv, int c)
{
	Vertex v;
	v.color = color;
	v.light = light;
	v.uv = uv;
	v.pos[0] = (pos.x & ChunkSizeMask) + Cube::corner[c].x;
	v.pos[1] = (pos.y & ChunkSizeMask) + Cube::corner[c].y;
	v.pos[2] = (pos.z & ChunkSizeMask) + Cube::corner[c].z;
	vertices.push_back(v);
}

void draw_quad(std::vector<Vertex>& vertices, glm::ivec3 pos, int a, int b, int c, int d, GLubyte color)
{
	GLubyte light = light_cache[a][b][c];
	vertex(vertices, pos, color, light, 0, a);
	vertex(vertices, pos, color, light, 1, b);
	vertex(vertices, pos, color, light, 3, c);
	vertex(vertices, pos, color, light, 0, a);
	vertex(vertices, pos, color, light, 3, c);
	vertex(vertices, pos, color, light, 2, d);
}

void draw_triangle(std::vector<Vertex>& vertices, glm::ivec3 pos, int a, int b, int c, GLubyte color)
{
	GLubyte light = light_cache[a][b][c];
	vertex(vertices, pos, color, light, 0, a);
	vertex(vertices, pos, color, light, 1, b);
	vertex(vertices, pos, color, light, 3, c);
}

int B(int a, int i) { return (a & (1 << i)) >> i; }

void draw_face(std::vector<Vertex>& vertices, glm::ivec3 pos, int block, int a, int b, int c, int d, GLubyte color)
{
	int n = B(block, a) + B(block, b) + B(block, c) + B(block, d);

	if (n == 4)
	{
		draw_quad(vertices, pos, a, b, c, d, color);
	}
	else if (n == 3)
	{
		GLubyte light = light_cache[a][b][c];
		if (block & (1 << a)) vertex(vertices, pos, color, light, 0, a);
		if (block & (1 << b)) vertex(vertices, pos, color, light, 1, b);
		if (block & (1 << c)) vertex(vertices, pos, color, light, 3, c);
		if (block & (1 << d)) vertex(vertices, pos, color, light, 2, d);
	}
}

void render_block(std::vector<Vertex>& vertices, glm::ivec3 pos, GLubyte color)
{
	if (map_get(pos - ix) != 255)
	{
		draw_quad(vertices, pos, 0, 4, 6, 2, color);
	}
	if (map_get(pos + ix) != 255)
	{
		draw_quad(vertices, pos, 1, 3, 7, 5, color);
	}
	if (map_get(pos - iy) != 255)
	{
		draw_quad(vertices, pos, 0, 1, 5, 4, color);
	}
	if (map_get(pos + iy) != 255)
	{
		draw_quad(vertices, pos, 2, 6, 7, 3, color);
	}
	if (map_get(pos - iz) != 255)
	{
		draw_quad(vertices, pos, 0, 2, 3, 1, color);
	}
	if (map_get(pos + iz) != 255)
	{
		draw_quad(vertices, pos, 4, 5, 7, 6, color);
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
void render_general(std::vector<Vertex>& vertices, glm::ivec3 pos, int block, GLubyte color)
{
	if (block == 0)
	{
		return;
	}
	if (block == 255)
	{
		render_block(vertices, pos, color);
		return;
	}

	// common faces
	if (mode) // TODO: looks unfinished, check if it works!
	{
		if (MaxX(map_get(pos - ix)) != MinX(block)) // TODO: could be more strict: chekc if subset
		{
			draw_face(vertices, pos, block, 0, 4, 6, 2, color);
		}
		if (MinX(map_get(pos + ix)) != MaxX(block))
		{
			draw_face(vertices, pos, block, 1, 3, 7, 5, color);
		}
	}
	else
	{
		if (map_get(pos - ix) != 255)
		{
			draw_face(vertices, pos, block, 0, 4, 6, 2, color);
		}
		if (map_get(pos + ix) != 255)
		{
			draw_face(vertices, pos, block, 1, 3, 7, 5, color);
		}
	}
	if (map_get(pos - iy) != 255)
	{
		draw_face(vertices, pos, block, 0, 1, 5, 4, color);
	}
	if (map_get(pos + iy) != 255)
	{
		draw_face(vertices, pos, block, 2, 6, 7, 3, color);
	}
	if (map_get(pos - iz) != 255)
	{
		draw_face(vertices, pos, block, 0, 2, 3, 1, color);
	}
	if (map_get(pos + iz) != 255)
	{
		draw_face(vertices, pos, block, 4, 5, 7, 6, color);
	}

	// prism faces
	if (block == 255 - 128 - 64)
	{
		draw_quad(vertices, pos, 2, 4, 5, 3, color);
	}
	if (block == 255 - 2 - 1)
	{
		draw_quad(vertices, pos, 4, 2, 3, 5, color);
	}
	if (block == 255 - 32 - 16)
	{
		draw_quad(vertices, pos, 1, 7, 6, 0, color);
	}
	if (block == 255 - 8 - 4)
	{
		draw_quad(vertices, pos, 7, 1, 0, 6, color);
	}
	if (block == 255 - 64 - 16)
	{
		draw_quad(vertices, pos, 0, 5, 7, 2, color);
	}
	if (block == 255 - 8 - 2)
	{
		draw_quad(vertices, pos, 5, 0, 2, 7, color);
	}
	if (block == 255 - 128 - 32)
	{
		draw_quad(vertices, pos, 3, 6, 4, 1, color);
	}
	if (block == 255 - 4 - 1)
	{
		draw_quad(vertices, pos, 6, 3, 1, 4, color);
	}
	if (block == 255 - 32 - 2)
	{
		draw_quad(vertices, pos, 0, 3, 7, 4, color);
	}
	if (block == 255 - 64 - 4)
	{
		draw_quad(vertices, pos, 3, 0, 4, 7, color);
	}
	if (block == 255 - 16 - 1)
	{
		draw_quad(vertices, pos, 5, 6, 2, 1, color);
	}
	if (block == 255 - 128 - 8)
	{
		draw_quad(vertices, pos, 6, 5, 1, 2, color);
	}

	// pyramid faces
	if (block == 23)
	{
		draw_triangle(vertices, pos, 1, 2, 4, color);
	}
	if (block == 43)
	{
		draw_triangle(vertices, pos, 0, 5, 3, color);
	}
	if (block == 13 + 64)
	{
		draw_triangle(vertices, pos, 3, 6, 0, color);
	}
	if (block == 8 + 4 + 2 + 128)
	{
		draw_triangle(vertices, pos, 2, 1, 7, color);
	}
	if (block == 16 + 32 + 64 + 1)
	{
		draw_triangle(vertices, pos, 5, 0, 6, color);
	}
	if (block == 32 + 16 + 128 + 2)
	{
		draw_triangle(vertices, pos, 4, 7, 1, color);
	}
	if (block == 64 + 128 + 16 + 4)
	{
		draw_triangle(vertices, pos, 7, 4, 2, color);
	}
	if (block == 128 + 64 + 32 + 8)
	{
		draw_triangle(vertices, pos, 6, 3, 5, color);
	}

	// anti-pyramid faces
	if (block == 254)
	{
		draw_triangle(vertices, pos, 1, 4, 2, color);
	}
	if (block == 253)
	{
		draw_triangle(vertices, pos, 0, 3, 5, color);
	}
	if (block == 251)
	{
		draw_triangle(vertices, pos, 3, 0, 6, color);
	}
	if (block == 247)
	{
		draw_triangle(vertices, pos, 2, 7, 1, color);
	}
	if (block == 255 - 16)
	{
		draw_triangle(vertices, pos, 5, 6, 0, color);
	}
	if (block == 255 - 32)
	{
		draw_triangle(vertices, pos, 4, 1, 7, color);
	}
	if (block == 255 - 64)
	{
		draw_triangle(vertices, pos, 7, 2, 4, color);
	}
	if (block == 127)
	{
		draw_triangle(vertices, pos, 6, 5, 3, color);
	}
}

#include <array>

struct Frustum
{
	std::array<glm::vec4, 4> frustum;
	void Init(const glm::mat4& matrix);
	bool IsSphereCompletelyOutside(glm::vec3 p, float radius) const;
};

void Frustum::Init(const glm::mat4& matrix)
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

bool Frustum::IsSphereCompletelyOutside(glm::vec3 p, float radius) const
{
	FOR(i, 4) if (glm::dot(p, frustum[i].xyz()) + frustum[i].w < -radius) return true;
	return false;
}

const float BlockRadius = sqrtf(3) / 2;

int buffer_budget = 0;

std::vector<Vertex> g_buffer_chunk(6 * ChunkSize * ChunkSize * ChunkSize);
int64_t chunks_buffered = 0;

void buffer_chunk(Chunk& chunk)
{
	g_buffer_chunk.clear();
	FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
	{
		Block block = chunk.block[x][y][z];
		render_general(g_buffer_chunk, chunk.cpos * ChunkSize + glm::ivec3(x, y, z), block.shape, block.color);
	}

	chunks_buffered += 1;
	chunk.vertices.resize(g_buffer_chunk.size());
	std::copy(g_buffer_chunk.begin(), g_buffer_chunk.end(), chunk.vertices.begin());
	assert(chunk.vertices.size() == chunk.vertices.capacity());
	
	chunk.buffered = true;
	chunk.init_planes();
}

bool chunk_renderable(Chunk& chunk, const Frustum& frustum)
{
	if (!chunk.buffered)
		return false;

	// Empty and underground chunks have no triangles to render
	if (chunk.vertices.size() == 0)
		return false;

	glm::ivec3 p = chunk.cpos * ChunkSize + (ChunkSize / 2);
	return !frustum.IsSphereCompletelyOutside(glm::vec3(p), ChunkSize * BlockRadius);
}

void render_chunk(Chunk& chunk)
{
	stats::triangle_count += chunk.vertices.size();
	stats::chunk_count += 1;

	glm::ivec3 pos = chunk.cpos * ChunkSize;
	glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * chunk.vertices.size(), &chunk.vertices[0], GL_STREAM_DRAW);

	glDrawArrays(GL_TRIANGLES, 0, chunk.vertices.size());
}

GLuint GetQueryResult(GLuint query)
{
	GLuint result = 0;
	glGetQueryObjectuiv(query, GL_QUERY_RESULT, &result);
	return result;
}

LineOfSight losa;
int64_t pvcs_computed = 0;

int precompute_potentially_visible_chunks(Chunk& chunk, int budget)
{
	if (chunk.visible.it == render_sphere.end()) return budget;
	if (chunk.obstructor)
	{
		chunk.visible.it = render_sphere.end();
		return budget;
	}
	do
	{
		glm::ivec3 b = chunk.cpos + glm::ivec3(*chunk.visible.it);
		if (!g_chunks.loaded(b) || !g_chunks(b).buffered) break;

		if (g_chunks(b).contains_face_visible_from_chunk(chunk.cpos))
		{
			int ret = losa.chunk_visible_fast(chunk.cpos, b - chunk.cpos);
			if (ret == 1) chunk.visible.chunks.push_back(*chunk.visible.it); else if (ret == -1) break;
			pvcs_computed += 1;
			if (--budget <= 0) return 0;
		}		
		chunk.visible.it++;
	}
	while (chunk.visible.it != render_sphere.end());
	return budget;
}

void precompute_potentially_visible_chunks(glm::ivec3 cplayer, int budget=4000)
{
	budget = precompute_potentially_visible_chunks(g_chunks(cplayer), budget);
	if (budget <= 0) return;

	if (pvs != 2) return;
	for (glm::i8vec3 d : render_sphere)
	{
		if (!g_chunks.loaded(cplayer + glm::ivec3(d))) break;
		Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
		if (!chunk.buffered) continue;
		budget = precompute_potentially_visible_chunks(chunk, budget);
		if (budget <= 0) return;
	}
}

void render_world_blocks(glm::ivec3 cplayer, const glm::mat4& matrix, const Frustum& frustum)
{
	float time_start = glfwGetTime();
	glBindTexture(GL_TEXTURE_2D, block_texture);

	float* ma = glm::value_ptr(player::orientation);
	glm::ivec3 direction(ma[4] * (1 << 20), ma[5] * (1 << 20), ma[6] * (1 << 20));

	if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glUseProgram(block_program);
	glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform1i(block_sampler_loc, 0/*block_texture*/);
	glUniform3fv(block_eye_loc, 1, glm::value_ptr(player::position));

	glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
	glEnableVertexAttribArray(block_position_loc);
	glEnableVertexAttribArray(block_color_loc);
	glEnableVertexAttribArray(block_light_loc);
	glEnableVertexAttribArray(block_uv_loc);

	glVertexAttribIPointer(block_position_loc, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->pos);
	glVertexAttribIPointer(block_color_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->color);
	glVertexAttribIPointer(block_light_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->light);
	glVertexAttribIPointer(block_uv_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->uv);

	// Center chunk is special
	Chunk& chunk0 = g_chunks(cplayer);
	render_chunk(chunk0);

	static Frustum last_frustum;

	std::vector<glm::i8vec3>& filter = pvs ? chunk0.visible.chunks : render_sphere;
	if (occlusion == 0)
	{
		for (glm::i8vec3 d : filter)
		{
			Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
			if (chunk_renderable(chunk, frustum))
			{
				render_chunk(chunk);
				if (stats::triangle_count > MaxTriangles * 3)
					break;
			}
		}
	}
	else
	{
		for (glm::i8vec3 d : filter)
		{
			Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
			if (!chunk_renderable(chunk, frustum))
				continue;

			if (chunk.query == (GLuint)(-1))
			{
				chunk.query = GetFreeQuery();
				glBeginQuery(GL_SAMPLES_PASSED, chunk.query);
				render_chunk(chunk);
				glEndQuery(GL_SAMPLES_PASSED);
				if (stats::triangle_count > MaxTriangles * 3)
					break;
			}
			else
			{
				GLuint samples = GetQueryResult(chunk.query);

				if (samples > 0)
				{
					render_chunk(chunk);
					if (stats::triangle_count > MaxTriangles * 3)
						break;
				}
				// BUG: when changing orientation no need to recompute everything, just chunks that are new from last_frustum
				else if (new_orientation && samples == 0 /*&& !chunk_renderable(chunk, last_frustum)*/)
				{
					glBeginQuery(GL_SAMPLES_PASSED, chunk.query);
					render_chunk(chunk);
					glEndQuery(GL_SAMPLES_PASSED);
					if (stats::triangle_count > MaxTriangles * 3)
						break;
				}
			}
		}
	}

	new_orientation = false;
	last_frustum = frustum;

	glUseProgram(0);
	if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	float time_ms = (glfwGetTime() - time_start) * 1000;
	stats::block_render_time_ms = stats::block_render_time_ms * 0.75f + time_ms * 0.25f;
}

glm::vec3 Q(glm::ivec3 a, glm::vec3 c)
{
	return (glm::vec3(a) - c) * 1.02f + c;
}

void render_block_selection(const glm::mat4& matrix)
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

bool buffer_chunks(glm::ivec3 cplayer, int budget = 500)
{
	Chunk& center_chunk = g_chunks(cplayer);
	if (!center_chunk.buffered)
	{
		buffer_chunk(center_chunk);
		if (--budget <= 0) return false;
	}
	while (buffer_it != render_sphere.end())
	{
		glm::i8vec3 d = *buffer_it;
		if (!g_chunks.loaded(cplayer + glm::ivec3(d))) break;
		Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
		if (!chunk.buffered)
		{
			buffer_chunk(chunk);
			if (--budget <= 0) return false;
		}
		buffer_it++;
	}
	return true;
}

void render_world()
{
	// TODO - avoid recomputing if same
	glm::mat4 matrix = glm::translate(perspective_rotation, -player::position);
	Frustum frustum;
	frustum.Init(matrix);

	glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
	
	float time_start = glfwGetTime();
	buffer_chunks(cplayer);
	float time = std::max<float>(0, (glfwGetTime() - time_start) * 1000);
	stats::buffer_time_ms = stats::buffer_time_ms * 0.75f + time * 0.25f;
	
	if (pvs > 0)
	{
		float time_start = glfwGetTime();
		precompute_potentially_visible_chunks(cplayer);
		float time = std::max<float>(0, (glfwGetTime() - time_start) * 1000);
		stats::pvc_time_ms = stats::pvc_time_ms * 0.75f + time * 0.25f;
	}
	else
	{
		stats::pvc_time_ms = 0;
	}
	
	glEnable(GL_DEPTH_TEST);
	render_world_blocks(cplayer, matrix, frustum);
	if (selection) render_block_selection(matrix);
	glDisable(GL_DEPTH_TEST);
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);

	text->Reset(width, height, matrix);
	text->Printf("[%.1f %.1f %.1f] C:%4d T:%3dk map:%2.0f frame:%2.1f model:%1.1f buffer:%2.1f_%lu, pvs:%2.1f_%luk render %2.1f",
			 player::position.x, player::position.y, player::position.z, stats::chunk_count, stats::triangle_count / 3000,
			 g_chunks.map_refresh_time_ms, stats::frame_time_ms, stats::model_time_ms, stats::buffer_time_ms, chunks_buffered / 1000, stats::pvc_time_ms, pvcs_computed / 1000, stats::block_render_time_ms);
	stats::triangle_count = 0;
	stats::chunk_count = 0;

	if (selection)
	{
		Block block = map_get_block(sel_cube);
		int red = block.color % 4;
		int green = (block.color / 4) % 4;
		int blue = block.color / 16;
		text->Printf("selected: cube [%d %d %d], shape %u, color [%u %u %u], face %d, distance %.1f", sel_cube.x, sel_cube.y, sel_cube.z, (uint)block.shape, red, green, blue, sel_face, sel_dist);
	}
	else
	{
		text->Print("", 0);
	}

	console.Render(text, last_time);

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

void OnError(int error, const char* message)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, message);
}

void refresh_worker(int k, int n)
{
	
}

int main(int argc, char** argv)
{
	Cube::Init();
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
	
	glClearColor(0.2, 0.4, 1, 1.0);
	float frame_start = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		float frame_end = glfwGetTime();
		stats::frame_time_ms = stats::frame_time_ms * 0.75f + (frame_end - frame_start) * 1000 * 0.25f;
		frame_start = frame_end;

		model_frame(window);

		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		render_world();
		render_gui();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwTerminate();
	return 0;
}
