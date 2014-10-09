// TODO:
// - slowly blend-in newly buffered chunks so that they are less noticable
// - persist map-chunks for faster loading!
// - MacBook Air support: detect number of cores, non-retina resolution, auto-reduce render distance based on frame time

// # Raytracing:
// - Raytracer when moving: clear set only, and not array. Deduplicate array when sorting. Remove stale array elements when raytracing is complete!
// - newly buffered chunks should re-start raytracer
// - generate ray in screen space (avoids testing all directions against frustum + adapts to different resolutions)
// - sky/ambient chunk cubes (generated when ray goes out of render distance), this allows us to avoid clearing the buffer (and makes holes when raycasting less noticable)

// # more proceduraly generated stuff!
// - nicer generated trees! palms!
// - water / sand / snow!
// - diffuse/reduce process?

// # eye-candy
// - grass with wind effect!
// - nicer textures!

// # gravity mode: planar (2 degrees of freedom orientation), spherical (2 degrees of freedom orientation), zero-g (3 degrees of freedom orientation)

// # PERF level-of-detail rendering
// - mesh reduction for far away chunks:
//	- remove interior quads not visible from far outside
//    - remove small lone blocks, fill small lone holes
//    - two concave quads at 90 degrees can be converted into slope (even if not far!)
// - combine 2x2x2 chunks that are far to be able to optimize their quads better and to reduce the total number of chunks as render distances are increasing

// - semi-transparent alpha color component (0-> 25%) (1->50%) (2->75%) (3->100%)
// - need to re-sort triangles inside chunk buffer (for nearby chunks only) as player moves
// - textures with transparent pixels

// # client / server:
// - disk persistence on server
// - terrain generation on server
// - server sends: chunks, block updates and player position
// - client sends: edits, player commands

// multi-player:
// - render simple avatars
// - login users
// - chat

// sky:
// - day/night cycle
// - sun with bloom effect (shaders)

// # advanced editor:
// # - free mouse (but fixed camera) mode for editing
// # - orbit camera mode for editing
// # - flood fill tool
// # - selection tool
// # - cut/copy/paste/move tool
// # - drawing shapes (line / wall)
// # - undo / redo
// # - wiki / user change tracking

// # water:
// # - animated / reflective surface (shaders)
// # - simple water (minecraft) with darker light in depth (flood fill lake!)

// # ambient lighting and shadows

// # portals! inside world and between worlds

// # bots / critters
// # python scripting integration (sandbox)

#include "util.hh"
#include "callstack.hh"
#include "rendering.hh"
#include "auto.hh"
#include "unistd.h"

// GUI

int width;
int height;

// Map

const int ChunkSizeBits = 4, MapSizeBits = 7;
const int ChunkSize = 1 << ChunkSizeBits, MapSize = 1 << MapSizeBits;
const int ChunkSizeMask = ChunkSize - 1, MapSizeMask = MapSize - 1;

const int RenderDistance = 63;
static_assert(RenderDistance < MapSize / 2, "");

Sphere render_sphere(RenderDistance);

typedef unsigned char Block;

namespace Cube
{
	glm::ivec3 corner[8];
	const int faces[6][4] = { { 0, 4, 6, 2 }/*xmin*/, { 1, 3, 7, 5 }/*xmax*/, { 0, 1, 5, 4 }/*ymin*/, { 2, 6, 7, 3 }/*ymax*/, { 0, 2, 3, 1 }/*zmin*/, { 4, 5, 7, 6 }/*zmax*/ };
	Initialize { FOR(i, 8) corner[i] = glm::ivec3(i&1, (i>>1)&1, (i>>2)&1); }
}

// ============================

Block Color(int r, int g, int b, int a)
{
	assert(0 <= r && r < 4 && 0 <= g && g < 4 && 0 <= b && b < 4 && 0 <= a && a < 4);
	return r + g * 4 + b * 16 + a * 64;
}

Block Empty = Color(3, 3, 3, 0);
Block Leaves = 255;

struct MapChunk
{
	glm::ivec3 cpos;
	int count_empty;
	MapChunk* next;
private:
	Block block[ChunkSize * ChunkSize * ChunkSize];
public:

	MapChunk()
	{
		memset(block, Empty, sizeof(Block) * ChunkSize * ChunkSize * ChunkSize);
		count_empty = ChunkSize * ChunkSize * ChunkSize;
	}

	Block operator[](glm::ivec3 a) { return block[index(a)]; }

