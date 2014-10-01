// TODO:
// BUG - stray long distorted triangles
// BUG - PVC incorrectly filters some chunks in crater boundary
// - slowly blend-in newly buffered chunks so that they are less noticable

// # more proceduraly generated stuff!
// - smooth 3d tunnels
// - diffuse/reduce process

// # eye-candy
// - grass!
// - iregular grid (just a bit) - affects collision testing

// # async computation
// - pvc computation in separate threads
// - more agressive pvc!

// # large spherical world
// - print lat-long-alt coordinates
// - use quaternions for orientation + adjust UP vector using gravity
// - spherical terrain generation

// # PERF level-of-detail rendering
// - use lower resultion buffers (ie. less triangles) of chunks for rendering in the distance
// - convert chunk to cube with low resolution generated texture?

// # render blocks back to front
// - semi-transparent alpha color component (0-> 25%) (1->50%) (2->75%) (3->100%)
// - need to re-sort triangles inside chunk buffer (for nearby chunks only) as player moves
// - textures with transparent pixels

// # client / server:
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
// - static cloud voxels (+ transparency)
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

// # shadows:
// # - real shadows from sun (no bounce)
// # - point lights with shadows (for caves)?

// # portals (there is nice youtube demo and open source project!)

// # more basic shapes: cones, cylinders (with level-of-detail!)

// # PERF multi-threaded chunk array buffer construction

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
	FOR(i, octaves - 1)
	{
		freq *= freqf;
		amp *= ampf;
		max += amp;
		total += (turbulent ? fabs(glm::simplex(p * freq)) : glm::simplex(p * freq)) * amp;
	}
	return total / max;
}

float SimplexNoise(glm::vec3 p, int octaves, float freqf, float ampf, bool turbulent)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = turbulent ? fabs(glm::simplex(p)) : glm::simplex(p);
	FOR(i, octaves - 1)
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
		FOR2(x, -size, size) FOR2(y, -size, size) FOR2(z, -size, size)
		{
			glm::ivec3 d(x, y, z);
			if (glm::dot(d, d) > 0 && glm::dot(d, d) <= size * size)
			{
				push_back(glm::i8vec3(d));
			}
		}
		std::sort(begin(), end(), Closer);
	}
};


// ============================

const int ChunkSizeBits = 4, MapSizeBits = 7;
const int ChunkSize = 1 << ChunkSizeBits, MapSize = 1 << MapSizeBits;
const int ChunkSizeMask = ChunkSize - 1, MapSizeMask = MapSize - 1;

static const int RenderDistance = 40;
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
	FOR(r, 4) FOR(g, 4) FOR(b, 4) color_codes[r + g * 4 + b * 16] = glm::vec3(r / 3.0f, g / 3.0f, b / 3.0f);
}

