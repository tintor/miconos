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

struct Closer
{
	glm::ivec3 origin;
	bool operator()(glm::ivec3 a, glm::ivec3 b) { return glm::distance2(a, origin) < glm::distance2(b, origin); }
};

struct Sphere : public std::vector<glm::ivec3>
{
	Sphere(int size)
	{
		FOR2(x, -size, size) FOR2(y, -size, size) FOR2(z, -size, size)
		{
			glm::ivec3 d(x, y, z);
			if (glm::dot(d, d) > 0 && glm::dot(d, d) <= size * size) push_back(d);
		}
		std::sort(begin(), end(), Closer{glm::ivec3(0,0,0)});
	}
};


// ============================

const int ChunkSizeBits = 4, MapSizeBits = 7;
const int ChunkSize = 1 << ChunkSizeBits, MapSize = 1 << MapSizeBits;
const int ChunkSizeMask = ChunkSize - 1, MapSizeMask = MapSize - 1;

static const int RenderDistance = 63;
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

typedef unsigned char Block;

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

	Block& operator[](glm::ivec3 a)
	{
		assert(a.x >= 0 && a.x < ChunkSize && a.y >= 0 && a.y < ChunkSize && a.z >= 0 && a.z < ChunkSize);
		return block[a.x][a.y][a.z];
	}
};

struct Heightmap
{
	int height[ChunkSize * MapSize][ChunkSize * MapSize];
	bool hasTree[ChunkSize * MapSize][ChunkSize * MapSize];
	Block color[ChunkSize * MapSize][ChunkSize * MapSize];
	glm::ivec2 last[MapSize][MapSize];

	Heightmap()
	{
		FOR(x, ChunkSize) FOR(y, ChunkSize) last[x][y] = glm::ivec2(1000000, 1000000);
	}