	void set(glm::ivec3 a, Block b)
	{
		int i = index(a);
		if (block[i] == b) return;
		if (block[i] == Empty) count_empty -= 1;
		if (b == Empty) count_empty += 1;
		block[i] = b;
	}

private:
	int index(glm::ivec3 a)
	{
		// Z-order space filling curve!
		assert(a.x >= 0 && a.x < ChunkSize && a.y >= 0 && a.y < ChunkSize && a.z >= 0 && a.z < ChunkSize);
		int e = 0;
		int w = 0;
		FOR(i, 4)
		{
			glm::ivec3 b = a & 1;
			a >>= 1;
			e += ((b.x * 2 + b.y) * 2 + b.z) << w;
			w += 3;
		}
		return e;
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

const int CraterRadius = 500;
const glm::ivec3 CraterCenter(CraterRadius * -0.8, CraterRadius * -0.8, 0);

const int MoonRadius = 500;
const glm::ivec3 MoonCenter(MoonRadius * 0.8, MoonRadius * 0.8, 0);

void GenerateTree(MapChunk& mc, int x, int y)
{
	int dx = x + mc.cpos.x * ChunkSize;
	int dy = y + mc.cpos.y * ChunkSize;

	if (heightmap->HasTree(dx, dy))
	{
		int height = heightmap->Height(dx, dy);
		FOR(z, ChunkSize)
		{
			int dz = z + mc.cpos.z * ChunkSize;
			if (dz > height && dz < height + 6)
			{
				mc.set(glm::ivec3(x, y, z), Color(0, 0, 3, 3));
			}
		}
		return;
	}
	if (heightmap->HasTree(dx, dy-1))
	{
		int height = heightmap->Height(dx, dy-1);
		FOR(z, ChunkSize)
		{
			int dz = z + mc.cpos.z * ChunkSize;
			if (dz > height + 2 && dz < height + 6)
			{
				mc.set(glm::ivec3(x, y, z), Leaves);
			}
		}
		return;
	}
	if (heightmap->HasTree(dx, dy+1))
	{
		int height = heightmap->Height(dx, dy+1);
		FOR(z, ChunkSize)
		{
			int dz = z + mc.cpos.z * ChunkSize;
			if (dz > height + 2 && dz < height + 6)
			{
				mc.set(glm::ivec3(x, y, z), Leaves);
			}
		}
		return;
	}
	if (heightmap->HasTree(dx+1, dy))
	{
		int height = heightmap->Height(dx+1, dy);
		FOR(z, ChunkSize)
		{
			int dz = z + mc.cpos.z * ChunkSize;
			if (dz > height + 2 && dz < height + 6)
			{
				mc.set(glm::ivec3(x, y, z), Leaves);
			}
		}
		return;
	}
	if (heightmap->HasTree(dx-1, dy))
	{
		int height = heightmap->Height(dx-1, dy);
		FOR(z, ChunkSize)
		{
			int dz = z + mc.cpos.z * ChunkSize;
			if (dz > height + 2 && dz < height + 6)
			{
				mc.set(glm::ivec3(x, y, z), Leaves);
			}
		}
		return;
	}
}

void GenerateTerrain(MapChunk& mc)
{
	heightmap->Populate(mc.cpos.x, mc.cpos.y); // TODO: not thread safe!

	FOR(x, ChunkSize) FOR(y, ChunkSize)
	{
		int dx = x + mc.cpos.x * ChunkSize;
		int dy = y + mc.cpos.y * ChunkSize;

		FOR(z, ChunkSize)
		{
			int dz = z + mc.cpos.z * ChunkSize;
			glm::ivec3 pos(dx, dy, dz);

			if (dz > 100 && dz < 200)
			{
				// clouds
				double q = SimplexNoise(glm::vec3(pos) * 0.01f, 4, 0.5f, 0.5f, false);
				if (q < -0.35) mc.set(glm::ivec3(x, y, z), Color(3, 3, 3, 2));
			}
			else if (dz <= heightmap->Height(dx, dy))
			{
				// ground and caves
				double q = SimplexNoise(glm::vec3(pos) * 0.03f, 4, 0.5f, 0.5f, false);
				mc.set(glm::ivec3(x, y, z), q < -0.25 ? Empty : heightmap->Color(dx, dy));
			}
		}
	}

	FOR(x, ChunkSize) FOR(y, ChunkSize) GenerateTree(mc, x, y);

	// moon
	FOR(x, ChunkSize)
	{
		int dx = x + mc.cpos.x * ChunkSize;
		int64_t sx = sqr<int64_t>(dx - MoonCenter.x) - sqr<int64_t>(MoonRadius);
		if (sx > 0) continue;

		FOR(y, ChunkSize)
		{
			int dy = y + mc.cpos.y * ChunkSize;
			int64_t sy = sx + sqr<int64_t>(dy - MoonCenter.y);
			if (sy > 0) continue;

			FOR(z, ChunkSize)
			{
				int dz = z + mc.cpos.z * ChunkSize;
				int64_t sz = sy + sqr<int64_t>(dz - MoonCenter.z);
				if (sz > 0) continue;
				mc.set(glm::ivec3(x, y, z), heightmap->Color(dx, dy));
			}
		}
	}

	// crater
	FOR(x, ChunkSize)
	{
		int64_t sx = sqr<int64_t>(x + mc.cpos.x * ChunkSize - CraterCenter.x) - sqr<int64_t>(CraterRadius);
		if (sx > 0) continue;

		FOR(y, ChunkSize)
		{
			int64_t sy = sx + sqr<int64_t>(y + mc.cpos.y * ChunkSize - CraterCenter.y);
			if (sy > 0) continue;

			FOR(z, ChunkSize)
			{
				int64_t sz = sy + sqr<int64_t>(z + mc.cpos.z * ChunkSize - CraterCenter.z);
				if (sz > 0 || z + ChunkSize * mc.cpos.z >= 100) continue;
				mc.set(glm::ivec3(x, y, z), Empty);
			}
		}
	}
}

// ============================

struct Map
{
	Map()
	{
		FOR(i, MapSize) m_map[i] = nullptr;
		FOR(x, 4) FOR(y, 4) FOR(z, 4) set(glm::ivec3(x*2, y*2, z*2+45), Color(x, y, z, 3));
	}

	MapChunk& get(glm::ivec3 cpos)
	{
		int h = index(cpos);
		MapChunk* head = m_map[h].load(std::memory_order_relaxed);
		for (MapChunk* i = head; i; i = i->next) if (i->cpos == cpos) return *i;

		std::lock_guard<std::mutex> _(m_lock[(cpos.x ^ cpos.y ^ cpos.z) & 63]);
		MapChunk* head2 = m_map[h].load(std::memory_order_relaxed);
		for (MapChunk* i = head2; i != head; i = i->next) if (i->cpos == cpos) return *i;
		MapChunk* p = new MapChunk;
		p->cpos = cpos;
		p->next = head2;
		GenerateTerrain(*p);
		m_map[h] = p;
		return *p;
	}

	MapChunk* try_get(glm::ivec3 cpos)
	{
		int h = index(cpos);
		MapChunk* head = m_map[h].load(std::memory_order_relaxed);
		for (MapChunk* i = head; i; i = i->next) if (i->cpos == cpos) return i;
		return nullptr;
	}

	void set(glm::ivec3 pos, Block block)
	{
		get(pos >> ChunkSizeBits).set(pos & ChunkSizeMask, block);
	}

private:
	// TODO: Z-order?
	int index(glm::ivec3 a) const { a &= 127; return (a.x * 128 + a.y) * 128 + a.z; }

	static const int MapSize = 128 * 128 * 128;
	std::atomic<MapChunk*> m_map[MapSize];
	std::mutex m_lock[64];
};

Map* map = new Map;

Block map_get(glm::ivec3 pos) { return map->get(pos >> ChunkSizeBits)[pos & ChunkSizeMask]; }
Block map_get(glm::ivec3 pos, MapChunk& hint)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	MapChunk& mc = (cpos == hint.cpos) ? hint : map->get(cpos);
	return mc[pos & ChunkSizeMask];
}

// ============================

struct Quad
{
	glm::u8vec3 pos[4];
	uint8_t color;
	uint8_t light;
	uint16_t plane;
};

bool operator<(const Quad& a, const Quad& b)
{
	if (a.color != b.color) return a.color < b.color;
	return a.plane < b.plane;
}

struct Vertex
{
	GLubyte color;
	GLubyte light;
	glm::u8vec2 uv;
	glm::u8vec3 pos;
};

const float BlockRadius = sqrtf(3) / 2;

struct VisibleChunks
{
	VisibleChunks() { set.clear(); }