unsigned char ColorToCode(glm::vec3 color)
{
	int code = 0;
	float err = sqr(color_codes[code] - color);
	FOR2(i, 1, 63)
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

struct Cube
{
	static glm::ivec3 corner[8];
	static const int faces[6][4];

	Cube()
	{
		FOR(i, 8) corner[i] = glm::ivec3(i&1, (i>>1)&1, (i>>2)&1);
	}
};

glm::ivec3 Cube::corner[8];
const int Cube::faces[6][4] = { { 0, 4, 6, 2 }/*xmin*/, { 1, 3, 7, 5 }/*xmax*/, { 0, 1, 5, 4 }/*ymin*/, { 2, 6, 7, 3 }/*ymax*/, { 0, 2, 3, 1 }/*zmin*/, { 4, 5, 7, 6 }/*zmax*/ };

Cube x_dummy;

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
		FOR(x, ChunkSize) FOR(y, ChunkSize) last[x][y] = glm::ivec2(1000000, 1000000);
	}

	void Populate(int cx, int cy);
	int& Height(int x, int y) { return height[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
	bool& HasTree(int x, int y) { return hasTree[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
};

int GetHeight(int x, int y)
{
	float q = SimplexNoise(glm::vec2(x, y) * 0.004f, 6, 0.5f, 0.5f, true);
	return q * q * q * q * 200;
}

int GetColor(int x, int y)
{
	return floorf((1 + SimplexNoise(glm::vec2(x, y) * -0.024f, 6, 0.5f, 0.5f, false)) * 8);
}

float Tree(int x, int y)
{
	return SimplexNoise(glm::vec2(x+321398, y+8901) * 0.005f, 4, 2.0f, 0.5f, true);
}

bool GetHasTree(int x, int y)
{
	float a = Tree(x, y);
	FOR2(xx, -1, 1) FOR2(yy, -1, 1)
	{
		if ((xx != 0 || yy != 0) && a <= Tree(x + xx, y + yy))
			return false;
	}
	return true;
}

void Heightmap::Populate(int cx, int cy)
{
	if (last[cx & MapSizeMask][cy & MapSizeMask] != glm::ivec2(cx, cy))
	{
		FOR(x, ChunkSize) FOR(y, ChunkSize)
		{
			Height(x + cx * ChunkSize, y + cy * ChunkSize) = GetHeight(x + cx * ChunkSize, y + cy * ChunkSize);
			HasTree(x + cx * ChunkSize, y + cy * ChunkSize) = GetHasTree(x + cx * ChunkSize, y + cy * ChunkSize);
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

uint8_t shape_class[256];

void GenerateTerrain(glm::ivec3 cpos, MapChunk& chunk)
{
	memset(chunk.block, 0, sizeof(Block) * ChunkSize * ChunkSize * ChunkSize);

	heightmap->Populate(cpos.x, cpos.y);
	heightmap->Populate(cpos.x-1, cpos.y);
	heightmap->Populate(cpos.x+1, cpos.y);
	heightmap->Populate(cpos.x, cpos.y-1);
	heightmap->Populate(cpos.x, cpos.y+1);
	
	FOR(x, ChunkSize) FOR(y, ChunkSize)
	{
		int dx = x + cpos.x * ChunkSize;
		int dy = y + cpos.y * ChunkSize;

		int height = heightmap->Height(dx, dy);
		FOR(z, ChunkSize)
		{
			int dz = z + cpos.z * ChunkSize;
			glm::ivec3 pos(dx, dy, dz);
			if (dz <= height)
			{
				// ground and caves
				chunk.block[x][y][z].color = GetColor(dx, dy);
				if (height - dz <= 200)
				{
					int shape = 0;
					FOR(i, 8)
					{
						double q = SimplexNoise(glm::vec3(pos + Cube::corner[i]) * 0.03f, 4, 0.5f, 0.5f, false);
						if (q >= -0.2) shape |= 1 << i;
					}
					if (shape_class[shape] == 255) shape = 0;
					chunk.block[x][y][z].shape = shape;
				}
				else
				{
					chunk.block[x][y][z].shape = 255;
				}
			}
			else if (dz > 100 && dz < 200)
			{
				// clouds
				int shape = 0;
				FOR(i, 8)
				{
					double q = SimplexNoise(glm::vec3(pos + Cube::corner[i]) * 0.03f, 4, 0.5f, 0.5f, false);
					if (q < -0.3) shape |= 1 << i;
				}
				if (shape_class[shape] == 255) shape = 0;
				chunk.block[x][y][z].shape = shape;
				chunk.block[x][y][z].color = 1 + 4*2 + 16*3;
			}
		}

		// slope on top
		int z = height + 1 - cpos.z * ChunkSize;
		if (false && z >= 0 && z < ChunkSize)
		{
			if (z > 0 && chunk.block[x][y][z-1].shape == 0) break;
			
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
	FOR(x, ChunkSize)
	{
		int64_t sx = sqr<int64_t>(x + cpos.x * ChunkSize - MoonCenter.x) - sqr<int64_t>(MoonRadius);
		if (sx > 0) continue;

		FOR(y, ChunkSize)
		{
			int64_t sy = sx + sqr<int64_t>(y + cpos.y * ChunkSize - MoonCenter.y);
			if (sy > 0) continue;

			FOR(z, ChunkSize)
			{
				int64_t sz = sy + sqr<int64_t>(z + cpos.z * ChunkSize - MoonCenter.z);
				if (sz > 0) continue;

				chunk.block[x][y][z].shape = 255;
				chunk.block[x][y][z].color = 21*2;
			}
		}
	}

	// crater
	FOR(x, ChunkSize)
	{
		int64_t sx = sqr<int64_t>(x + cpos.x * ChunkSize - CraterCenter.x) - sqr<int64_t>(CraterRadius);
		if (sx > 0) continue;

		FOR(y, ChunkSize)
		{
			int64_t sy = sx + sqr<int64_t>(y + cpos.y * ChunkSize - CraterCenter.y);
			if (sy > 0) continue;

			FOR(z, ChunkSize)
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
const char* str(glm::vec3 a)
{
	char buffer[100];
	snprintf(buffer, sizeof(buffer), "[%f %f %f]", a.x, a.y, a.z);
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
		GenerateTerrain(cpos, *p);
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
		FOR(i, MapSize)
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

// ============================

int S(int a) { return 1 << a; }

// Block tools:
// 1) cube 8
// 2) prism 6
// 3) tetra 4
// 4) xtetra 7
// 5) pyramid 5 (quarter pyramid)
// 6) screw 6 (cube - two tetrahedrons)

const int prism_shapes[]   = { 63, 252, 207, 243, 175, 245, 95, 250, 221, 187, 238, 119 };
const int tetra_shapes[]   = { 23, 43, 77, 142, 113, 178, 212, 232 };
const int xtetra_shapes[]  = { 254, 253, 251, 247, 239, 223, 191, 127 };
const int pyramid_shapes[] = { 31, 47, 79, 143, 241, 242, 244, 248 }; // TODO: only third of all pyramids
const int screw_shapes[]   = { 159, 111, 249, 246 }; // TODO: only half

void init_shape_class();

Map::Map() : m_cpos(0x80000000, 0, 0)
{
	FOR(i, MapSize) m_map[i] = nullptr;
	InitColorCodes();
	init_shape_class();

	// sphere test
	FOR2(x, -1, 1) FOR2(y, -1, 1) FOR2(z, -1, 1)
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

	// all colors
	FOR(x, 4) FOR(y, 4) FOR(z, 4) Set(glm::ivec3(x*2, y*2, z*2+45), 255, glm::vec3(x*0.3333f, y*0.3333f, z*0.3333f));

	// all blocks test
	FOR(i, 12) Set(glm::ivec3(0, 0, 10 + i * 2), prism_shapes[i], glm::vec3(1, 0.66, 0.33));
	FOR(i, 8) Set(glm::ivec3(-2, 0, 10 + i * 2), tetra_shapes[i], glm::vec3(0.66, 0.33, 1));
	FOR(i, 8) Set(glm::ivec3(-4, 0, 10 + i * 2), xtetra_shapes[i], glm::vec3(1, 0, 1));
	FOR(i, 8) Set(glm::ivec3(-6, 0, 10 + i * 2), pyramid_shapes[i], glm::vec3(0, 1, 1));
	FOR(i, 4) Set(glm::ivec3(-8, 0, 10 + i * 2), screw_shapes[i], glm::vec3(1, 1, 0));
}

// ============================

Map* map = new Map;

Block& map_get_block(glm::ivec3 pos)
{
	return map->Get(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask];
}

unsigned char map_get(glm::ivec3 pos) { return map_get_block(pos).shape; }
unsigned char map_get_color(glm::ivec3 pos) { return map_get_block(pos).color; }

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

int MinX(int a) { return a & 85; }
int MaxX(int a) { return (a >> 1) & 85; }

int MinY(int a) { return a & 51; }
int MaxY(int a) { return (a >> 2) & 51; }

int MinZ(int a) { return a & 15; }
int MaxZ(int a) { return (a >> 4) & 15; }

struct BlockRenderer
{
	std::vector<Vertex> m_vertices;
	glm::ivec3 m_pos;
	GLubyte m_color;

	void vertex(GLubyte light, GLubyte uv, int c);
	void draw_quad(int a, int b, int c, int d);
	void draw_triangle(int a, int b, int c);
	void draw_face(int block, int a, int b, int c, int d);
	void render(glm::ivec3 pos, Block bs);

	glm::ivec3 unpack(int i)
	{
		auto pos = m_vertices[i].pos;
		return glm::ivec3(pos[0], pos[1], pos[2]);
	}
};

struct Chunk
{
	std::mutex mutex;
	glm::ivec3 cpos; // TODO -> should be atomic -> pack into uint64 -> 21 bit per coordinate -> 25 effective
	std::vector<Vertex> vertices;
	std::vector<glm::ivec4> planes; // Overflow? // TODO: compress as normals are 5bits
	GLuint query;
	int query_version;

	bool obstructor;
	VisibleChunks visible;

	void buffer(BlockRenderer& renderer)
	{
		renderer.m_vertices.clear();
		MapChunk& mc = map->Get(cpos);
		FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
		{
			renderer.render(cpos * ChunkSize + glm::ivec3(x, y, z), mc.block[x][y][z]);
		}
	
		vertices.resize(renderer.m_vertices.size());
		std::copy(renderer.m_vertices.begin(), renderer.m_vertices.end(), vertices.begin());
		assert(vertices.size() == vertices.capacity());
	}

	bool is_obstructor()
	{
		MapChunk& mc = map->Get(cpos);
		FOR(a, ChunkSize) FOR(b, ChunkSize)
		{
			if (MinX(mc.block[0][a][b].shape) != MinX(255) || MaxX(mc.block[ChunkSize-1][a][b].shape) != MaxX(255)) return false;
			if (MinY(mc.block[a][0][b].shape) != MinY(255) || MaxX(mc.block[a][ChunkSize-1][b].shape) != MaxY(255)) return false;
			if (MinZ(mc.block[a][b][0].shape) != MinZ(255) || MaxX(mc.block[a][b][ChunkSize-1].shape) != MaxZ(255)) return false;
		}
		return true;
	}
	
	void init(glm::ivec3 _cpos, BlockRenderer& renderer)
	{
		mutex.lock();
		if (cpos == _cpos) { mutex.unlock(); return; }
		cpos = _cpos;
		release(vertices);
		release(planes);
		visible.clear();
		buffer(renderer);
		init_planes();
		obstructor = is_obstructor();
		mutex.unlock();
	}

	void reset(BlockRenderer& renderer)
	{
		mutex.lock();
		release(vertices);
		release(planes);
		visible.clear();
		buffer(renderer);
		init_planes();
		obstructor = is_obstructor();
		mutex.unlock();
	}
	
	void init_planes()
	{
		FOR(i, vertices.size() / 3)
		{
			glm::ivec3 a = unpack(i * 3);
			glm::ivec3 b = unpack(i * 3 + 1);
			glm::ivec3 c = unpack(i * 3 + 2);

			glm::ivec3 normal = glm::cross(c - b, a - b);
			bool xy = (normal.x == 0 || normal.y == 0 || abs(normal.x) == abs(normal.y));
			bool xz = (normal.x == 0 || normal.z == 0 || abs(normal.x) == abs(normal.z));
			bool yz = (normal.y == 0 || normal.z == 0 || abs(normal.y) == abs(normal.z));
			if (!xy || !xz || !yz || (normal.x == 0 && normal.y == 0 && normal.z == 0))
			{
				fprintf(stderr, "normal %s, a %s, b %s, c %s\n", str(normal), str(a), str(b), str(c));
				assert(false);
			}
			normal /= glm::max(abs(normal.x), abs(normal.y), abs(normal.z));
			int w = glm::dot(normal, a + (cpos * ChunkSize)); // Overflow?

			glm::ivec4 plane(normal, w);
			if (!contains(planes, plane)) planes.push_back(plane);
		}
		compress(planes);
	}

	glm::ivec3 unpack(int i)
	{
		auto pos = vertices[i].pos;
		return glm::ivec3(pos[0], pos[1], pos[2]);
	}

	// at least one of the points
	bool contains_face_visible_from(const glm::ivec3 point[], int count)
	{
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
	
	Chunk() : obstructor(false), cpos(0x80000000, 0, 0), query(-1), query_version(-1) { }
};

Console console;

namespace player
{
//	glm::vec3 position = glm::vec3(MoonCenter) + glm::vec3(0, 0, MoonRadius + 10);
	glm::vec3 position(0, 0, 20);
	std::atomic<int> cposx(0), cposy(0), cposz(1); // TODO: use formula!
	float yaw = 0;
	float pitch = 0;
	glm::mat4 orientation;
	glm::vec3 velocity;
	bool flying = true;
}

#include <functional>
#include <atomic>
#include "unistd.h"
class Chunks
{
public:
	static const int Threads = 8;

	Chunks(): m_map(new Chunk[MapSize * MapSize * MapSize]) { }

	Chunk& operator()(glm::ivec3 cpos)
	{
		Chunk& chunk = get(cpos);
		if (chunk.cpos != cpos) { fprintf(stderr, "cpos = [%d %d %d] %lg\n", cpos.x, cpos.y, cpos.z, sqrt(glm::length2(cpos))); PrintCallStack(); assert(false); }
		return chunk;
	}

	void start_threads()
	{
		FOR(i, Threads)
		{
			std::thread t(loader_thread, this, i);
			t.detach();
		}
	}
	
	static void loader_thread(Chunks* chunks, int k)
	{
		glm::ivec3 last_cpos(0x80000000, 0, 0);
		BlockRenderer renderer;
		while (true)
		{
			int x = player::cposx;
			int y = player::cposy;
			int z = player::cposz;
			glm::ivec3 cpos(x, y, z);
			if (cpos == last_cpos)
			{
				usleep(200);
				continue;
			}

			if (owner(cpos) == k) chunks->get(cpos).init(cpos, renderer);
			for (int i = 0; i < render_sphere.size(); i++)
			{
				glm::ivec3 c = cpos + glm::ivec3(render_sphere[i]);
				if (owner(c) == k) chunks->get(c).init(c, renderer);
			}
			last_cpos = cpos;
		}
	}
	
	static int owner(glm::ivec3 cpos)
	{
		return static_cast<uint32_t>(cpos.x ^ cpos.y ^ cpos.z) % Threads;
	}
	
	// TODO: add trylock to prevent it from being unloaded
	bool loaded(glm::ivec3 cpos) { return get(cpos).cpos == cpos; }

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
	Chunk* m_map; // Huge array in memory!
};

Chunks g_chunks;

// ======================

float distance2_point_and_line(glm::vec3 point, glm::vec3 orig, glm::vec3 dir)
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
	FOR(i, size) if (line_crossing(line, edges[i]) > 0) return false;
	return true;
}

// Is there any empty block in chunk A from which at least one face from chunk A+D is visible?
int LineOfSight::chunk_visible_fast(glm::ivec3 a, glm::ivec3 d)
{
	std::ivector<glm::ivec3, 3> moves;
	FOR(i, 3) if (d[i] < 0) moves.push_back(Axis[i]); else if (d[i] > 0) moves.push_back(-Axis[i]);
	
	glm::vec3 orig = glm::vec3(d << ChunkSizeBits) + ChunkSize * 0.5f;
	glm::vec3 dir = glm::normalize(-orig + ChunkSize * 0.5f); 
	float D2 = sqr(ChunkSize + ChunkSize) * 0.75f;

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
			if (!g_chunks.loaded(a + q)) return -1;
			if (g_chunks(a + q).obstructor) continue;
			if (distance2_point_and_line(glm::vec3(q) + glm::vec3(0.5, 0.5, 0.5), orig, dir) > D2) continue;
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
	float pvc_time_ms = 0;
}

// Model

float last_time;

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
bool selection = false;

bool wireframe = false;
bool occlusion = false;
int pvs = 0;
int block_tool = 0;
const int block_tool_shape[] = { 255, 63, 23, 254, 31, 159};
const char* block_tool_name[] = { "cube", "prism", "tetra", "xtetra", "pyramid", "screw"};

void init_shape_class()
{
	memset(shape_class, 255, sizeof(shape_class));
	shape_class[255] = 0;
	FOR(i, 12) shape_class[prism_shapes[i]]  = 1;
	FOR(i, 8) shape_class[tetra_shapes[i]]   = 2;
	FOR(i, 8) shape_class[xtetra_shapes[i]]  = 3;
	FOR(i, 8) shape_class[pyramid_shapes[i]] = 4;
	FOR(i, 4) shape_class[screw_shapes[i]]   = 5;
}

int NextShape(int shape, int dir)
{
	if (shape == 255) return 255;
	FOR(i, 12) if (shape == prism_shapes[i]) return prism_shapes[(i + dir + 12) % 12];
	FOR(i, 8) if (shape == tetra_shapes[i]) return tetra_shapes[(i + dir + 8) % 8];
	FOR(i, 8) if (shape == xtetra_shapes[i]) return xtetra_shapes[(i + dir + 8) % 8];
	FOR(i, 8) if (shape == pyramid_shapes[i]) return pyramid_shapes[(i + dir + 8) % 8];
	FOR(i, 4) if (shape == screw_shapes[i]) return screw_shapes[(i + dir + 4) % 4];
	assert(false);
	return -1;
}

std::vector<GLuint> free_queries;
int current_query_version = 0;
BlockRenderer g_renderer;

void EditBlock(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	glm::ivec3 p(pos.x & ChunkSizeMask, pos.y & ChunkSizeMask, pos.z & ChunkSizeMask);

	Block prev_block = map_get_block(sel_cube);
	map->Set(pos, block);
	g_chunks(cpos).reset(g_renderer);

	if (p.x == 0) g_chunks(cpos - ix).reset(g_renderer);
	if (p.y == 0) g_chunks(cpos - iy).reset(g_renderer);
	if (p.z == 0) g_chunks(cpos - iz).reset(g_renderer);
	if (p.x == ChunkSizeMask) g_chunks(cpos + ix).reset(g_renderer);
	if (p.y == ChunkSizeMask) g_chunks(cpos + iy).reset(g_renderer);
	if (p.z == ChunkSizeMask) g_chunks(cpos + iz).reset(g_renderer);

	if (block.shape != prev_block.shape && block.shape != 255)
	{
		current_query_version += 1;
	}
	// TODO: recompute pvc of surrounding chunks
}

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
		block.shape = NextShape(block.shape, 1);
		EditBlock(sel_cube, block);
	}
	if (key == GLFW_KEY_B)
	{
		block.shape = NextShape(block.shape, -1);
		EditBlock(sel_cube, block);
	}
}

bool show_counters = true;

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
		if (key == GLFW_KEY_1)
		{
			block_tool = 0;
		}
		else if (key == GLFW_KEY_2)
		{
			block_tool = 1;
		}
		else if (key == GLFW_KEY_3)
		{
			block_tool = 2;
		}
		else if (key == GLFW_KEY_4)
		{
			block_tool = 3;
		}
		else if (key == GLFW_KEY_5)
		{
			block_tool = 4;
		}
		else if (key == GLFW_KEY_6)
		{
			block_tool = 5;
		}
		else if (key == GLFW_KEY_F2)
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
		else if (key == GLFW_KEY_F6)
		{
			show_counters = !show_counters;
		}
		else if (key == GLFW_KEY_TAB)
		{
			player::flying = !player::flying;
			player::velocity = glm::vec3(0, 0, 0);
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
		Block block;
		block.shape = block_tool_shape[block_tool];
		block.color = map_get_block(sel_cube).color;
		EditBlock(sel_cube + dir[sel_face], block);
	}
}

void model_init(GLFWwindow* window)
{
	glfwSetKeyCallback(window, OnKey);
	glfwSetMouseButtonCallback(window, OnMouseButton);

	last_time = glfwGetTime();

	glfwSetCursorPos(window, 0, 0);
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
	FOR2(dx, -1, 1) FOR2(dy, -1, 1) FOR2(dz, -1, 1)
	{
		if (map_get(glm::ivec3(cube.x + dx, cube.y + dy, cube.z + dz)) != 0)
		{
			neighbors |= NeighborBit(dx, dy, dz);
		}
	}
	return neighbors;
}

void ResolveCollisionsWithBlocks()
{
	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	FOR(i, 2)
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
	player::position += dir * ((player::flying ? 15 : 6) * dt) + player::velocity * dt;
	ResolveCollisionsWithBlocks();
}

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
		current_query_version += 1;
	}
}

float intersect_line_plane(glm::vec3 orig, glm::vec3 dir, glm::vec4 plane)
{
	assert(is_unit_length(dir));
	assert(is_unit_length(plane.xyz()));
	return (-plane.w - glm::dot(orig, plane.xyz())) / glm::dot(dir, plane.xyz());
}

const float BlockRadius = sqrtf(3) / 2;

// Find cube face that contains closest point of intersection with the ray.
// No results if ray's origin is inside the cube
int intersects_ray_cube(glm::vec3 orig, glm::vec3 dir, glm::ivec3 cube)
{
	orig -= glm::dvec3(cube);
	glm::vec3 cube_center(0.5f, 0.5f, 0.5f);
	float pos = glm::dot(cube_center - orig, dir);
	if (pos + BlockRadius < 0) return -1;

	// optional, check if cube is far from ray
	float dist2 = glm::distance2(cube_center, orig + dir * pos);
	if (dist2 >= 0.75) return -1;

	if (orig.x < 0 && dir.x > 0)
	{
		float d = -orig.x / dir.x;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.y && k.y <= 1 && 0 <= k.z && k.z <= 1) return 0;
	}
	if (orig.x > 1 && dir.x < 0)
	{
		float d = (1 - orig.x) / dir.x;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.y && k.y <= 1 && 0 <= k.z && k.z <= 1) return 1;
	}
	if (orig.y < 0 && dir.y > 0)
	{		
		float d = -orig.y / dir.y;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.z && k.z <= 1) return 2;
	}
	if (orig.y > 1 && dir.y < 0)
	{
		float d = (1 - orig.y) / dir.y;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.z && k.z <= 1) return 3;
	}
	if (orig.z < 0 && dir.z > 0)
	{		
		float d = -orig.z / dir.z;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.y && k.y <= 1) return 4;
	}
	if (orig.z > 1 && dir.z < 0)
	{		
		float d = (1 - orig.z) / dir.z;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.y && k.y <= 1) return 5;
	}
	return -1;
}

Sphere select_sphere(8);

bool SelectCube(glm::ivec3& sel_cube, int& sel_face)
{
	float* ma = glm::value_ptr(player::orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);
	glm::ivec3 p = glm::ivec3(glm::floor(player::position));

	for (glm::i8vec3 d : select_sphere)
	{
		glm::ivec3 cube = p + glm::ivec3(d);
		if (map_get(cube) != 0 && (sel_face = intersects_ray_cube(player::position, dir, cube)) != -1)
		{
			sel_cube = cube;
			return true;
		}
	}
	return false;
}

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

		current_query_version += 1;
	}
	
	if (!console.IsVisible())
	{
		model_move_player(window, dt);
	}

	glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
	player::cposx = cplayer.x;
	player::cposy = cplayer.y;
	player::cposz = cplayer.z;
	
	selection = SelectCube(/*out*/sel_cube, /*out*/sel_face);
	double end = std::max<float>(0, (glfwGetTime() - time) * 1000);
	stats::model_time_ms = stats::model_time_ms * 0.85f + end * 0.15f;
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
	FOR(i, octaves - 1)
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
	FOR(x, size) FOR(y, size)
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
	FOR(i, OcclusionQueryCount) free_queries.push_back(occlusion_query[i]);

	block_program = load_program("block");
	block_matrix_loc = glGetUniformLocation(block_program, "matrix");
	block_sampler_loc = glGetUniformLocation(block_program, "sampler");
	block_pos_loc = glGetUniformLocation(block_program, "pos");
	block_eye_loc = glGetUniformLocation(block_program, "eye");
	block_position_loc = glGetAttribLocation(block_program, "position");
	block_color_loc = glGetAttribLocation(block_program, "color");
	block_light_loc = glGetAttribLocation(block_program, "light");
	block_uv_loc = glGetAttribLocation(block_program, "uv");

	g_chunks.start_threads();
}