	void Populate(int cx, int cy);
	int& Height(int x, int y) { return height[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
	bool& HasTree(int x, int y) { return hasTree[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
	Block& Color(int x, int y) { return color[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
};

int GetHeight(int x, int y)
{
	float q = SimplexNoise(glm::vec2(x, y) * 0.004f, 6, 0.5f, 0.5f, true);
	return q * q * q * q * 200;
}

Block GetColor(int x, int y)
{
	return std::min<int>(63, floorf((1 + SimplexNoise(glm::vec2(x, y) * -0.024f, 6, 0.5f, 0.5f, false)) * 8)) + 64 * 3;
}

float Tree(int x, int y)
{
	return SimplexNoise(glm::vec2(x+321398, y+8901) * 0.002f, 4, 2.0f, 0.5f, true);
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
			Color(x + cx * ChunkSize, y + cy * ChunkSize) = GetColor(x + cx * ChunkSize, y + cy * ChunkSize);
		}
		last[cx & MapSizeMask][cy & MapSizeMask] = glm::ivec2(cx, cy);
	}
}

Heightmap* heightmap = new Heightmap;

Block Color(int r, int g, int b, int a)
{
	assert(0 <= r && r < 4);
	assert(0 <= g && g < 4);
	assert(0 <= b && b < 4);
	assert(0 <= a && a < 4);
	return r + g * 4 + b * 16 + a * 64;
}

Block Empty = Color(3, 3, 3, 0);
Block Leaves = 255;

const int CraterRadius = 500;
const glm::ivec3 CraterCenter(CraterRadius * -0.8, CraterRadius * -0.8, 0);

const int MoonRadius = 500;
const glm::ivec3 MoonCenter(MoonRadius * 0.8, MoonRadius * 0.8, 0);

void GenerateTerrain(glm::ivec3 cpos, MapChunk& chunk)
{
	memset(chunk.block, Empty, sizeof(Block) * ChunkSize * ChunkSize * ChunkSize);

	heightmap->Populate(cpos.x, cpos.y);

	FOR(x, ChunkSize) FOR(y, ChunkSize)
	{
		int dx = x + cpos.x * ChunkSize;
		int dy = y + cpos.y * ChunkSize;

		FOR(z, ChunkSize)
		{
			int dz = z + cpos.z * ChunkSize;
			glm::ivec3 pos(dx, dy, dz);

			if (dz > 100 && dz < 200)
			{
				// clouds
				double q = SimplexNoise(glm::vec3(pos) * 0.01f, 4, 0.5f, 0.5f, false);
				if (q < -0.35) chunk.block[x][y][z] = Color(3, 3, 3, 2);
			}
			else if (dz <= heightmap->Height(dx, dy))
			{			
				// ground and caves
				chunk.block[x][y][z] = heightmap->Color(dx, dy);
				double q = SimplexNoise(glm::vec3(pos) * 0.03f, 4, 0.5f, 0.5f, false);
				if (q < -0.25) chunk.block[x][y][z] = Empty;
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
					chunk.block[x][y][z] = Color(0, 0, 3, 3);
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
					chunk.block[x][y][z] = Leaves;
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
					chunk.block[x][y][z] = Leaves;
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
					chunk.block[x][y][z] = Leaves;
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
					chunk.block[x][y][z] = Leaves;
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

				chunk.block[x][y][z] = Color(2, 2, 2, 3);
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
				if (sz > 0 || z + ChunkSize * cpos.z >= 100) continue;

				chunk.block[x][y][z] = Empty;
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

	MapChunk& get(glm::ivec3 cpos)
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

	MapChunk* try_get(glm::ivec3 cpos)
	{
		int h = KeyHash()(cpos) % MapSize;
		MapChunk* head = m_map[h].load(std::memory_order_relaxed);
		for (MapChunk* i = head; i; i = i->next)
		{
			if (i->cpos == cpos) return i;
		}
		return nullptr;
	}
	
	void Set(glm::ivec3 pos, Block block)
	{
		get(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask] = block;
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
};

// ============================

int S(int a) { return 1 << a; }

Map::Map() : m_cpos(0x80000000, 0, 0)
{
	FOR(i, MapSize) m_map[i] = nullptr;
	InitColorCodes();

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

		Set(glm::ivec3(x+1, y-10, z+30), Color(3, 3, 0, 3));
	}

	// all colors
	FOR(x, 4) FOR(y, 4) FOR(z, 4) Set(glm::ivec3(x*2, y*2, z*2+45), Color(x, y, z, 3));
}

// ============================

Map* map = new Map;

Block& map_get(glm::ivec3 pos) { return map->get(pos >> ChunkSizeBits)[pos & ChunkSizeMask]; }

// ============================

struct Vertex
{
	GLubyte color;
	GLubyte light;
	GLubyte uv;
	GLubyte pos[3];
};

struct Frustum
{
	std::array<glm::vec4, 4> frustum;
	void init(const glm::mat4& matrix);

	bool contains_point(glm::vec3 p) const;

	bool is_sphere_outside(glm::vec3 p, float radius) const
	{
		FOR(i, 4) if (glm::dot(p, frustum[i].xyz()) + frustum[i].w < -radius) return true;
		return false;
	}
};

const float BlockRadius = sqrtf(3) / 2;

struct VisibleChunks
{
	BitCube<MapSize> set;
	std::vector<glm::ivec3> array;	

	VisibleChunks() { set.clear(); }

	void add(glm::ivec3 v)
	{
		if (set.xset(v & MapSizeMask)) array.push_back(v);
	}

	void cleanup(glm::ivec3 center, Frustum& frustum)
	{
		int w = 0;
		for (glm::ivec3 c : array)
		{
			glm::vec3 sphere(c * ChunkSize + ChunkSize / 2);
			if (frustum.is_sphere_outside(sphere, ChunkSize * BlockRadius) || glm::distance2(c, center) > sqr(RenderDistance))
			{
				assert(set[c & MapSizeMask]);
				set.clear(c & MapSizeMask);
			}
			else
			{
				array[w++] = c;
			}
		}
		array.resize(w);
	}
};

struct BlockRenderer
{
	std::vector<Vertex> m_vertices;
	glm::ivec3 m_pos;
	GLubyte m_color;

	void vertex(GLubyte light, GLubyte uv, int c);
	void draw_quad(int a, int b, int c, int d);
	void render(glm::ivec3 pos, Block bs);
};

struct Chunk
{
	std::mutex mutex;
	glm::ivec3 cpos; // TODO -> should be atomic -> pack into uint64 -> 21 bit per coordinate -> 25 effective
	std::vector<Vertex> vertices;
	int render_size;

	void buffer(BlockRenderer& renderer)
	{
		renderer.m_vertices.clear();
		MapChunk& mc = map->get(cpos);
		FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
		{
			renderer.render(cpos * ChunkSize + glm::ivec3(x, y, z), mc.block[x][y][z]);
		}
	
		vertices.resize(renderer.m_vertices.size());
		std::copy(renderer.m_vertices.begin(), renderer.m_vertices.end(), vertices.begin());
		assert(vertices.size() == vertices.capacity());
		render_size = vertices.size();
	}

	bool init(glm::ivec3 _cpos, BlockRenderer& renderer)
	{
		mutex.lock();
		if (cpos == _cpos) { mutex.unlock(); return true; }
		cpos = _cpos;
		release(vertices);
		buffer(renderer);
		mutex.unlock();
		return false;
	}

	void reset(BlockRenderer& renderer)
	{
		mutex.lock();
		release(vertices);
		buffer(renderer);
		mutex.unlock();
	}

	void sort_relative_to(glm::ivec3 cpos)
	{
		// TODO: move visible quads
	}

	Chunk() : cpos(0x80000000, 0, 0), render_size(0) { }
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

			int q = 0;
			if (owner(cpos) == k && chunks->get(cpos).init(cpos, renderer)) q += 1;
			for (int i = 0; i < render_sphere.size(); i++)
			{
				glm::ivec3 c = cpos + glm::ivec3(render_sphere[i]);
				if (owner(c) == k && chunks->get(c).init(c, renderer))
				{
					q += 1;
					if (q >= 1000) { last_cpos.x = 0x80000000; break; }
				}
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

template<int size>
bool intersects_line_polygon(Plucker line, const Plucker edges[size])
{
	FOR(i, size) if (line_crossing(line, edges[i]) > 0) return false;
	return true;
}

struct BoundaryBlocks : public std::vector<glm::ivec3>
{
	BoundaryBlocks()
	{
		const int M = ChunkSize - 1;
		FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
			if (x == 0 || x == M || y == 0 || y == M || z == 0 || z == M)
				push_back(glm::ivec3(x, y, z));
	}
} boundary_blocks;

bool can_move_through(Block block) { return block == Empty; }
bool can_see_through(Block block) { return block == Empty; }

struct Directions : public std::vector<glm::ivec3>
{
	static const int Bits = 9;
	Directions()
	{
		const int M = 1 << Bits, N = M - 1;
		FOR2(x, static_cast<int>(1), M) FOR2(y, static_cast<int>(1), M) FOR2(z, static_cast<int>(1), M)
		{
			int d2 = x*x + y*y + z*z;
			if (N*N < d2 && d2 <= M*M)
			{
				push_back(glm::ivec3( x,  y,  z));
				push_back(glm::ivec3( x,  y, -z));
				push_back(glm::ivec3( x, -y,  z));
				push_back(glm::ivec3( x, -y, -z));
				push_back(glm::ivec3(-x,  y,  z));
				push_back(glm::ivec3(-x,  y, -z));
				push_back(glm::ivec3(-x, -y,  z));
				push_back(glm::ivec3(-x, -y, -z));
			}
		}
		push_back(glm::ivec3( M, 0, 0));
		push_back(glm::ivec3(-M, 0, 0));
		push_back(glm::ivec3(0,  M, 0));
		push_back(glm::ivec3(0, -M, 0));
		push_back(glm::ivec3(0, 0,  M));
		push_back(glm::ivec3(0, 0, -M));
		for (int i = 0; i < size(); i++)
		{
			std::swap(operator[](std::rand() % size()), operator[](i));
		}
	}
} directions;

int ray_it = 0;
int rays_remaining = 0;

VisibleChunks visible_chunks;

// which chunks must be rendered from the center chunk?
void update_render_list(glm::ivec3 cpos, Frustum& frustum)
{
	if (rays_remaining == 0) return;
	MapChunk& mc = map->get(cpos);
	const int D = Directions::Bits;
	int MaxDist = RenderDistance * ChunkSize;
	int size = visible_chunks.array.size();

	float k = 1 << D;
	glm::ivec3 origin = glm::ivec3(glm::floor(player::position * k));

	int budget = 3000000;
	while (budget > 0 && rays_remaining > 0)
	{
		glm::ivec3 dir = directions[ray_it++];
		if (ray_it == directions.size()) ray_it = 0;
		rays_remaining -= 1;
		if (!frustum.contains_point(player::position + glm::vec3(dir))) continue;

		glm::ivec3 photon = origin;
		int dist = 0;
		MapChunk* pmc = &mc;
		while (true)
		{
			photon += dir;
			dist += 1;
			if (dist > MaxDist) break;
			glm::ivec3 voxel = (photon >> D);
			glm::ivec3 cvoxel = voxel >> ChunkSizeBits;
			if (pmc->cpos != cvoxel)
			{
				pmc = map->try_get(cvoxel);
				if (!pmc) break;
			}
			Block block = (*pmc)[voxel & ChunkSizeMask];
			if (block != Empty) visible_chunks.add(cvoxel);
			if (!can_see_through(block)) break;
		}
		budget -= dist;
	}

	if (visible_chunks.array.size() != size) std::sort(visible_chunks.array.begin(), visible_chunks.array.end(), Closer{cpos});
}

namespace stats
{
	int triangle_count = 0;
	int chunk_count = 0;

	float frame_time_ms = 0;
	float block_render_time_ms = 0;
	float model_time_ms = 0;
	float raytrace_time_ms = 0;
}

// Model

float last_time;

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
bool selection = false;
bool wireframe = false;

BlockRenderer g_renderer;

void EditBlock(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	glm::ivec3 p(pos.x & ChunkSizeMask, pos.y & ChunkSizeMask, pos.z & ChunkSizeMask);

	Block prev_block = map_get(sel_cube);
	map->Set(pos, block);
	g_chunks(cpos).reset(g_renderer);

	if (p.x == 0) g_chunks(cpos - ix).reset(g_renderer);
	if (p.y == 0) g_chunks(cpos - iy).reset(g_renderer);
	if (p.z == 0) g_chunks(cpos - iz).reset(g_renderer);
	if (p.x == ChunkSizeMask) g_chunks(cpos + ix).reset(g_renderer);
	if (p.y == ChunkSizeMask) g_chunks(cpos + iy).reset(g_renderer);
	if (p.z == ChunkSizeMask) g_chunks(cpos + iz).reset(g_renderer);
}

void OnEditKey(int key)
{
	Block block = map_get(sel_cube);
	int r = block % 4;
	int g = (block / 4) % 4;
	int b = (block / 16) % 4;
	int a = block % 64;

	if (key == GLFW_KEY_Z)
	{
		block = Color((r + 1) % 4, g, b, a);
		EditBlock(sel_cube, block);
	}
	if (key == GLFW_KEY_X)
	{
		block = Color(r, (g + 1) % 4, b, a);
		EditBlock(sel_cube, block);
	}
	if (key == GLFW_KEY_C)
	{
		block = Color(r, g, (b + 1) % 4, a);
		EditBlock(sel_cube, block);
	}
	if (key == GLFW_KEY_V)
	{
		block = Color(r, g, b, (a + 1) % 4);
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
		if (key == GLFW_KEY_F2)
		{
			console.Show();
		}
		else if (key == GLFW_KEY_F3)
		{
			wireframe = !wireframe;
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
		EditBlock(sel_cube, Empty);
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		EditBlock(sel_cube + dir[sel_face], map_get(sel_cube));
	}
}

void model_init(GLFWwindow* window)
{
	glfwSetKeyCallback(window, OnKey);
	glfwSetMouseButtonCallback(window, OnMouseButton);

	last_time = glfwGetTime();

	glfwSetCursorPos(window, 0, 0);
}

int NeighborBit(int dx, int dy, int dz)
{
	return 1 << ((dx + 1) + (dy + 1) * 3 + (dz + 1) * 9);
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
int SphereVsCube(glm::vec3& center, float radius, glm::ivec3 cube, int neighbors)
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

int CubeNeighbors(glm::ivec3 cube)
{
	int neighbors = 0;
	FOR2(dx, -1, 1) FOR2(dy, -1, 1) FOR2(dz, -1, 1)
	{
		if (map_get(glm::ivec3(cube.x + dx, cube.y + dy, cube.z + dz)) != Empty)
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
			if (map_get(cube) != Empty && 1 == SphereVsCube(player::position, 1.6f, cube, CubeNeighbors(cube)))
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
	player::position += dir * ((player::flying ? 20 : 10) * dt) + player::velocity * dt;
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
		if (dt <= 0.008)
		{
			Simulate(dt, dir);
			break;
		}
		Simulate(0.008, dir);
		dt -= 0.008;
	}
	if (p != player::position) rays_remaining = directions.size();
}

float intersect_line_plane(glm::vec3 orig, glm::vec3 dir, glm::vec4 plane)
{
	assert(is_unit_length(dir));
	assert(is_unit_length(plane.xyz()));
	return (-plane.w - glm::dot(orig, plane.xyz())) / glm::dot(dir, plane.xyz());
}

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

	for (glm::ivec3 d : select_sphere)
	{
		glm::ivec3 cube = p + d;
		if (map_get(cube) != Empty && (sel_face = intersects_ray_cube(player::position, dir, cube)) != -1)
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
		if (player::pitch > M_PI / 2 * 0.999) player::pitch = M_PI / 2 * 0.999;
		if (player::pitch < -M_PI / 2 * 0.999) player::pitch = -M_PI / 2 * 0.999;
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

		rays_remaining = directions.size();		
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

int block_program;
GLuint block_matrix_loc;
GLuint block_sampler_loc;
GLuint block_pos_loc;
GLuint block_tick_loc;
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

	block_program = load_program("block");
	block_matrix_loc = glGetUniformLocation(block_program, "matrix");
	block_sampler_loc = glGetUniformLocation(block_program, "sampler");
	block_pos_loc = glGetUniformLocation(block_program, "pos");
	block_tick_loc = glGetUniformLocation(block_program, "tick");
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

// every bit from 0 to 7 in block represents once vertex (can be off or on)
void BlockRenderer::render(glm::ivec3 pos, Block block)
{
	if (block == Empty) return;
	m_pos = pos;
	m_color = block;

	if (can_see_through(map_get(pos - ix))) draw_quad(0, 4, 6, 2);
	if (can_see_through(map_get(pos + ix))) draw_quad(1, 3, 7, 5);
	if (can_see_through(map_get(pos - iy))) draw_quad(0, 1, 5, 4);
	if (can_see_through(map_get(pos + iy))) draw_quad(2, 6, 7, 3);
	if (can_see_through(map_get(pos - iz))) draw_quad(0, 2, 3, 1);
	if (can_see_through(map_get(pos + iz))) draw_quad(4, 5, 7, 6);
}

void Frustum::init(const glm::mat4& matrix)
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

bool Frustum::contains_point(glm::vec3 p) const
{
	FOR(i, 4) if (glm::dot(p, frustum[i].xyz()) + frustum[i].w < 0) return false;
	return true;
}

int g_tick;

void render_chunk(Chunk& chunk)
{
	if (chunk.render_size == 0) return;
	glm::ivec3 pos = chunk.cpos * ChunkSize;
	glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * chunk.vertices.size(), &chunk.vertices[0], GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, chunk.render_size);
}

void render_world_blocks(glm::ivec3 cplayer, const glm::mat4& matrix, const Frustum& frustum)
{
	float time_start = glfwGetTime();
	glBindTexture(GL_TEXTURE_2D, block_texture);

	//float* ma = glm::value_ptr(player::orientation);
	//glm::ivec3 direction(ma[4] * (1 << 20), ma[5] * (1 << 20), ma[6] * (1 << 20));

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

	glUniform1i(block_tick_loc, g_tick++);
	if (g_chunks.loaded(cplayer))
	{
		Chunk& chunk0 = g_chunks(cplayer);
		render_chunk(chunk0);

		auto it = visible_chunks.array.begin();
		for (; it != visible_chunks.array.end() && stats::triangle_count < MaxTriangles * 3; it++)
		{
			glm::ivec3 cpos = glm::ivec3(*it);
			if (g_chunks.loaded(cpos)) stats::triangle_count += g_chunks(cpos).vertices.size();
			stats::chunk_count += 1;
		}
		for (it--; it != visible_chunks.array.begin() - 1; it--)
		{
			glm::ivec3 cpos = glm::ivec3(*it);
			if (g_chunks.loaded(cpos)) render_chunk(g_chunks(cpos));
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
	glm::mat4 matrix = glm::translate(perspective_rotation, -player::position);
	Frustum frustum;
	frustum.init(matrix);

	glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
	
	float time_start = glfwGetTime();
	if (rays_remaining == directions.size()) visible_chunks.cleanup(cplayer, frustum);
	update_render_list(cplayer, frustum);
	float time = std::max<float>(0, (glfwGetTime() - time_start) * 1000);
	stats::raytrace_time_ms = stats::raytrace_time_ms * 0.75f + time * 0.25f;

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
	text->Printf("[%.1f %.1f %.1f] C:%4d T:%3dk frame:%2.1f model:%1.1f raytrace:%2.1f %.0f%% render %2.1f",
			 player::position.x, player::position.y, player::position.z, stats::chunk_count, stats::triangle_count / 3000,
			 stats::frame_time_ms, stats::model_time_ms, stats::raytrace_time_ms, 100.0f * (directions.size() - rays_remaining) / directions.size(), stats::block_render_time_ms);
	stats::triangle_count = 0;
	stats::chunk_count = 0;

	if (show_counters)
	{
		if (selection)
		{
			Block block = map_get(sel_cube);
			int r = block % 4;
			int g = (block / 4) % 4;
			int b = (block / 16) % 4;
			int a = block / 64;
			text->Printf("selected: [%d %d %d], color [%u %u %u %u]", sel_cube.x, sel_cube.y, sel_cube.z, r, g, b, a);
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

int main(int, char**)
{
	if (!glfwInit()) return 0;

	glfwSetErrorCallback(OnError);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode->width*2, mode->height*2, "Arena", glfwGetPrimaryMonitor(), NULL);
	if (!window) { glfwTerminate(); return 0; }
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