	void add(glm::ivec3 v)
	{
		if (set.xset(v & MapSizeMask)) array.push_back(v);
	}

	void cleanup(glm::ivec3 center, glm::vec3 direction)
	{
		int w = 0;
		for (glm::ivec3 c : array)
		{
			if (glm::dot(direction, glm::vec3(c - center)) < 0 || glm::distance2(c, center) > sqr(RenderDistance))
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

	void sort(glm::ivec3 center)
	{
		auto cmp = [center](glm::ivec3 a, glm::ivec3 b) { return glm::distance2(a, center) > glm::distance2(b, center); };
		std::sort(array.begin(), array.end(), cmp);
	}

	glm::ivec3* begin() { return &array[0]; }
	glm::ivec3* end() { return begin() + array.size(); }

private:
	BitCube<MapSize> set;
	std::vector<glm::ivec3> array;
};

struct BlockRenderer
{
	std::vector<Quad> m_quads;
	glm::ivec3 m_pos;
	GLubyte m_color;

	std::vector<Quad> m_xy, m_yx;

	void draw_quad(int a, int b, int c, int d, int face);
	void render(MapChunk& mc, glm::ivec3 pos, Block bs);

	static bool combine(Quad& a, Quad b, int X, int Y)
	{
		if (a.pos[0][X] == b.pos[0][X] && a.pos[2][X] == b.pos[2][X] && a.pos[2][Y] == b.pos[0][Y])
		{
			a.pos[2] = b.pos[2];
			if (a.pos[1][Y] == b.pos[0][Y]) a.pos[1] = b.pos[1];
			if (a.pos[3][Y] == b.pos[0][Y]) a.pos[3] = b.pos[3];
			return true;
		}
		return false;
	}

	static void merge_axis(std::vector<Quad>& quads, int X, int Y)
	{
		std::sort(quads.begin(), quads.end(), [X, Y](Quad a, Quad b) { return a.pos[0][X] < b.pos[0][X] || (a.pos[0][X] == b.pos[0][X] && a.pos[0][Y] < b.pos[0][Y]); });
		int w = 0;
		for (Quad q : quads)
		{
			if (w == 0 || !combine(quads[w-1], q, X, Y)) quads[w++] = q;
		}
		quads.resize(w);
	}

	void merge_quads(std::vector<Vertex>& vertices)
	{
		std::sort(m_quads.begin(), m_quads.end());
		auto a = m_quads.begin(), w = a;
		while (a != m_quads.end())
		{
			auto b = a + 1;
			while (b != m_quads.end() && a->color == b->color && a->plane == b->plane) b += 1;

			if (a->color == Leaves)
			{
				while (a < b) *w++ = *a++;
				continue;
			}

			m_xy.clear();
			m_yx.clear();
			while (a < b)
			{
				m_xy.push_back(*a);
				m_yx.push_back(*a);
				a += 1;
			}

			int axis = m_xy[0].plane >> 9;
			int x = (axis == 0) ? 1 : 0;
			int y = (axis == 2) ? 1 : 2;

			merge_axis(m_xy, x, y);
			merge_axis(m_xy, y, x);
			merge_axis(m_yx, y, x);
			merge_axis(m_yx, x, y);

			if (m_xy.size() > m_yx.size()) std::swap(m_xy, m_yx);
			w = std::copy(m_xy.begin(), m_xy.end(), w);
		}
		m_quads.resize(w - m_quads.begin());

		vertices.resize(6 * m_quads.size());
		auto v = vertices.begin();
		for (Quad q : m_quads)
		{
			uint tv = non_zero(q.pos[1] - q.pos[0]);
			uint tu = non_zero(q.pos[3] - q.pos[0]);
			*v++ = { q.color, q.light, glm::u8vec2( 0,  0), q.pos[0] };
			*v++ = { q.color, q.light, glm::u8vec2( 0, tv), q.pos[1] };
			*v++ = { q.color, q.light, glm::u8vec2(tu, tv), q.pos[2] };
			*v++ = { q.color, q.light, glm::u8vec2( 0,  0), q.pos[0] };
			*v++ = { q.color, q.light, glm::u8vec2(tu, tv), q.pos[2] };
			*v++ = { q.color, q.light, glm::u8vec2(tu,  0), q.pos[3] };
		}
	}

	static uint non_zero(glm::u8vec3 a)
	{
		FOR(i, 3) if (a[i] != 0) return a[i];
		assert(false);
		return 0;
	}
};

struct Chunk
{
	Chunk() : cpos(0x80000000, 0, 0) { }

	void buffer(BlockRenderer& renderer)
	{
		renderer.m_quads.clear();
		MapChunk& mc = map->get(cpos);
		FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
		{
			Block block = mc[glm::ivec3(x, y, z)];
			if (block != Empty) renderer.render(mc, cpos * ChunkSize + glm::ivec3(x, y, z), block);
		}
		renderer.merge_quads(vertices);
		render_size = vertices.size();
	}

	int render()
	{
		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * vertices.size(), &vertices[0], GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, render_size);
		return render_size;
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

	friend class Chunks;
private:
	std::mutex mutex;
	glm::ivec3 cpos; // TODO -> should be atomic -> pack into uint64 -> 21 bit per coordinate -> 25 effective
	std::vector<Vertex> vertices;
	int render_size;
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
	//float* m = glm::value_ptr(player::orientation);
	//glm::vec3 forward(m[4], m[5], m[6]);
	//glm::vec3 right(m[0], m[1], m[2]);
	glm::vec3 velocity;
	bool flying = true;
}

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
		static_assert(Threads == 1 || Threads == 2 || Threads == 4 || Threads == 8 || Threads == 16, "");
		return (cpos.x ^ cpos.y ^ cpos.z) & (Threads - 1);
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

struct BoundaryBlocks : public std::vector<glm::ivec3>
{
	BoundaryBlocks()
	{
		const int M = ChunkSize - 1;
		FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
		{
			if (x == 0 || x == M || y == 0 || y == M || z == 0 || z == M) push_back(glm::ivec3(x, y, z));
		}
	}
} boundary_blocks;

bool can_move_through(Block block) { return block == Empty; }
bool can_see_through(Block block) { return block == Empty || block == Leaves; }

struct Directions : public std::vector<glm::ivec3>
{
	enum { Bits = 9 };
	Directions()
	{
		const int M = 1 << Bits, N = M - 1;
		FOR2(x, 1, M) FOR2(y, 1, M) FOR2(z, 1, M)
		{
			int d2 = x*x + y*y + z*z;
			if (N*N < d2 && d2 <= M*M) for (int i : {-x, x}) for (int j : {-y, y})
			{
				push_back(glm::ivec3(i, j, z));
				push_back(glm::ivec3(i, j, -z));
			}
		}
		for (int m : {M, -M})
		{
			push_back(glm::ivec3(m, 0, 0));
			push_back(glm::ivec3(0, m, 0));
			push_back(glm::ivec3(0, 0, m));
		}
		FOR(i, size()) std::swap(operator[](std::rand() % size()), operator[](i));
	}
} directions;

int ray_it = 0;
int rays_remaining = 0;

VisibleChunks visible_chunks;

const int E = Directions::Bits + 1;

void raytrace(MapChunk* mc, glm::ivec3 photon, glm::ivec3 dir)
{
	const int MaxDist2 = sqr(RenderDistance * ChunkSize);
	const int B = ChunkSizeBits + E;
	glm::ivec3 svoxel = photon >> E, voxel = svoxel;
	while (true)
	{
		do photon += dir; while ((photon >> E) == voxel);
		glm::ivec3 cvoxel = photon >> B;
		if (mc->cpos != cvoxel)
		{
			mc = map->try_get(cvoxel);
			if (!mc) return;
			// Advance photon until it leaves empty chunk
			while (mc->count_empty == ChunkSize * ChunkSize * ChunkSize)
			{
				do photon += dir; while ((photon >> B) == cvoxel);
				voxel = photon >> E;
				if (glm::distance2(voxel, svoxel) > MaxDist2) return;
				cvoxel = photon >> B;
				mc = map->try_get(cvoxel);
				if (!mc) return;
			}
		}
		voxel = photon >> E;
		if (glm::distance2(voxel, svoxel) > MaxDist2) return;
		Block block = (*mc)[voxel & ChunkSizeMask];
		if (block == Empty) continue;
		visible_chunks.add(cvoxel);
		if (!can_see_through(block)) return;
	}
}

// which chunks must be rendered from the center chunk?
void update_render_list(glm::ivec3 cpos, Frustum& frustum)
{
	if (rays_remaining == 0) return;
	MapChunk& mc = map->get(cpos);

	float k = 1 << E;
	glm::ivec3 origin = glm::ivec3(glm::floor(player::position * k));

	int64_t budget = 40 / Timestamp::milisec_per_tick;
	Timestamp ta;

	int q = 0;
	while (rays_remaining > 0)
	{
		glm::ivec3 dir = directions[ray_it++];
		if (ray_it == directions.size()) ray_it = 0;
		rays_remaining -= 1;
		if (frustum.contains_point(player::position + glm::vec3(dir))) raytrace(&mc, origin, dir);
		if ((q++ % 2048) == 0 && ta.elapsed() > budget) break;
	}
	visible_chunks.sort(cpos);
}

namespace stats
{
	int vertex_count = 0;
	int chunk_count = 0;

	float frame_time_ms = 0;
	float render_time_ms = 0;
	float model_time_ms = 0;
	float raytrace_time_ms = 0;
}

// Model

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
bool selection = false;
bool wireframe = false;
bool show_counters = true;

void edit_block(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	glm::ivec3 p = pos & ChunkSizeMask;

	map->set(pos, block);
	static BlockRenderer renderer;
	g_chunks(cpos).reset(renderer);
	if (p.x == 0) g_chunks(cpos - ix).reset(renderer);
	if (p.y == 0) g_chunks(cpos - iy).reset(renderer);
	if (p.z == 0) g_chunks(cpos - iz).reset(renderer);
	if (p.x == ChunkSizeMask) g_chunks(cpos + ix).reset(renderer);
	if (p.y == ChunkSizeMask) g_chunks(cpos + iy).reset(renderer);
	if (p.z == ChunkSizeMask) g_chunks(cpos + iz).reset(renderer);
}

void on_edit_key(int key)
{
	Block block = map_get(sel_cube);
	int r = block % 4;
	int g = (block / 4) % 4;
	int b = (block / 16) % 4;
	int a = block % 64;

	if (key == GLFW_KEY_Z)
	{
		block = Color((r + 1) % 4, g, b, a);
		edit_block(sel_cube, block);
	}
	if (key == GLFW_KEY_X)
	{
		block = Color(r, (g + 1) % 4, b, a);
		edit_block(sel_cube, block);
	}
	if (key == GLFW_KEY_C)
	{
		block = Color(r, g, (b + 1) % 4, a);
		edit_block(sel_cube, block);
	}
	if (key == GLFW_KEY_V)
	{
		block = Color(r, g, b, (a + 1) % 4);
		edit_block(sel_cube, block);
	}
}

void on_key(GLFWwindow* window, int key, int scancode, int action, int mods)
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
			on_edit_key(key);
		}
	}
	else if (action == GLFW_REPEAT)
	{
		if (selection)
		{
			on_edit_key(key);
		}
	}
}