GLuint block_buffer;
GLuint line_buffer;

GLfloat* line_data = new GLfloat[24 * 3];

void render_general_init()
{
	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &line_buffer);
}

void BlockRenderer::vertex(GLubyte light, GLubyte uv, int c)
{
	Vertex v;
	v.color = m_color;
	v.light = light;
	v.uv = uv;
	v.pos[0] = (m_pos.x & ChunkSizeMask) + Cube::corner[c].x;
	v.pos[1] = (m_pos.y & ChunkSizeMask) + Cube::corner[c].y;
	v.pos[2] = (m_pos.z & ChunkSizeMask) + Cube::corner[c].z;
	m_vertices.push_back(v);
}

void BlockRenderer::draw_quad(int a, int b, int c, int d)
{
	GLubyte light = light_cache[a][b][c];
	vertex(light, 0, a);
	vertex(light, 1, b);
	vertex(light, 3, c);
	vertex(light, 0, a);
	vertex(light, 3, c);
	vertex(light, 2, d);
}

void BlockRenderer::draw_triangle(int a, int b, int c)
{
	GLubyte light = light_cache[a][b][c];
	vertex(light, 0, a);
	vertex(light, 1, b);
	vertex(light, 3, c);
}

int B(int a, int i) { return (a & (1 << i)) >> i; }

void BlockRenderer::draw_face(int block, int a, int b, int c, int d)
{
	int n = B(block, a) + B(block, b) + B(block, c) + B(block, d);
	if (n == 4)
	{
		draw_quad(a, b, c, d);
	}
	else if (n == 3)
	{
		GLubyte light = light_cache[a][b][c];
		if (block & (1 << a)) vertex(light, 0, a);
		if (block & (1 << b)) vertex(light, 1, b);
		if (block & (1 << c)) vertex(light, 3, c);
		if (block & (1 << d)) vertex(light, 2, d);
	}
}

bool Visible(int a, int b) { return (a | b) != a; }

// every bit from 0 to 7 in block represents once vertex (can be off or on)
// cube is 255, empty is 0, prisms / pyramids / anti-pyramids are in between
void BlockRenderer::render(glm::ivec3 pos, Block bs)
{
	if (bs.shape == 0) return;
	m_pos = pos;
	m_color = bs.color;
	int block = bs.shape;

	if (block == 255)
	{
		if (Visible(MaxX(map_get(pos - ix)), MinX(block))) draw_quad(0, 4, 6, 2);
		if (Visible(MinX(map_get(pos + ix)), MaxX(block))) draw_quad(1, 3, 7, 5);
		if (Visible(MaxY(map_get(pos - iy)), MinY(block))) draw_quad(0, 1, 5, 4);
		if (Visible(MinY(map_get(pos + iy)), MaxY(block))) draw_quad(2, 6, 7, 3);
		if (Visible(MaxZ(map_get(pos - iz)), MinZ(block))) draw_quad(0, 2, 3, 1);
		if (Visible(MinZ(map_get(pos + iz)), MaxZ(block))) draw_quad(4, 5, 7, 6);
		return;		
	}

	// common faces
	if (Visible(MaxX(map_get(pos - ix)), MinX(block))) draw_face(block, 0, 4, 6, 2);
	if (Visible(MinX(map_get(pos + ix)), MaxX(block))) draw_face(block, 1, 3, 7, 5);
	if (Visible(MaxY(map_get(pos - iy)), MinY(block))) draw_face(block, 0, 1, 5, 4);
	if (Visible(MinY(map_get(pos + iy)), MaxY(block))) draw_face(block, 2, 6, 7, 3);
	if (Visible(MaxZ(map_get(pos - iz)), MinZ(block))) draw_face(block, 0, 2, 3, 1);
	if (Visible(MinZ(map_get(pos + iz)), MaxZ(block))) draw_face(block, 4, 5, 7, 6);

	switch (block)
	{
	// prism faces
	case  63: draw_quad(2, 4, 5, 3); break;
	case 252: draw_quad(4, 2, 3, 5); break;
	case 207: draw_quad(1, 7, 6, 0); break;
	case 243: draw_quad(7, 1, 0, 6); break;
	case 175: draw_quad(0, 5, 7, 2); break;
	case 245: draw_quad(5, 0, 2, 7); break;
	case  95: draw_quad(3, 6, 4, 1); break;
	case 250: draw_quad(6, 3, 1, 4); break;
	case 221: draw_quad(0, 3, 7, 4); break;
	case 187: draw_quad(3, 0, 4, 7); break;
	case 238: draw_quad(5, 6, 2, 1); break;
	case 119: draw_quad(6, 5, 1, 2); break;
	// tetra faces
	case 23: draw_triangle(1, 2, 4); break;
	case 43: draw_triangle(0, 5, 3); break;
	case 77: draw_triangle(3, 6, 0); break;
	case 142: draw_triangle(2, 1, 7); break;
	case 113: draw_triangle(5, 0, 6); break;
	case 178: draw_triangle(4, 7, 1); break;
	case 212: draw_triangle(7, 4, 2); break;
	case 232: draw_triangle(6, 3, 5); break;
	// xtetra faces
	case 254: draw_triangle(1, 4, 2); break;
	case 253: draw_triangle(0, 3, 5); break;
	case 251: draw_triangle(3, 0, 6); break;
	case 247: draw_triangle(2, 7, 1); break;
	case 239: draw_triangle(5, 6, 0); break;
	case 223: draw_triangle(4, 1, 7); break;
	case 191: draw_triangle(7, 2, 4); break;
	case 127: draw_triangle(6, 5, 3); break;
	// pyramid faces TODO: more
	case 47: draw_triangle(5, 3, 2); draw_triangle(5, 2, 0); break;
	case 31: draw_triangle(4, 1, 3); draw_triangle(4, 3, 2); break;
	case 79: draw_triangle(6, 0, 1); draw_triangle(6, 1, 3); break;
	case 143: draw_triangle(7, 2, 0); draw_triangle(7, 0, 1); break;
	case 241: draw_triangle(6, 7, 0); draw_triangle(7, 5, 0); break;
	case 242: draw_triangle(4, 6, 1); draw_triangle(6, 7, 1); break;
	case 244: draw_triangle(7, 5, 2); draw_triangle(5, 4, 2); break;
	case 248: draw_triangle(5, 4, 3); draw_triangle(4, 6, 3); break;
	// screw faces TODO: more
	case 159: draw_triangle(4, 7, 2); draw_triangle(4, 1, 7); break;
	case 111: draw_triangle(5, 6, 0); draw_triangle(6, 5, 3); break;
	case 246: draw_triangle(7, 1, 2); draw_triangle(1, 4, 2); break;
	case 249: draw_triangle(6, 3, 0); draw_triangle(3, 5, 0); break;
	}
}

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

	FOR(i, 4) frustum[i] *= glm::fastInverseSqrt(sqr(frustum[i].xyz()));
}