void on_mouse_button(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && selection)
	{
		edit_block(sel_cube, Empty);
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		edit_block(sel_cube + dir[sel_face], map_get(sel_cube));
	}
}

void model_init(GLFWwindow* window)
{
	glfwSetKeyCallback(window, on_key);
	glfwSetMouseButtonCallback(window, on_mouse_button);
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
int sphere_vs_cube(glm::vec3& center, float radius, glm::ivec3 cube, int neighbors)
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

int cube_neighbors(glm::ivec3 cube)
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

glm::vec3 gravity(glm::vec3 pos)
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

void simulate(float dt, glm::vec3 dir)
{
	if (!player::flying)
	{
		player::velocity += gravity(player::position) * dt;
	}
	player::position += dir * ((player::flying ? 20 : 10) * dt) + player::velocity * dt;

	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	FOR(i, 2)
	{
		// Resolve all collisions simultaneously
		glm::vec3 sum(0, 0, 0);
		int c = 0;
		FOR2(x, p.x - 3, p.x + 3) FOR2(y, p.y - 3, p.y + 3) FOR2(z, p.z - 3, p.z + 3)
		{
			glm::ivec3 cube(x, y, z);
			if (map_get(cube) != Empty && 1 == sphere_vs_cube(player::position, 0.9, cube, cube_neighbors(cube)))
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
		player::velocity = gravity(player::position) * -0.5f;
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
			simulate(dt, dir);
			break;
		}
		simulate(0.008, dir);
		dt -= 0.008;
	}
	if (p != player::position)
	{
		rays_remaining = directions.size();
		float* ma = glm::value_ptr(player::orientation);
		glm::vec3 direction(ma[4], ma[5], ma[6]);
		glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
		visible_chunks.cleanup(cplayer, direction);
	}
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
	assert(is_unit_length(dir));
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

Sphere select_sphere(10);

bool select_cube(glm::ivec3& sel_cube, int& sel_face)
{
	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	float* ma = glm::value_ptr(player::orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);

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

void model_frame(GLFWwindow* window, double delta_ms)
{
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
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
		player::orientation = glm::rotate(glm::rotate(glm::mat4(), -player::yaw, glm::vec3(0, 0, 1)), -player::pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective, player::pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, player::yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
		rays_remaining = directions.size();
	}

	if (!console.IsVisible())
	{
		model_move_player(window, delta_ms * 1e-3);
	}

	selection = select_cube(/*out*/sel_cube, /*out*/sel_face);
}

// Render

GLuint block_texture;
glm::vec3 light_direction(0.5, 1, -1);

GLubyte light_cache[8][8][8];

Initialize
{
	FOR(a, 8) FOR(b, 8) FOR(c, 8)
	{
		glm::vec3 normal = glm::normalize(glm::cross(glm::vec3(Cube::corner[b] - Cube::corner[a]), glm::vec3(Cube::corner[c] - Cube::corner[a])));
		float cos_angle = std::max<float>(0, -glm::dot(normal, light_direction));
		float light = glm::clamp<float>(0.3f + cos_angle * 0.7f, 0, 1);
		light_cache[a][b][c] = (uint) floorf(glm::clamp<float>(light * 256, 0, 255));
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

GLuint block_buffer;
GLuint line_buffer;

int g_tick;

void load_noise_texture(int size)
{
	GLubyte* image = new GLubyte[size * size * 3];
	FOR(x, size) FOR(y, size)
	{
		glm::vec2 p = glm::vec2(x+32131, y+908012) * (1.0f / size);
		float f = SimplexNoise(p * 32.f, 10, 1 / sqrt(2), 1, false) * 3;
		f = fabs(f);
		f = std::min<float>(f, 1);
		f = std::max<float>(f, -1);
		GLubyte a = (int) std::min<float>(255, floorf(f * 256));
		image[(x + y * size) * 3] = a;

		p = glm::vec2(x+9420234, y+9808312) * (1.0f / size);
		f = SimplexNoise(p * 32.f, 10, 1 / sqrt(2), 1, false) * 3;
		f = fabs(f);
		f = std::min<float>(f, 1);
		f = std::max<float>(f, -1);
		a = (int) std::min<float>(255, floorf(f * 256));
		image[(x + y * size) * 3 + 1] = a;

		p = glm::vec2(x+983322, y+1309329) * (1.0f / size);
		f = SimplexNoise(p * 32.f, 10, 1 / sqrt(2), 1, false) * 3;
		f = fabs(f);
		f = std::min<float>(f, 1);
		f = std::max<float>(f, -1);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	//load_noise_texture(512);
	load_png_texture("noise.png");

	light_direction = glm::normalize(light_direction);

	perspective = glm::perspective<float>(M_PI / 180 * 90, width / (float)height, 0.03, 1000);
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

	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &line_buffer);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glClearColor(0.2, 0.4, 1, 1.0);
	glViewport(0, 0, width, height);

	g_chunks.start_threads();
}

void BlockRenderer::draw_quad(int a, int b, int c, int d, int face)
{
	Quad q;
	q.color = m_color;
	q.light = light_cache[a][b][c];
	glm::ivec3 w = m_pos & ChunkSizeMask;
	q.pos[0] = glm::u8vec3(w + Cube::corner[a]);
	q.pos[1] = glm::u8vec3(w + Cube::corner[b]);
	q.pos[2] = glm::u8vec3(w + Cube::corner[c]);
	q.pos[3] = glm::u8vec3(w + Cube::corner[d]);
	q.plane = (face << 8) | q.pos[0][face / 2];
	m_quads.push_back(q);
}

// every bit from 0 to 7 in block represents once vertex (can be off or on)
void BlockRenderer::render(MapChunk& mc, glm::ivec3 pos, Block block)
{
	m_pos = pos;
	m_color = block;

	if (can_see_through(map_get(pos - ix, mc))) draw_quad(0, 4, 6, 2, 0);
	if (can_see_through(map_get(pos + ix, mc))) draw_quad(1, 3, 7, 5, 1);
	if (can_see_through(map_get(pos - iy, mc))) draw_quad(0, 1, 5, 4, 2);
	if (can_see_through(map_get(pos + iy, mc))) draw_quad(2, 6, 7, 3, 3);
	if (can_see_through(map_get(pos - iz, mc))) draw_quad(0, 2, 3, 1, 4);
	if (can_see_through(map_get(pos + iz, mc))) draw_quad(4, 5, 7, 6, 5);
}

void render_world_blocks(const glm::mat4& matrix, const Frustum& frustum)
{
	glBindTexture(GL_TEXTURE_2D, block_texture);
	if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	Auto(if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));

	glUseProgram(block_program);
	glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform3fv(block_eye_loc, 1, glm::value_ptr(player::position));
	glUniform1i(block_tick_loc, g_tick++);

	glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
	glEnableVertexAttribArray(block_position_loc);
	glEnableVertexAttribArray(block_color_loc);
	glEnableVertexAttribArray(block_light_loc);
	glEnableVertexAttribArray(block_uv_loc);

	glVertexAttribIPointer(block_position_loc, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->pos);
	glVertexAttribIPointer(block_color_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->color);
	glVertexAttribIPointer(block_light_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->light);
	glVertexAttribIPointer(block_uv_loc, 2, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->uv);

	stats::chunk_count = 0;
	stats::vertex_count = 0;

	for (glm::ivec3 cpos : visible_chunks)
	{
		if (g_chunks.loaded(cpos) && !frustum.is_sphere_outside(glm::vec3(cpos * ChunkSize + ChunkSize / 2), ChunkSize * BlockRadius))
		{
			glm::ivec3 pos = cpos * ChunkSize;
			glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
			stats::vertex_count += g_chunks(cpos).render();
			stats::chunk_count += 1;
		}
	}
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);

	text->Reset(width, height, matrix);
	if (show_counters)
	{
		int raytrace = std::round(100.0f * (directions.size() - rays_remaining) / directions.size());
		text->Printf("[%.1f %.1f %.1f] C:%4d T:%3dk frame:%2.0f model:%1.0f raytrace:%2.0f %d%% render %2.0f",
			 player::position.x, player::position.y, player::position.z, stats::chunk_count, stats::vertex_count / 3000,
			 stats::frame_time_ms, stats::model_time_ms, stats::raytrace_time_ms, raytrace, stats::render_time_ms);

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

	console.Render(text, glfwGetTime());

	glUseProgram(0);

	if (selection)
	{
		glUseProgram(line_program);
		glUniformMatrix4fv(line_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));

		glBindBuffer(GL_ARRAY_BUFFER, line_buffer);
		glEnableVertexAttribArray(line_position_loc);
		Auto(glDisableVertexAttribArray(line_position_loc));

		glVertexAttribPointer(line_position_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);

		glm::vec2 vline[4];
		vline[0] = glm::vec2(width/2 - 15, height / 2);
		vline[1] = glm::vec2(width/2 + 15, height / 2);
		vline[2] = glm::vec2(width/2, height / 2 - 15);
		vline[3] = glm::vec2(width/2, height / 2 + 15);

		glBufferData(GL_ARRAY_BUFFER, sizeof(vline), vline, GL_STREAM_DRAW);
		glDrawArrays(GL_LINES, 0, 4);
	}
}

void OnError(int error, const char* message)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, message);
}

double Timestamp::milisec_per_tick = 0;

int main(int, char**)
{
	if (!glfwInit()) return 0;

	glm::dvec3 a;
	glm::i64vec3 b;
	a.x = glfwGetTime();
	b.x = rdtsc();
	usleep(200000);
	a.y = glfwGetTime();
	b.y = rdtsc();
	usleep(200000);
	a.z = glfwGetTime();
	b.z = rdtsc();

	glfwSetErrorCallback(OnError);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode->width*2, mode->height*2, "Arena", glfwGetPrimaryMonitor(), NULL);
	if (!window) { glfwTerminate(); return 0; }
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1/*VSYNC*/);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);

	model_init(window);
	render_init();

	glm::dvec3 c;
	glm::i64vec3 d;
	c.x = glfwGetTime();
	d.x = rdtsc();
	usleep(200000);
	c.y = glfwGetTime();
	d.y = rdtsc();
	usleep(200000);
	c.z = glfwGetTime();
	d.z = rdtsc();
	Timestamp::init(a, b, c, d);

	Timestamp t0;
	while (!glfwWindowShouldClose(window))
	{
		Timestamp ta;
		double frame_ms = t0.elapsed_ms(ta);
		t0 = ta;
		glfwPollEvents();
		model_frame(window, frame_ms);
		glm::mat4 matrix = glm::translate(perspective_rotation, -player::position);
		Frustum frustum(matrix);
		glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
		player::cposx = cplayer.x;
		player::cposy = cplayer.y;
		player::cposz = cplayer.z;

		Timestamp tb;
		update_render_list(cplayer, frustum);

		Timestamp tc;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		render_world_blocks(matrix, frustum);
		glDisable(GL_DEPTH_TEST);
		render_gui();

		if (show_counters)
		{
			Timestamp td;
			stats::frame_time_ms = glm::mix<float>(stats::frame_time_ms, frame_ms, 0.15f);
			stats::model_time_ms = glm::mix<float>(stats::model_time_ms, ta.elapsed_ms(tb), 0.15f);
			stats::raytrace_time_ms = glm::mix<float>(stats::raytrace_time_ms, tb.elapsed_ms(tc), 0.15f);
			stats::render_time_ms = glm::mix<float>(stats::render_time_ms, tc.elapsed_ms(td), 0.15f);
		}

		glfwSwapBuffers(window);
	}
	glfwTerminate();
	return 0;
}