bool Frustum::IsSphereCompletelyOutside(glm::vec3 p, float radius) const
{
	FOR(i, 4) if (glm::dot(p, frustum[i].xyz()) + frustum[i].w < -radius) return true;
	return false;
}

int buffer_budget = 0;

bool chunk_renderable(Chunk& chunk, const Frustum& frustum)
{
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
		if (!g_chunks.loaded(b)) break;

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

void precompute_potentially_visible_chunks(glm::ivec3 cplayer, int budget=5000)
{
	if (!g_chunks.loaded(cplayer)) return;
	budget = precompute_potentially_visible_chunks(g_chunks(cplayer), budget);
	if (budget <= 0) return;

	if (pvs != 2) return;
	for (glm::i8vec3 d : render_sphere)
	{
		if (!g_chunks.loaded(cplayer + glm::ivec3(d))) break;
		Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
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

	if (g_chunks.loaded(cplayer))
	{
	Chunk& chunk0 = g_chunks(cplayer);
	render_chunk(chunk0);

	std::vector<glm::i8vec3>& filter = pvs ? chunk0.visible.chunks : render_sphere;
	if (occlusion == 0)
	{
		for (glm::i8vec3 d : filter)
		{
			if (g_chunks.loaded(cplayer + glm::ivec3(d)))
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
	}
	else
	{
		for (glm::i8vec3 d : filter)
		{
			Chunk& chunk = g_chunks(cplayer + glm::ivec3(d));
			if (!chunk_renderable(chunk, frustum))
				continue;

			if (chunk.query_version != current_query_version || chunk.query == (GLuint)(-1))
			{
				if (chunk.query == (GLuint)(-1)) chunk.query = GetFreeQuery();
				chunk.query_version = current_query_version;
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
			}
		}
	}
	}

	glUseProgram(0);
	if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	float time_ms = (glfwGetTime() - time_start) * 1000;
	stats::block_render_time_ms = stats::block_render_time_ms * 0.85f + time_ms * 0.15f;
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
	FOR(i, 2) FOR(j, 2)
	{
		glm::ivec3 sel = sel_cube;
		*vline++ = Q(sel + i*iy + j*iz, c); *vline++ = Q(sel + ix + i*iy + j*iz, c);
		*vline++ = Q(sel + i*ix + j*iz, c); *vline++ = Q(sel + iy + i*ix + j*iz, c);
		*vline++ = Q(sel + i*ix + j*iy, c); *vline++ = Q(sel + iz + i*ix + j*iy, c);
	}

	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 3 * 24, line_data, GL_STREAM_DRAW);
	glDrawArrays(GL_LINES, 0, 24);

	glDisableVertexAttribArray(line_position_loc);
}

void render_world()
{
	// TODO - avoid recomputing if same
	glm::mat4 matrix = glm::translate(perspective_rotation, -player::position);
	Frustum frustum;
	frustum.Init(matrix);

	glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
	
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
	if (show_counters)
	text->Printf("[%.1f %.1f %.1f] tool:%s C:%4d T:%3dk frame:%2.1f model:%1.1f pvs:%2.1f_%luk render %2.1f",
			 player::position.x, player::position.y, player::position.z, block_tool_name[block_tool], stats::chunk_count, stats::triangle_count / 3000,
			 stats::frame_time_ms, stats::model_time_ms, stats::pvc_time_ms, pvcs_computed / 1000, stats::block_render_time_ms);
	stats::triangle_count = 0;
	stats::chunk_count = 0;

	if (show_counters)
	{
		if (selection)
		{
			Block block = map_get_block(sel_cube);
			int red = block.color % 4;
			int green = (block.color / 4) % 4;
			int blue = block.color / 16;
			const char* name = "###";
			int c = shape_class[block.shape];
			if (0 <= c && c <= 5) name = block_tool_name[c];
			text->Printf("selected: [%d %d %d], %s, shape %u, color [%u %u %u]", sel_cube.x, sel_cube.y, sel_cube.z, name, (uint)block.shape, red, green, blue);
		}
		else
		{
			text->Print("", 0);
		}
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

int main(int argc, char** argv)
{
	init_shape_class();

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
	glViewport(0, 0, width, height);
	
	float frame_start = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		float frame_end = glfwGetTime();
		stats::frame_time_ms = stats::frame_time_ms * 0.85f + (frame_end - frame_start) * 1000 * 0.15f;
		frame_start = frame_end;

		model_frame(window);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		render_world();
		render_gui();
		glfwPollEvents();
		glfwSwapBuffers(window);
	}
	glfwTerminate();
	return 0;
}
