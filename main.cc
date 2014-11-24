#include "ply_io.h"
#include <unordered_map>

#include "util.hh"
#include "algorithm.hh"
#include "rendering.hh"
#include "block.hh"
#include "auto.hh"
#include "socket.hh"
#include "parse.hh"
#include "message.hh"

#define LODEPNG_COMPILE_CPP
#include "lodepng/lodepng.h"

// GUI

int width;
int height;

// Map

const int RenderDistance = 40;
static_assert(RenderDistance < MapSize / 2, "");

Sphere render_sphere(RenderDistance);

// ============================

namespace Cube
{
	glm::ivec3 corner[8];
	const int faces[6][4] = { { 0, 4, 6, 2 }/*xmin*/, { 1, 3, 7, 5 }/*xmax*/, { 0, 1, 5, 4 }/*ymin*/, { 2, 6, 7, 3 }/*ymax*/, { 0, 2, 3, 1 }/*zmin*/, { 4, 5, 7, 6 }/*zmax*/ };
	glm::i8vec3 lightmap[6/*face*/][8/*vertex*/][4/*four blocks around vertex affecting light of vertex*/];
	glm::i8vec3 lightmap2[6/*face*/][8/*vertex*/][4*3/*three blocks around four blocks around vertex affecting light of vertex*/];

	Initialize
	{
		FOR(i, 8)
		{
			FOR(i, 8) corner[i] = glm::ivec3(i&1, (i>>1)&1, (i>>2)&1);
			FOR(f, 6)
			{
				glm::ivec3 max = corner[i], min = max - ii;
				if (f % 2 == 0) max[f / 2] -= 1;
				if (f % 2 == 1) min[f / 2] += 1;
				int p = 0;
				int q = 0;
				FOR2(x, min.x, max.x) FOR2(y, min.y, max.y) FOR2(z, min.z, max.z)
				{
					lightmap[f][i][p++] = glm::i8vec3(x, y, z);
					FOR(ff, 6) if (ff != (f ^ 1))
					{
						glm::ivec3 b = glm::ivec3(x, y, z) + face_dir[ff];
						if (!between(min, b, max)) lightmap2[f][i][q++] = glm::i8vec3(b);
					}
					assert(q == p * 3);
				}
			}
		}
	}
}

// ============================

struct Quad
{
	glm::u8vec3 pos[4]; // TODO: replace with x y w h
	BlockTexture texture; // highest bit: isUnderwater
	uint16_t light; // 4 bits per vertex
	uint16_t plane; // hi byte: plane normal (face), lo byte: plane offset
};

struct WQuad
{
	glm::u16vec3 pos[4]; // TODO: replace with x y w h
	BlockTexture texture; // highest bit: isUnderwater
	uint16_t light; // 4 bits per vertex
	uint16_t plane; // hi byte: plane normal (face), lo byte: plane offset
};

float distance(const Quad& q, glm::vec3 e)
{
	glm::vec3 a(q.pos[0]), c(q.pos[2]);
	return glm::distance2((a + c) * 0.5f, e);
}

bool operator<(const Quad& a, const Quad& b)
{
	if (a.texture != b.texture)
	{
		int ga = is_blended(a.texture) ? 1 : 0;
		int gb = is_blended(b.texture) ? 1 : 0;
		if (ga != gb) return ga < gb;
		return a.texture < b.texture;
	}
	if (a.plane != b.plane) return a.plane < b.plane;
	return a.light < b.light;
}

const float BlockRadius = sqrtf(3) / 2;

struct VisibleChunks
{
	VisibleChunks() : m_frame(0) { set.clear_all(); }

	void add(glm::ivec3 v)
	{
		if (set.xset(v & MapSizeMask))
		{
			Element e;
			e.cpos = v;
			e.frame = m_frame;
			array.push_back(e);
		}
	}

	void cleanup()
	{
		m_reset_frame = m_frame;
		set.clear_all();
	}

	void sort(glm::vec3 camera, bool done)
	{
		camera -= ii * ChunkSize / 2;
		camera /= ChunkSize;

		if (done)
		{
			int i = 0;
			while (i < array.size())
			{
				if (array[i].frame - m_frame <= m_reset_frame - m_frame)
				{
					array[i] = array.back();
					array.pop_back();
					continue;
				}
				i += 1;
			}

			for (Element& e : array) e.distance = glm::distance2(glm::vec3(e.cpos), camera);
			std::sort(array.begin(), array.end());
		}
		else
		{
			for (Element& e : array) e.distance = glm::distance2(glm::vec3(e.cpos), camera);
			std::sort(array.begin(), array.end());

			// remove duplicate array entries
			uint max_age = (m_reset_frame - m_frame + 10);
			int w = 0;
			for (int i = 0; i < array.size(); i++)
			{
				int j = i + 1;
				uint frame = array[i].frame;
				while (j < array.size() && array[i].cpos == array[j].cpos)
				{
					if (array[j].frame - m_frame < frame - m_frame) frame = array[j].frame;
					j += 1;
				}
				if (frame - m_frame <= max_age)
				{
					array[w].cpos = array[i].cpos;
					array[w].frame = frame;
					w += 1;
				}
				i = j - 1;
			}
			array.resize(w);
		}

		m_frame -= 1;
	}

	struct Element
	{
		glm::ivec3 cpos;
		float distance;
		uint frame;
		bool operator<(const Element& b) const { return distance > b.distance; }
	};

	Element* begin() { return &array[0]; }
	Element* end() { return begin() + array.size(); }

private:
	uint m_reset_frame;
	uint m_frame;
	BitCube<MapSize> set;
	std::vector<Element> array;
};

struct Chunk;

bool enable_f4 = true;
bool enable_f5 = true;
bool enable_f6 = true;

uint8_t Light(const Quad& q, int i)
{
	int s = (i % 4) * 2;
	int a = (q.light >> s) & 3;
	return (a + 1) * 64 - 1;
}

uint8_t Light2(const Quad& q, int i)
{
	int s = (i % 4) * 4;
	int a = (q.light >> s) & 15;
	return (a + 1) * 16 - 1;
}

struct BlockRenderer
{
	std::vector<Quad>* m_quads;
	std::vector<Quad> m_quadsp;
	glm::ivec3 m_pos;
	Block m_block;

	std::vector<Quad> m_xy, m_yx;

	// V1
	glm::ivec3 m_cxpos;
	const Blocks** m_chunks; // 3x3x3 cube

	// V2
	// Idea: if mapchunks are shifted 8 blocks on each axis then each render chunk would only depend on 2x2x2 mapchunks (8 instead of 27)
	// Problem: how to parallelize loading when player moves?
	// - worker threads only load superchunks and generate chunks (if mapchunk is not ready by buffer time, just use pointer to zero memory or full with some special block types)
	// - renderchunks are generated by main thread on demand (if chunk is going to be rendered and is dirty)
	// How does it affect the current and replacement raytracers?/
	//MapChunkLight m_mcl[3][3][3];

	/*Block get_v2(glm::ivec3 rel)
	{
		glm::ivec3 a = m_fpos + rel;
		glm::ivec3 b = (a - corner) >> ChunkSizeBits;
		return m_mcl[b.x][b.y][b.z].get(a & ChunkSizeMask);
	}*/

	Block get(glm::ivec3 rel)
	{
		glm::ivec3 a = m_pos + rel;
		glm::ivec3 b = a >> ChunkSizeBits;
		assert(-1 <= b.x && b.x <= 1);
		assert(-1 <= b.y && b.y <= 1);
		assert(-1 <= b.z && b.z <= 1);
		int i = b.x*9 + b.y*3 + b.z + 13; // ((b.x+1)*3 + b.y+1)*3 + b.z+1
		assert((uint)i < 27u);
		return m_chunks[i] ? (*m_chunks[i])[(a + m_cxpos) & ChunkSizeMask] : Block::none;
	}

	uint8_t face_light(int face)
	{
		const int* f = Cube::faces[face];
		int q = 0;
		FOR(i, 4)
		{
			glm::i8vec3* map = Cube::lightmap[face][f[i]];
			int s = 0;
			FOR(j, 4) if (get(glm::ivec3(map[j])) == Block::none) s += 1;
			q |= (s - 1) << (i * 2);
		}
		return q;
	}

	uint16_t face_light2(int face)
	{
		const int* f = Cube::faces[face];
		int q = 0;
		FOR(i, 4)
		{
			glm::i8vec3* map = Cube::lightmap[face][f[i]];
			glm::i8vec3* map2 = Cube::lightmap2[face][f[i]];
			int s = 0;
			FOR(j, 4)
			{
				Block b = get(glm::ivec3(map[j]));
				if (can_see_through(b))
				{
					s += (b == Block::none) ? 2 : 1;
					FOR(k, 3)
					{
						Block b2 = get(glm::ivec3(map2[j*3+k]));
						if (can_see_through(b2)) s += (b2 == Block::none) ? 2 : 1;
					}
				}
			}
			s = s / 2;
			q |= (s - 1) << (i * 4);
		}
		return q;
	}

	void draw_quad(int face, bool reverse, bool underwater_overlay);
	void draw_quad(int face, int zmin, int zmax, bool reverse, bool underwater_overlay);

	Block get(int face) { return get(face_dir[face]); }

	template<bool side>
	void draw_non_water_face(int face)
	{
		Block q = get(face);
		if (side && is_water(q))
		{
			if (q == Block::water)
			{
				draw_quad(face, false, true);
			}
			else if (q != m_block)
			{
				draw_quad(face, 0, water_level(q), false, true);
				draw_quad(face, water_level(q), 15, false, false);
			}
			return;
		}
		if (!side && q == Block::water)
		{
			draw_quad(face, false, true);
			return;
		}
		if (can_see_through(q) && q != m_block)
		{
			draw_quad(face, false, false);
		}
	}

	void draw_water_side(int face, int w)
	{
		Block q = get(face);
		if (is_water_partial(q))
		{
			if (w > water_level(q))
			{
				draw_quad(face, water_level(q), w, false, false);
				draw_quad(face, water_level(q), w, true, false);
			}
		}
		else if (can_see_through_non_water(q))
		{
			draw_quad(face, 0, w, false, false);
			draw_quad(face, 0, w, true, false);
		}
	}

	void draw_water_bottom()
	{
		Block q = get(/*m_pos.z != CMin, 4,*/ -iz);
		if (q != Block::water && can_see_through(q)) draw_quad(4, false, false);
		if (q == Block::none) draw_quad(4, true, false);
	}

	void draw_water_top(int w)
	{
		if (m_block == Block::water)
		{
			Block q = get(/*m_pos.z != CMax, 5,*/ iz);
			if (can_see_through_non_water(q)) draw_quad(5, false, false);
			if (q == Block::none) draw_quad(5, true, false);
		}
		else
		{
			draw_quad(5, w, w, false, false);
			draw_quad(5, w, w, true, false);
		}
	}

	void generate_quads(glm::ivec3 cpos, const Blocks* chunks[27], bool merge, std::vector<Quad>& out, int& blended_quads)
	{
		out.clear();
		m_quadsp.clear();
		m_quads = merge ? &m_quadsp : &out;
		m_chunks = chunks;
		m_cxpos = cpos << ChunkSizeBits;
		const Blocks& mc = *chunks[13];
		FOR(z, ChunkSize) FOR(y, ChunkSize) FOR(x, ChunkSize)
		{
			glm::ivec3 p(x, y, z);
			Block block = mc[p];
			if (block == Block::none) continue;
			m_pos = p;
			m_block = block;

			if (is_water(block))
			{
				int w = uint(block) - uint(Block::water1) + 1;
				draw_water_side(0, w);
				draw_water_side(1, w);
				draw_water_side(2, w);
				draw_water_side(3, w);
				draw_water_bottom();
				draw_water_top(w);
			}
			else
			{
				draw_non_water_face<true>(0);
				draw_non_water_face<true>(1);
				draw_non_water_face<true>(2);
				draw_non_water_face<true>(3);
				draw_non_water_face<false>(4);
				draw_non_water_face<false>(5);
			}
		}
		if (merge) merge_quads(out);
		if (!merge) std::partition(out.begin(), out.end(), [](const Quad& q) { return !is_blended(q.texture); });
		blended_quads = 0;
		while (blended_quads < out.size() && is_blended(out[out.size() - 1 - blended_quads].texture)) blended_quads += 1;
	}

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

	void merge_quads(std::vector<Quad>& out)
	{
		std::sort(m_quadsp.begin(), m_quadsp.end());
		auto a = m_quadsp.begin();
		while (a != m_quadsp.end())
		{
			auto b = a + 1;
			while (b != m_quadsp.end() && a->texture == b->texture && a->plane == b->plane && a->light == b->light) b += 1;

			if (is_leaves(a->texture))
			{
				while (a < b) out.push_back(*a++);
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
			for (Quad& q : m_xy)
			{
				out.push_back(q);
			}
		}
	}
};

typedef uint64_t CompressedIVec3;

uint64_t compress(int v, int bits)
{
	uint64_t c = v;
	return c & ((1lu << bits) - 1);
}

int decompress(uint64_t c, int bits)
{
	int64_t q = c << (64 - bits);
	return q >> (64 - bits);
}

CompressedIVec3 compress_ivec3(glm::ivec3 v)
{
	uint64_t c = 0;
	c |= compress(v.x, 21);
	c <<= 21;
	c |= compress(v.y, 21);
	c <<= 21;
	c |= compress(v.z, 21);
	return c;
}

glm::ivec3 decompress_ivec3(CompressedIVec3 c)
{
	glm::ivec3 v;
	v.z = decompress(c, 21);
	c >>= 21;
	v.y = decompress(c, 21);
	c >>= 21;
	v.x = decompress(c, 21);
	return v;
}

// ===============

class MyConsole : public Console
{
public:
	virtual void Execute(const char* command, int length) override;
};

MyConsole console;

struct Player
{
	// Persisted
	glm::vec3 position;
	float yaw, pitch;
	glm::vec3 velocity;
	bool creative_mode;
	Block palette_block;
	// TODO: active tool
	// TODO: inventory items
	// TODO: health
	// TODO: armor

	// Non-persisted
	glm::ivec3 cpos;
	std::atomic<CompressedIVec3> atomic_cpos;
	glm::mat4 orientation;
	bool broadcasted;

	bool digging_on;
	Timestamp digging_start;
	glm::ivec3 digging_block;

	bool load();
	bool save();

	void start_digging(glm::ivec3 cube);
	void turn(float dx, float dy);
};

bool g_collision = true;

Player g_player;
double scroll_y = 0, scroll_dy = 0;

void Player::turn(float dx, float dy)
{
	yaw += dx;
	pitch += dy;
	if (pitch > M_PI / 2 * 0.999) pitch = M_PI / 2 * 0.999;
	if (pitch < -M_PI / 2 * 0.999) pitch = -M_PI / 2 * 0.999;
	orientation = glm::rotate(glm::rotate(glm::mat4(), -yaw, glm::vec3(0, 0, 1)), -pitch, glm::vec3(1, 0, 0));
	broadcasted = false;
}

void Player::start_digging(glm::ivec3 cube)
{
	digging_on = true;
	digging_block = cube;
	digging_start = Timestamp();
}

json_t* json_vec3(glm::vec3 a)
{
	json_t* v = json_array();
	json_array_append(v, json_real(a.x));
	json_array_append(v, json_real(a.y));
	json_array_append(v, json_real(a.z));
	return v;
}

bool Player::save()
{
	json_t* doc = json_object();
	Auto(json_decref(doc));
	json_object_set(doc, "position", json_vec3(position));
	json_object_set(doc, "yaw", json_real(yaw));
	json_object_set(doc, "pitch", json_real(pitch));
	json_object_set(doc, "velocity", json_vec3(velocity));
	json_object_set(doc, "creative_mode", json_boolean(creative_mode));
	json_object_set(doc, "palette_block", json_integer((int)palette_block - (int)Block::water));
	json_object_set(doc, "console", console.save());
	CHECK(0 == json_dump_file(doc, "player.json", JSON_INDENT(4) | JSON_PRESERVE_ORDER));
	return true;
}

bool json_read(json_t* a, float& out)
{
	CHECK(json_is_real(a));
	out = json_real_value(a);
	return true;
}

bool json_read(json_t* a, glm::vec3& out)
{
	CHECK(json_is_array(a));
	CHECK(json_array_size(a) == 3);
	CHECK(json_read(json_array_get(a, 0), out.x));
	CHECK(json_read(json_array_get(a, 1), out.y));
	CHECK(json_read(json_array_get(a, 2), out.z));
	return true;
}

bool json_read(json_t* a, int64_t& out)
{
	CHECK(json_is_integer(a));
	out = json_integer_value(a);
	return true;
}

bool json_read(json_t* a, bool& out)
{
	CHECK(json_is_boolean(a));
	out = json_boolean_value(a);
	return true;
}

bool Player::load()
{
	// load defaults
	position = glm::vec3(0, 0, 20);
	yaw = 0;
	pitch = 0;
	velocity = glm::vec3(0, 0, 0);
	creative_mode = true;
	palette_block = Block::water;

	FILE* file = fopen("player.json", "r");
	CHECK(file || errno == ENOENT);
	if (file)
	{
		Auto(fclose(file));

		json_error_t error;
		json_t* doc = json_loadf(file, 0, &error);
		if (!doc)
		{
			fprintf(stderr, "JSON error: line=%d column=%d position=%d source='%s' text='%s'\n", error.line, error.column, error.position, error.source, error.text);
			return false;
		}
		assert(doc->refcount == 1);
		Auto(json_decref(doc));

		CHECK(json_is_object(doc));
		#define read(K, V) { json_t* value = json_object_get(doc, K); CHECK(value); CHECK(json_read(value, V)); }

		read("position", position);
		read("yaw", yaw);
		read("pitch", pitch);
		read("velocity", velocity);
		read("creative_mode", creative_mode);
		CHECK(console.load(json_object_get(doc, "console")));

		int64_t pb;
		read("palette_block", pb);
		CHECK(pb >= 0 && pb < block_count - (int)Block::water);
		palette_block = (Block)(pb + (int)Block::water);

		#undef read
	}

	scroll_y = (uint)palette_block - (uint)Block::water;
	orientation = glm::rotate(glm::rotate(glm::mat4(), -yaw, glm::vec3(0, 0, 1)), -pitch, glm::vec3(1, 0, 0));
	cpos = glm::ivec3(glm::floor(position)) >> ChunkSizeBits;
	atomic_cpos = compress_ivec3(cpos);
	digging_on = false;
	broadcasted = false;
	return true;
}

// ===============

struct Mesh
{
	bool load(const char* filename);
	void bounding_box(glm::vec3& min, glm::vec3& max) const;

	struct Face
	{
		uint verts[3];
	};

	std::vector<glm::vec3> vertices;
	std::vector<Face> faces;
};

bool Mesh::load(const char* filename)
{
	FILE* f = fopen(filename, "r");
	PlyFile* pf = read_ply(f);

	PlyProperty vert_prop_x = { const_cast<char*>("x"), Float32, Float32, offsetof(glm::vec3, x), 0, 0, 0, 0 };
	PlyProperty vert_prop_y = { const_cast<char*>("y"), Float32, Float32, offsetof(glm::vec3, y), 0, 0, 0, 0 };
	PlyProperty vert_prop_z = { const_cast<char*>("z"), Float32, Float32, offsetof(glm::vec3, z), 0, 0, 0, 0 };
	PlyProperty face_prop = { const_cast<char*>("vertex_indices"), Int32, Int32, offsetof(Face, verts), 3/*LIST_FIXED*/, Uint8, Uint8, 3/*num verts*/ };

	CHECK(pf->num_elem_types == 2);

	// Read vertex list
	int count;
	char* elem_name = setup_element_read_ply(pf, 0, &count);
	CHECK(strcmp(elem_name, "vertex") == 0);
	CHECK(count >= 0);
	CHECK(pf->elems[0]->nprops == 3);
	CHECK(strcmp(pf->elems[0]->props[0]->name, "x") == 0);
	CHECK(strcmp(pf->elems[0]->props[1]->name, "y") == 0);
	CHECK(strcmp(pf->elems[0]->props[2]->name, "z") == 0);
	CHECK(pf->elems[0]->props[0]->is_list == 0);
	CHECK(pf->elems[0]->props[1]->is_list == 0);
	CHECK(pf->elems[0]->props[2]->is_list == 0);
	setup_property_ply(pf, &vert_prop_x);
	setup_property_ply(pf, &vert_prop_y);
	setup_property_ply(pf, &vert_prop_z);
	vertices.resize(count);
	for (int i = 0; i < count; i++)
	{
		get_element_ply(pf, &vertices[i]);
		std::swap(vertices[i].y, vertices[i].z);
	}

	// Read face list
	elem_name = setup_element_read_ply(pf, 1, &count);
	CHECK(strcmp(elem_name, "face") == 0);
	CHECK(count >= 0);
	CHECK(pf->elems[1]->nprops == 1);
	CHECK(strcmp(pf->elems[1]->props[0]->name, "vertex_indices") == 0);
	CHECK(pf->elems[1]->props[0]->is_list == 1);
	setup_property_ply(pf, &face_prop);
	faces.resize(count);
	for (int i = 0; i < count; i++)
	{
		Face& face = faces[i];
		get_element_ply(pf, &face);
		CHECK(pf->error == 0);
	}

	free_ply(pf);
	fclose(f);
	return true;
}

void Mesh::bounding_box(glm::vec3& min, glm::vec3& max) const
{
	min = max = vertices[0];
	for (uint i = 1; i < vertices.size(); i++)
	{
		glm::vec3 v = vertices[i];
		if (v.x < min.x) min.x = v.x;
		if (v.y < min.y) min.y = v.y;
		if (v.z < min.z) min.z = v.z;
		if (v.x > max.x) max.x = v.x;
		if (v.y > max.y) max.y = v.y;
		if (v.z > max.z) max.z = v.z;
	}
}

// ===============

struct Chunk
{
	Chunk() : m_cpos(x_bad_ivec3) { }

	Block get(glm::ivec3 a) const { return m_blocks[a]; }
	const Block* getp(glm::ivec3 a) const { return m_blocks.getp(a); }
	const Blocks& blocks() const { return m_blocks; }
	bool empty() const { return m_empty; }

	void update_empty()
	{
		m_empty = true;
		FOR(i, ChunkSize3) if (m_blocks.data()[i] != Block::none)
		{
			m_empty = false;
			break;
		}
	}

	void sort(glm::vec3 camera)
	{
		if (m_blended_quads > 0)
		{
			camera -= get_cpos() << ChunkSizeBits;
			auto cmp = [camera](const Quad& a, const Quad& b) { return distance(a, camera) > distance(b, camera); };
			std::sort(m_quads.end() - m_blended_quads, m_quads.end(), cmp);
		}
	}

	void remesh(BlockRenderer& renderer, const Blocks* chunks[27])
	{
		renderer.generate_quads(get_cpos(), chunks, true/*!m_active*/, m_quads, m_blended_quads);
		m_remesh = false;
	}

	int render()
	{
		glBufferData(GL_ARRAY_BUFFER, sizeof(Quad) * m_quads.size(), &m_quads[0], GL_STREAM_DRAW);
		glDrawArrays(GL_POINTS, 0, m_quads.size());
		return m_quads.size();
	}

	void init(glm::ivec3 cpos, Block blocks[ChunkSize3])
	{
		memcpy(m_blocks.data(), blocks, sizeof(Block) * ChunkSize3);
		update_empty();
		m_quads.clear();
		m_cpos = cpos;
		m_remesh = true;
	}

	glm::ivec3 get_cpos() { return m_cpos; }

	bool m_remesh;
	friend class Chunks;
private:
	bool m_empty;
	Blocks m_blocks;
	glm::ivec3 m_cpos;
	std::vector<Quad> m_quads;
	int m_blended_quads;
};

class Chunks
{
public:

	Chunks(): m_map(new Chunk[MapSize * MapSize * MapSize]) { }

	Block get_block(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		assert(chunk.get_cpos() == (pos >> ChunkSizeBits));
		return chunk.get(pos & ChunkSizeMask);
	}

	Block get_block(glm::ivec3 pos, Block def)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		if (chunk.get_cpos() != (pos >> ChunkSizeBits)) return def;
		return chunk.get(pos & ChunkSizeMask);
	}

	bool selectable_block(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		return chunk.get_cpos() == (pos >> ChunkSizeBits) && chunk.get(pos & ChunkSizeMask) != Block::none;
	}

	bool can_move_through(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		return chunk.get_cpos() == (pos >> ChunkSizeBits) && ::can_move_through(chunk.get(pos & ChunkSizeMask));
	}

	Chunk& get(glm::ivec3 cpos)
	{
		cpos &= MapSizeMask;
		return m_map[((cpos.x * MapSize) + cpos.y) * MapSize + cpos.z];
	}

	Chunk* get_opt(glm::ivec3 cpos)
	{
		Chunk& chunk = get(cpos);
		return (chunk.get_cpos() == cpos) ? &chunk : nullptr;
	}

private:
	Chunk* m_map; // Huge array in memory!
};

Chunks g_chunks;

// ======================

void server_main();
Socket g_client;
SocketBuffer g_recv_buffer;
SocketBuffer g_send_buffer;
bool g_fsync_ack;

void edit_block(glm::ivec3 pos, Block block)
{
	write_text_message(g_send_buffer, "block %d %d %d %d", pos.x, pos.y, pos.z, block);
}

// <scale> is size (in blocks) of the largest extent of mesh.
void rasterize_mesh(const Mesh& mesh, glm::ivec3 base, float scale, Block block)
{
	glm::vec3 min, max;
	mesh.bounding_box(/*out*/min, /*out*/max);
	glm::vec3 mesh_size = max - min;
	float extent = glm::max(mesh_size.x, mesh_size.y, mesh_size.z);
	float mesh_block = extent / scale; // size of block in model space

	glm::ivec3 bmax = base + glm::ivec3(glm::floor(mesh_size / mesh_block));

	FOR(i, mesh.faces.size())
	{
		const Mesh::Face& face = mesh.faces[i];
		glm::vec3 va = mesh.vertices[face.verts[0]];
		glm::vec3 vb = mesh.vertices[face.verts[1]];
		glm::vec3 vc = mesh.vertices[face.verts[2]];

		FOR(p, 3)
		{
			glm::vec3 v2 = glm::mix(va, vb, p * 0.5f);
			FOR(q, 3)
			{
				glm::vec3 v3 = glm::mix(v2, vc, q * 0.5f);
				glm::ivec3 pos = base + glm::ivec3(glm::floor((v3 - min) / mesh_block));
				edit_block(pos, block);
			}
		}
	}
}

// ======================

struct Direction
{
	glm::vec3 dir;
	glm::vec3 inv_dir;
};

struct Directions : public std::vector<Direction>
{
	enum { Bits = 9 };
	Directions()
	{
		const int M = 1 << Bits, N = M - 1;
		FOR2(x, 1, M) FOR2(y, 1, M) for(int z = 1; z <= M; z++)
		{
			int d2 = x*x + y*y + z*z;
			if (N*N < d2 && d2 <= M*M) for (int i : {-x, x}) for (int j : {-y, y})
			{
				add(i, j, z);
				add(i, j, -z);
			}
		}
		for (int m : {M, -M})
		{
			add(m, 0, 0);
			add(0, m, 0);
			add(0, 0, m);
		}
		FOR(i, size()) std::swap(operator[](std::rand() % size()), operator[](i));
	}
	void add(int x, int y, int z)
	{
		Direction e;
		e.dir = glm::normalize(glm::vec3(x, y, z));
		e.inv_dir = glm::abs(1.0f / e.dir);
		push_back(e);
	}
} directions;

int ray_it = 0;
int rays_remaining = 0;

VisibleChunks visible_chunks;

void raytrace(Chunk* chunk, glm::ivec3 pos, glm::ivec3 cpos, const Block* bp, glm::ivec3 id, glm::vec3 dd, glm::vec3 crossing)
{
	const float MaxDist = RenderDistance * ChunkSize;

	while (true)
	{
		if (crossing.x < crossing.y)
		{
			if (crossing.x < crossing.z)
			{
				if (crossing.x > MaxDist) return;
				pos.x += id.x;
				crossing.x += dd.x;
				if (cpos.x != (pos.x >> ChunkSizeBits)) { cpos.x = pos.x >> ChunkSizeBits; goto next_chunk; }
				bp += id.x;
			}
			else
			{
				if (crossing.z > MaxDist) return;
				pos.z += id.z;
				crossing.z += dd.z;
				if (cpos.z != (pos.z >> ChunkSizeBits)) { cpos.z = pos.z >> ChunkSizeBits; goto next_chunk; }
				bp += id.z * ChunkSize2;
			}
		}
		else
		{
			if (crossing.y < crossing.z)
			{
				if (crossing.y > MaxDist) return;
				pos.y += id.y;
				crossing.y += dd.y;
				if (cpos.y != (pos.y >> ChunkSizeBits)) { cpos.y = pos.y >> ChunkSizeBits; goto next_chunk; }
				bp += id.y * ChunkSize;
			}
			else
			{
				if (crossing.z > MaxDist) return;
				pos.z += id.z;
				crossing.z += dd.z;
				if (cpos.z != (pos.z >> ChunkSizeBits)) { cpos.z = pos.z >> ChunkSizeBits; goto next_chunk; }
				bp += id.z * ChunkSize2;
			}
		}

		resume:
		Block block = *bp;
		if (block != Block::none)
		{
			visible_chunks.add(cpos);
			if (!can_see_through(block)) return;
		}
	}

next_chunk:
	chunk = &g_chunks.get(cpos);
	if (chunk->get_cpos() != cpos) return;
	if (!chunk->empty())
	{
		bp = chunk->getp(pos & ChunkSizeMask);
		goto resume;
	}

	while (true)
	{
		if (crossing.x < crossing.y && crossing.x < crossing.z)
		{
			if (crossing.x > MaxDist) return;
			pos.x += id.x;
			crossing.x += dd.x;
			if (cpos.x != (pos.x >> ChunkSizeBits)) { cpos.x = pos.x >> ChunkSizeBits; goto next_chunk; }
		}
		else if (crossing.y < crossing.z)
		{
			if (crossing.y > MaxDist) return;
			pos.y += id.y;
			crossing.y += dd.y;
			if (cpos.y != (pos.y >> ChunkSizeBits)) { cpos.y = pos.y >> ChunkSizeBits; goto next_chunk; }
		}
		else
		{
			if (crossing.z > MaxDist) return;
			pos.z += id.z;
			crossing.z += dd.z;
			if (cpos.z != (pos.z >> ChunkSizeBits)) { cpos.z = pos.z >> ChunkSizeBits; goto next_chunk; }
		}
	}
}

// which chunks must be rendered from the center chunk?
void update_render_list(Frustum& frustum)
{
	if (rays_remaining == 0) return;

	glm::vec3 origin = g_player.position;
	glm::ivec3 pos = glm::ivec3(glm::floor(origin));
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	Chunk* chunk = &g_chunks.get(cpos);
	assert(chunk->get_cpos() == cpos);

	int64_t budget = 40 / Timestamp::milisec_per_tick;
	Timestamp ta;

	const Block* bp = chunk->getp(pos & ChunkSizeMask);

	glm::vec3 crossingA = glm::ceil(origin) - origin;
	glm::vec3 crossingB = origin - glm::floor(origin);

	int q = 0;
	while (rays_remaining > 0)
	{
		const Direction& e = directions[ray_it++];
		if (ray_it == directions.size()) ray_it = 0;
		rays_remaining -= 1;
		if (frustum.contains_point(g_player.position + e.dir))
		{
			glm::ivec3 id;
			glm::vec3 crossing;
			FOR(i, 3)
			{
				id[i] = (e.dir[i] > 0) ? 1 : -1;
				crossing[i] = (e.inv_dir[i] == INFINITY) ? INFINITY : ((e.dir[i] > 0 ? crossingA[i] : crossingB[i]) * e.inv_dir[i]);
			}
			raytrace(chunk, pos, cpos, bp, id, e.inv_dir, crossing);
		}
		if ((q++ % 1024) == 0 && ta.elapsed() > budget) break;
	}
	visible_chunks.sort(g_player.position, rays_remaining == 0);
}

namespace stats
{
	int quad_count = 0;
	int chunk_count = 0;

	float frame_time_ms = 0;
	float render_time_ms = 0;
	float model_time_ms = 0;
	float raytrace_time_ms = 0;

	float collide_time_ms = 0;
	float select_time_ms = 0;
	float simulate_time_ms = 0;
}

// Model

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
bool selection = false;
bool wireframe = false;
bool show_counters = true;

Timestamp ts_palette;
bool show_palette = false;

void mark_remesh_chunk(Chunk& chunk, glm::ivec3 pos, glm::ivec3 q);

void on_key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	show_palette = false;
	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE && mods == GLFW_MOD_SHIFT)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
		return;
	}

	if (console.IsVisible())
	{
		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			if (key == GLFW_KEY_F1)
			{
				console.Hide();
			}
			else
			{
				console.OnKey(key, mods);
			}
		}
	}
	else if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_F1)
		{
			console.Show();
		}
		else if (key == GLFW_KEY_F2)
		{
			show_counters = !show_counters;
		}
		else if (key == GLFW_KEY_F3)
		{
			wireframe = !wireframe;
		}
		else if (key == GLFW_KEY_F4)
		{
			enable_f4 = !enable_f4;
		}
		else if (key == GLFW_KEY_F5)
		{
			enable_f5 = !enable_f5;
		}
		else if (key == GLFW_KEY_F6)
		{
			enable_f6 = !enable_f6;
		}
		else if (key == GLFW_KEY_TAB)
		{
			g_player.creative_mode = !g_player.creative_mode;
			g_player.velocity = glm::vec3(0, 0, 0);
			g_player.digging_on = false;
		}
	}
}

void on_mouse_button(GLFWwindow* window, int button, int action, int mods)
{
	show_palette = false;

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && selection && g_player.creative_mode)
	{
		edit_block(sel_cube, Block::none);
	}

	if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT)
	{
		g_player.digging_on = false;
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		edit_block(sel_cube + dir[sel_face], g_player.palette_block);
	}
}

void on_scroll(GLFWwindow* window, double x, double y)
{
	ts_palette = Timestamp();
	show_palette = true;
	scroll_dy = y / 5;
	scroll_y += scroll_dy;
	if (scroll_y < 0) scroll_y = 0;
	if (scroll_y > (block_count - 1 - (uint)Block::water)) scroll_y = block_count - 1 - (uint)Block::water;
	g_player.palette_block = Block(uint(std::round(scroll_y)) + (uint)Block::water);
}

bool last_cursor_init = false;
double last_cursor_x, last_cursor_y;

void model_init(GLFWwindow* window)
{
	if (!g_player.load())
	{
		fprintf(stderr, "Failed to load player.json\n");
		exit(1);
	}
	glfwSetKeyCallback(window, on_key);
	glfwSetMouseButtonCallback(window, on_mouse_button);
	glfwSetScrollCallback(window, on_scroll);
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

int Resolve(glm::vec3& delta, float k, float x, float y, float z)
{
	glm::vec3 d(x, y, z);
	float ds = sqr(d);
	if (ds < 1e-6) return -1;
	delta = d * (k / sqrtf(ds) - 1);
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

int cuboid_vs_cube(glm::vec3 center, glm::vec3 radius, glm::ivec3 cube, glm::vec3& delta)
{
	float e = 0.5;
	float x = center.x - cube.x - e;
	float y = center.y - cube.y - e;
	float z = center.z - cube.z - e;

	glm::vec3 k = radius + e;

	if (x >= k.x || x <= -k.x || y >= k.y || y <= -k.y || z >= k.z || z <= -k.z) return 0;

	float dx = (x > 0 ? k.x : -k.x) - x;
	float dy = (y > 0 ? k.y : -k.y) - y;
	float dz = (z > 0 ? k.z : -k.z) - z;

	if (std::abs(dx) < std::abs(dy))
	{
		if (std::abs(dx) < std::abs(dz))
		{
			delta = glm::vec3(dx, 0, 0);
		}
		else
		{
			delta = glm::vec3(0, 0, dz);
		}
	}
	else
	{
		if (std::abs(dy) < std::abs(dz))
		{
			delta = glm::vec3(0, dy, 0);
		}
		else
		{
			delta = glm::vec3(0, 0, dz);
		}
	}
	return 1;
}

// Move cylinder so that it is not coliding with cube.
// Cylinder is always facing up.
//
// Neighbors is 3x3x3 (only 4 corners are used) matrix of bits representing nearby cubes.
// Used to turn off edges that are not exposed.
int cylinder_vs_cube(glm::vec3 center, float radius, float /*half*/height, glm::ivec3 cube, glm::vec3& delta, int neighbors)
{
	float e = 0.5;
	float x = center.x - cube.x - e;
	float y = center.y - cube.y - e;
	float z = center.z - cube.z - e;
	float kr = e + radius;
	float kh = e + height;

	if (x >= kr || x <= -kr || y >= kr || y <= -kr || z >= kh || z <= -kh) return 0;

	float s2 = sqr(radius + sqrtf(2) / 2);
	if (x*x + y*y >= s2) return 0;

	// Compute delta XY
	float dxy = 0;
	if (x > +e)
	{
		if (y > +e)
		{
			if (!IsEdgeFree(neighbors, 1, 1, 0) || !Resolve(delta, radius, x - e, y - e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else if (y < -e)
		{
			if (!IsEdgeFree(neighbors, 1, -1, 0) || !Resolve(delta, radius, x - e, y + e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else
		{
			delta = glm::vec3(kr - x, 0, 0);
			dxy = delta.x;
		}
	}
	else if (x < -e)
	{
		if (y > +e)
		{
			if (!IsEdgeFree(neighbors, -1, 1, 0) || !Resolve(delta, radius, x + e, y - e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else if (y < -e)
		{
			if (!IsEdgeFree(neighbors, -1, -1, 0) || !Resolve(delta, radius, x + e, y + e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else
		{
			delta = glm::vec3(-kr - x, 0, 0);
			dxy = -delta.x;
		}
	}
	else
	{
		if (y > +e)
		{
			delta = glm::vec3(0, kr - y, 0);
			dxy = delta.y;
		}
		else if (y < -e)
		{
			delta = glm::vec3(0, -kr - y, 0);
			dxy = -delta.y;
		}
		else
		{
			// move either by X or Y, depending which one is less
			float dx = (x > 0 ? kr : -kr) - x;
			float dy = (y > 0 ? kr : -kr) - y;
			if (std::abs(dx) < std::abs(dy))
			{
				delta = glm::vec3(dx, 0, 0);
				dxy = std::abs(dx);
			}
			else
			{
				delta = glm::vec3(0, dy, 0);
				dxy = std::abs(dy);
			}
		}
	}

	// Compute delta Z
	float dz = (z > 0 ? kh : -kh) - z;
	if (std::abs(dz) < dxy) delta = glm::vec3(0, 0, dz);
	return 1;
}

int cube_neighbors(glm::ivec3 cube)
{
	int neighbors = 0;
	FOR2(dx, -1, 1) FOR2(dy, -1, 1) FOR2(dz, -1, 1)
	{
		glm::ivec3 pos(cube.x + dx, cube.y + dy, cube.z + dz);
		if (!g_chunks.can_move_through(pos)) neighbors |= NeighborBit(dx, dy, dz);
	}
	return neighbors;
}

// TODO: simplify
int cube_neighbors2(glm::ivec3 cube)
{
	int neighbors = 0;
	FOR2(dx, -1, 1) FOR2(dy, -1, 1) if (dx != 0 && dy != 0)
	{
		glm::ivec3 pos(cube.x + dx, cube.y + dy, cube.z);
		if (!g_chunks.can_move_through(pos)) neighbors |= NeighborBit(dx, dy, 0);
	}
	return neighbors;
}

glm::vec3 gravity(glm::vec3 pos)
{
	return glm::vec3(0, 0, -30);

	/*glm::vec3 dir = glm::vec3(MoonCenter) - pos;
	double dist2 = glm::dot(dir, dir);
	double a = 10000000;

	if (dist2 > MoonRadius * MoonRadius)
	{
		return dir * (float)(a / (sqrt(dist2) * dist2));
	}
	else
	{
		return dir * (float)(a / (MoonRadius * MoonRadius * MoonRadius));
	}*/
}

const float player_acc = 35;
const float player_jump_dv = 15;
const float player_max_vel = 10;

bool on_the_ground = false;

void simulate(float dt, glm::vec3 dir, bool jump)
{
	if (g_player.creative_mode)
	{
		g_player.position += dir * (dt * 20);
	}
	else
	{
		glm::ivec3 p = glm::ivec3(glm::floor(g_player.position));
		bool underwater = g_chunks.get_block(p) == Block::water;

		if ((on_the_ground || underwater) && !jump)
		{
			if (dir.x != 0 || dir.y != 0 || dir.z != 0)
			{
				float d = glm::dot(dir, g_player.velocity);
				glm::vec3 a = dir * d;
				glm::vec3 b = g_player.velocity - a;
				float p = dt * player_acc;

				// Brake on side-velocity
				float b_len = glm::length(b);
				if (b_len <= p)
				{
					g_player.velocity -= b;
					p -= b_len;

					if (glm::length(a) > player_max_vel)
					{
						// Brake on velocity along running direction
						g_player.velocity -= glm::normalize(a) * p;
					}
					else
					{
						glm::vec3 v = g_player.velocity + dir * p;
						float v2 = glm::length2(v);
						if (v2 < glm::length2(g_player.velocity) || v2 < player_max_vel * player_max_vel) g_player.velocity = v;
					}
				}
				else
				{
					g_player.velocity -= glm::normalize(b) * p;
				}
			}
			else
			{
				float v = glm::length(g_player.velocity);
				if (v <= dt * player_acc)
				{
					g_player.velocity = glm::vec3(0, 0, 0);
				}
				else
				{
					g_player.velocity -= glm::normalize(g_player.velocity) * (dt * player_acc);
				}
			}
		}
		if (!underwater) g_player.velocity += gravity(g_player.position) * dt;
		g_player.position += g_player.velocity * dt;
	}

	on_the_ground = false;
	if (!g_collision && g_player.creative_mode) return;

	FOR(i, 2)
	{
		// Resolve all collisions simultaneously
		glm::ivec3 p = glm::ivec3(glm::floor(g_player.position));
		glm::vec3 sum(0, 0, 0);
		int c = 0;
		FOR2(x, p.x - 1, p.x + 1) FOR2(y, p.y - 1, p.y + 1) FOR2(z, p.z - 1, p.z + 1)
		{
			glm::ivec3 cube(x, y, z);
			if (g_chunks.can_move_through(cube)) continue;
			glm::vec3 delta;
			const float radius = 0.48;
			glm::vec3 q = g_player.position;
			if (true)
			{
				// q.z -= radius; BUG: unstable?
				if (1 == cylinder_vs_cube(q, radius, radius * 2, cube, /*out*/delta, cube_neighbors(cube)))
				{
					sum += delta;
					c += 1;
				}
			}
			else if (false)
			{
				if (1 == sphere_vs_cube(q, radius, cube, cube_neighbors(cube)))
				{
					sum += q - g_player.position;
					c += 1;
				}
			}
			else
			{
				// q.z -= radius; BUG: unstable?
				if (1 == cuboid_vs_cube(q, glm::vec3(radius, radius, radius * 2), cube, /*out*/delta))
				{
					sum += delta;
					c += 1;
				}
			}
		}
		if (c == 0)	break;
		float sum2 = glm::length2(sum);
		if (sum2 == 0) return;

		glm::vec3 normal = sum * glm::inversesqrt(sum2);
		float d = glm::dot(normal, g_player.velocity);
		if (d < 0) g_player.velocity -= d * normal;
		if (glm::dot(normal, glm::normalize(gravity(g_player.position))) < -0.9) on_the_ground = true;
		g_player.position += sum / (float)c;
	}
}

void model_move_player(GLFWwindow* window, float dt)
{
	glm::vec3 dir(0, 0, 0);

	float* m = glm::value_ptr(g_player.orientation);
	glm::vec3 forward(m[4], m[5], m[6]);
	glm::vec3 right(m[0], m[1], m[2]);

	if (!g_player.creative_mode)
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

	bool jump = false;
	if (!g_player.creative_mode && on_the_ground && glfwGetKey(window, GLFW_KEY_SPACE))
	{
		g_player.velocity += glm::normalize(-gravity(g_player.position)) * player_jump_dv;
		jump = true;
	}

	glm::vec3 p = g_player.position;
	if (dir.x != 0 || dir.y != 0 || dir.z != 0)
	{
		show_palette = false;
		dir = glm::normalize(dir);
	}
	while (dt > 0)
	{
		if (dt <= 0.008)
		{
			simulate(dt, dir, jump);
			break;
		}
		simulate(0.008, dir, jump);
		dt -= 0.008;
	}
	if (p != g_player.position)
	{
		rays_remaining = directions.size();
		g_player.broadcasted = false;
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
	glm::ivec3 p = glm::ivec3(glm::floor(g_player.position));
	float* ma = glm::value_ptr(g_player.orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);

	for (glm::ivec3 d : select_sphere)
	{
		glm::ivec3 cube = p + d;
		if (g_chunks.selectable_block(cube) && (sel_face = intersects_ray_cube(g_player.position, dir, cube)) != -1)
		{
			sel_cube = cube;
			return true;
		}
	}
	return false;
}

void model_orientation(GLFWwindow* window)
{
	double cursor_x, cursor_y;
	glfwGetCursorPos(window, &cursor_x, &cursor_y);
	if (!last_cursor_init)
	{
		last_cursor_init = true;
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
	}
	if (cursor_x != last_cursor_x || cursor_y != last_cursor_y)
	{
		show_palette = false;
		g_player.turn((cursor_x - last_cursor_x) / 150, (cursor_y - last_cursor_y) / 150);
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
		perspective_rotation = glm::rotate(perspective, g_player.pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, g_player.yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
		rays_remaining = directions.size();
	}
}

void model_digging(GLFWwindow* window)
{
	if (g_player.digging_on)
	{
		if (!selection)
		{
			g_player.digging_on = false;
			return;
		}

		if (sel_cube != g_player.digging_block)
		{
			g_player.start_digging(sel_cube);
		}
		else if (g_player.digging_start.elapsed_ms() > 2000)
		{
			edit_block(sel_cube, Block::none);
			g_player.digging_on = false;
			selection = select_cube(/*out*/sel_cube, /*out*/sel_face);
			if (selection) g_player.start_digging(sel_cube);
		}
	}
	else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS && selection)
	{
		g_player.start_digging(sel_cube);
	}
}

void model_frame(GLFWwindow* window, double delta_ms)
{
	model_orientation(window);
	Timestamp ta;
	if (!console.IsVisible()) model_move_player(window, delta_ms * 1e-3);
	Timestamp tb;
	selection = select_cube(/*out*/sel_cube, /*out*/sel_face);
	Timestamp tc;
	model_digging(window);
	visible_chunks.cleanup();

	stats::collide_time_ms = glm::mix<float>(stats::collide_time_ms, ta.elapsed_ms(tb), 0.15f);
	stats::select_time_ms = glm::mix<float>(stats::select_time_ms, tb.elapsed_ms(tc), 0.15f);
}

// Render

GLuint block_texture;
Text* text = nullptr;

int line_program;
GLuint line_matrix_loc;
GLuint line_position_loc;

int block_program;
GLuint block_matrix_loc;
GLuint block_sampler_loc;
GLuint block_pos_loc;
GLuint block_tick_loc;
GLuint block_foglimit2_loc;
GLuint block_eye_loc;
GLuint block_pos0_loc;
GLuint block_pos1_loc;
GLuint block_pos2_loc;
GLuint block_pos3_loc;
GLuint block_texture_loc;
GLuint block_light_loc;
GLuint block_plane_loc;

int mesh_program;
GLuint mesh_matrix_loc;
GLuint mesh_sampler_loc;
GLuint mesh_tick_loc;
GLuint mesh_foglimit2_loc;
GLuint mesh_eye_loc;
GLuint mesh_mesh_pos_loc;
GLuint mesh_mesh_rot_loc;
GLuint mesh_vertex_pos_loc;
GLuint mesh_vertex_uv_loc;
GLuint mesh_texture_loc;

GLuint block_buffer;
GLuint line_buffer;
GLuint mesh_buffer;

int g_tick;

struct BlockTextureLoader
{
	BlockTextureLoader();
	void load(BlockTexture block_texture);
	void load_textures();

private:
	uint m_width, m_height, m_size;
	std::vector<uint8_t> m_pixels;
};

BlockTextureLoader::BlockTextureLoader()
{
	uint8_t* image;
	char filename[1024];
	snprintf(filename, sizeof(filename), "bdc256/%s.png", block_texture_name[0]);
	uint error = lodepng_decode32_file(&image, &m_width, &m_height, filename);
	if (error)
	{
		fprintf(stderr, "lodepgn_decode32_file(%s) error %u: %s\n", filename, error, lodepng_error_text(error));
		exit(1);
	}
	free(image);
	m_size = m_width * m_height * 4;
}

void BlockTextureLoader::load(BlockTexture tex)
{
	unsigned char* image;
	unsigned iwidth, iheight;
	char filename[1024];
	if (tex > BlockTexture::lava_still && tex <= BlockTexture::lava_still_31) return;
	if (tex > BlockTexture::lava_flow && tex <= BlockTexture::lava_flow_31) return;
	if (tex > BlockTexture::water_still && tex <= BlockTexture::water_still_63) return;
	if (tex > BlockTexture::water_flow && tex <= BlockTexture::water_flow_31) return;
	if (tex > BlockTexture::furnace_front_on && tex <= BlockTexture::ff_15) return;
	if (tex > BlockTexture::sea_lantern && tex <= BlockTexture::sl_4) return;
	snprintf(filename, sizeof(filename), "bdc256/%s.png", block_texture_name[uint(tex)]);
	unsigned error = lodepng_decode32_file(&image, &iwidth, &iheight, filename);
	if (error)
	{
		fprintf(stderr, "lodepgn_decode32_file(%s) error %u: %s\n", filename, error, lodepng_error_text(error));
		exit(1);
	}
	Auto(free(image));

	assertf(m_width == iwidth, "m_wdith=%u iwidth=%u", m_width, iwidth);
	assertf((iheight % m_height) == 0, "iheight=%u m_height=%u", iheight, m_height);

	glm::u8vec4* pixel = (glm::u8vec4*)image;
	if (is_leaves(tex) || tex == BlockTexture::grass_top)
	{
		FOR(j, m_width * m_height)
		{
			pixel[j].r = 0;
			pixel[j].b = 0;
		}
	}
	if (tex == BlockTexture::cloud)
	{
		FOR(j, m_width * m_height)
		{
			pixel[j].a = pixel[j].r;
		}
	}

	uint frames = 1;
	if (tex == BlockTexture::water_still) frames = 64;
	if (tex == BlockTexture::water_flow) frames = 32;
	if (tex == BlockTexture::lava_still || tex == BlockTexture::lava_flow) frames = 32;
	if (tex == BlockTexture::furnace_front_on) frames = 16;
	if (tex == BlockTexture::sea_lantern) frames = 5;
	memcpy(m_pixels.data() + uint(tex) * m_size, image, m_size * frames);
}

void BlockTextureLoader::load_textures()
{
	m_pixels.resize(m_width * m_height * 4 * block_texture_count);

	// Use 7 instead of 8 threads to avoid the same thread from having to load all animated textures
	std::thread threads[7];
	FOR(i, 7)
	{
		threads[i] = std::move(std::thread([](BlockTextureLoader* self, int k)
		{
			FOR(j, block_texture_count)
			{
				if (j % 7 == k) self->load((BlockTexture)j);
			}
		}, this, i));
	}
	FOR(i, 7) threads[i].join();

	glGenTextures(1, &block_texture);
	glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, m_width, m_height, block_texture_count, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
}

int get_uniform_location(int program, const char* name)
{
	int location = glGetUniformLocation(program, name);
	release_assertf(location != -1, "uniform '%s' not found", name);
	return location;
}

int get_attrib_location(int program, const char* name)
{
	int location = glGetAttribLocation(program, name);
	release_assertf(location != -1, "attrib '%s' not found", name);
	return location;
}

struct MeshVertex
{
	glm::vec3 vertex_pos;
	glm::vec2 vertex_uv;
	BlockTexture texture;
};

std::vector<MeshVertex> g_avatar_mesh;

void render_init()
{
	fprintf(stderr, "OpenGL version: [%s]\n", glGetString(GL_VERSION));
	glEnable(GL_CULL_FACE);

	{
		BlockTextureLoader loader;
		loader.load_textures();
	}

	perspective = glm::perspective<float>(M_PI / 180 * 90, width / (float)height, 0.03, 1000);
	perspective_rotation = glm::rotate(perspective, g_player.pitch, glm::vec3(1, 0, 0));
	perspective_rotation = glm::rotate(perspective_rotation, g_player.yaw, glm::vec3(0, 1, 0));
	perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
	rays_remaining = directions.size();
	last_cursor_init = false;
	text = new Text;

	line_program = load_program("line");
	line_matrix_loc = get_uniform_location(line_program, "matrix");
	line_position_loc = get_attrib_location(line_program, "position");

	block_program = load_program("block", true);
	block_matrix_loc = get_uniform_location(block_program, "matrix");
	block_sampler_loc = get_uniform_location(block_program, "sampler");
	block_pos_loc = get_uniform_location(block_program, "cpos");
	block_tick_loc = get_uniform_location(block_program, "tick");
	block_foglimit2_loc = get_uniform_location(block_program, "foglimit2");
	block_eye_loc = get_uniform_location(block_program, "eye");

	block_pos0_loc = get_attrib_location(block_program, "vertex0");
	block_pos1_loc = get_attrib_location(block_program, "vertex1");
	block_pos2_loc = get_attrib_location(block_program, "vertex2");
	block_pos3_loc = get_attrib_location(block_program, "vertex3");
	block_texture_loc = get_attrib_location(block_program, "block_texture_with_flag");
	block_light_loc = get_attrib_location(block_program, "light");
	block_plane_loc = get_attrib_location(block_program, "plane");

	mesh_program = load_program("mesh");
	mesh_matrix_loc = get_uniform_location(mesh_program, "matrix");
	mesh_sampler_loc = get_uniform_location(mesh_program, "sampler");
	mesh_tick_loc = get_uniform_location(mesh_program, "tick");
	mesh_foglimit2_loc = get_uniform_location(mesh_program, "foglimit2");
	mesh_eye_loc = get_uniform_location(mesh_program, "eye");
	mesh_mesh_pos_loc = get_uniform_location(mesh_program, "mesh_pos");
	mesh_mesh_rot_loc = get_uniform_location(mesh_program, "mesh_rot");

	mesh_vertex_pos_loc = get_attrib_location(mesh_program, "vertex_pos");
	mesh_vertex_uv_loc = get_attrib_location(mesh_program, "vertex_uv");
	mesh_texture_loc = get_attrib_location(mesh_program, "texture_with_flag");

	glGenBuffers(1, &line_buffer);
	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &mesh_buffer);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glClearColor(0.2, 0.4, 1, 1.0);
	glViewport(0, 0, width, height);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Generate avatar mesh
	g_avatar_mesh.resize(36);
	MeshVertex* e = &g_avatar_mesh[0];
	FOR(face, 6)
	{
		BlockTexture texture = get_block_texture(Block::pumpkin, face);
		const int* f = Cube::faces[face];
		glm::vec3 v0 = glm::vec3(Cube::corner[f[0]]) - glm::vec3(0.5, 0.5, 0.5);
		glm::vec3 v1 = glm::vec3(Cube::corner[f[1]]) - glm::vec3(0.5, 0.5, 0.5);
		glm::vec3 v2 = glm::vec3(Cube::corner[f[2]]) - glm::vec3(0.5, 0.5, 0.5);
		glm::vec3 v3 = glm::vec3(Cube::corner[f[3]]) - glm::vec3(0.5, 0.5, 0.5);

		glm::ivec3 d = Cube::corner[f[2]];
		int u, v;
		if (face < 2) { u = d.y; v = d.z; }
		else if (face < 4) { u = d.x; v = d.z; }
		else { u = d.y; v = d.x; }

		if (face == 1 || face == 2 || face == 4)
		{
			glm::vec3 w = v0;
			v0 = v1;
			v1 = v2;
			v2 = v3;
			v3 = w;
		}

		*e++ = { v0, glm::vec2(u, v), texture };
		*e++ = { v1, glm::vec2(u, 0), texture };
		*e++ = { v2, glm::vec2(0, 0), texture };

		*e++ = { v0, glm::vec2(u, v), texture };
		*e++ = { v2, glm::vec2(0, 0), texture };
		*e++ = { v3, glm::vec2(0, v), texture };
	}
}

void BlockRenderer::draw_quad(int face, bool reverse, bool underwater_overlay)
{
	Quad q;
	q.texture = get_block_texture(m_block, face);
	if (underwater_overlay) q.texture = BlockTexture((int)q.texture | (1 << 15));
	const int* f = Cube::faces[face];
	q.light = face_light2(face); // TODO reverse?
	glm::ivec3 w = m_pos & ChunkSizeMask;
	if (reverse)
	{
		q.pos[0] = glm::u8vec3((w + Cube::corner[f[0]]) * 15);
		q.pos[1] = glm::u8vec3((w + Cube::corner[f[3]]) * 15);
		q.pos[2] = glm::u8vec3((w + Cube::corner[f[2]]) * 15);
		q.pos[3] = glm::u8vec3((w + Cube::corner[f[1]]) * 15);
		q.plane = ((face ^ 1) << 8) | q.pos[0][face / 2];
	}
	else
	{
		q.pos[0] = glm::u8vec3((w + Cube::corner[f[0]]) * 15);
		q.pos[1] = glm::u8vec3((w + Cube::corner[f[1]]) * 15);
		q.pos[2] = glm::u8vec3((w + Cube::corner[f[2]]) * 15);
		q.pos[3] = glm::u8vec3((w + Cube::corner[f[3]]) * 15);
		q.plane = (face << 8) | q.pos[0][face / 2];
	}
	m_quads->push_back(q);
}

static glm::ivec3 adjust(glm::ivec3 v, int zmin, int zmax)
{
	return glm::ivec3(v.x * 15, v.y * 15, (v.z == 0) ? zmin : zmax);
}

void BlockRenderer::draw_quad(int face, int zmin, int zmax, bool reverse, bool underwater_overlay)
{
	Quad q;
	q.texture = get_block_texture(m_block, face);
	if (underwater_overlay) q.texture = BlockTexture((int)q.texture | (1 << 15));
	const int* f = Cube::faces[face];
	q.light = face_light2(face); // TODO zmin/zmax? // TODO reverse?
	glm::ivec3 w = m_pos & ChunkSizeMask;
	if (reverse)
	{
		q.pos[0] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[0]], zmin, zmax));
		q.pos[1] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[3]], zmin, zmax));
		q.pos[2] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[2]], zmin, zmax));
		q.pos[3] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[1]], zmin, zmax));
		q.plane = ((face ^ 1) << 8) | q.pos[0][face / 2];
	}
	else
	{
		q.pos[0] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[0]], zmin, zmax));
		q.pos[1] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[1]], zmin, zmax));
		q.pos[2] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[2]], zmin, zmax));
		q.pos[3] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[3]], zmin, zmax));
		q.plane = (face << 8) | q.pos[0][face / 2];
	}
	m_quads->push_back(q);
}

const float foglimit2 = sqr(0.8 * RenderDistance * ChunkSize);

void render_world_blocks(const glm::mat4& matrix, const Frustum& frustum)
{
	glUseProgram(block_program);
	glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform3fv(block_eye_loc, 1, glm::value_ptr(g_player.position));
	if (!g_player.creative_mode) g_tick += 1;
	glUniform1i(block_tick_loc, g_tick);
	glUniform1f(block_foglimit2_loc, foglimit2);

	glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
	glEnableVertexAttribArray(block_pos0_loc);
	glEnableVertexAttribArray(block_pos1_loc);
	glEnableVertexAttribArray(block_pos2_loc);
	glEnableVertexAttribArray(block_pos3_loc);
	glEnableVertexAttribArray(block_texture_loc);
	glEnableVertexAttribArray(block_light_loc);
	glEnableVertexAttribArray(block_plane_loc);

	glVertexAttribIPointer(block_pos0_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[0]);
	glVertexAttribIPointer(block_pos1_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[1]);
	glVertexAttribIPointer(block_pos2_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[2]);
	glVertexAttribIPointer(block_pos3_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[3]);
	glVertexAttribIPointer(block_texture_loc, 1, GL_UNSIGNED_SHORT, sizeof(Quad), &((Quad*)0)->texture);
	glVertexAttribIPointer(block_light_loc,   1, GL_UNSIGNED_SHORT, sizeof(Quad), &((Quad*)0)->light);
	glVertexAttribIPointer(block_plane_loc,   1, GL_UNSIGNED_SHORT, sizeof(Quad), &((Quad*)0)->plane);

	stats::chunk_count = 0;
	stats::quad_count = 0;

	glEnable(GL_BLEND);
	static BlockRenderer renderer;
	for (VisibleChunks::Element e : visible_chunks)
	{
		glm::ivec3 cpos = e.cpos;
		if (!frustum.is_sphere_outside(glm::vec3(cpos * ChunkSize + ChunkSize / 2), ChunkSize * BlockRadius))
		{
			Chunk& chunk = g_chunks.get(cpos);
			if (chunk.get_cpos() != cpos) continue;

			if (chunk.m_remesh)
			{
				const Blocks* chunks[27];
				FOR2(x, -1, 1) FOR2(y, -1, 1) FOR2(z, -1, 1)
				{
					Chunk* c = g_chunks.get_opt(cpos + glm::ivec3(x, y, z));
					chunks[x*9 + y*3 + z + 13] = c ? &c->blocks() : nullptr;
				}
				chunk.remesh(renderer, chunks);
			}
			glm::ivec3 pos = cpos * ChunkSize;
			glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
			// TODO: avoid expensive sorting for far chunks
			chunk.sort(g_player.position);
			stats::quad_count += chunk.render();
			stats::chunk_count += 1;
		}
	}
	glDisable(GL_BLEND);
}

struct Avatar
{
	bool visible;
	glm::vec3 position;
	float yaw, pitch;
	glm::mat3 rotation;
};

struct Avatars
{
	Avatar& add(int index)
	{
		if (!avatars[index].visible)
		{
			avatars[index].visible = true;
			list.push_back(index);
		}
		return avatars[index];
	}

	void remove(int index)
	{
		if (avatars[index].visible)
		{
			avatars[index].visible = false;
			FOR(i, list.size()) if (list[i] == index)
			{
				list[i] = list.back();
				list.pop_back();
				break;
			}
		}
	}

	Avatars() { FOR(i, 255) avatars[i].visible = false; }

	std::vector<uint8_t> list; // list of all visible avatars
	Avatar avatars[255]; // hash table
};

Avatars g_avatars;

void render_avatars(const glm::mat4& matrix, const Frustum& frustum)
{
	glUseProgram(mesh_program);
	glUniformMatrix4fv(mesh_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform3fv(mesh_eye_loc, 1, glm::value_ptr(g_player.position));
	if (!g_player.creative_mode) g_tick += 1;
	glUniform1i(mesh_tick_loc, g_tick);
	glUniform1f(mesh_foglimit2_loc, foglimit2);
	glUniform1i(mesh_sampler_loc, 0);

	glBindBuffer(GL_ARRAY_BUFFER, mesh_buffer);
	glEnableVertexAttribArray(mesh_vertex_pos_loc);
	glEnableVertexAttribArray(mesh_vertex_uv_loc);
	glEnableVertexAttribArray(mesh_texture_loc);

	MeshVertex* m = nullptr;
	glVertexAttribPointer(mesh_vertex_pos_loc, 3, GL_FLOAT, GL_FALSE, sizeof(*m), &m->vertex_pos);
	glVertexAttribPointer(mesh_vertex_uv_loc,  2, GL_FLOAT, GL_FALSE, sizeof(*m), &m->vertex_uv);
	glVertexAttribIPointer(mesh_texture_loc,   1, GL_UNSIGNED_SHORT, sizeof(*m), &m->texture);

	float radius = sqrt(5) * 0.48; // TODO constant - player cylinder radius
	glEnable(GL_BLEND);
	for (uint8_t index : g_avatars.list)
	{
		Avatar& avatar = g_avatars.avatars[index];
		if (!frustum.is_sphere_outside(avatar.position, radius))
		{
			glUniform3fv(mesh_mesh_pos_loc, 1, glm::value_ptr(avatar.position));
			glUniformMatrix3fv(mesh_mesh_rot_loc, 1, GL_FALSE, glm::value_ptr(avatar.rotation));
			glBufferData(GL_ARRAY_BUFFER, sizeof(MeshVertex) * g_avatar_mesh.size(), &g_avatar_mesh[0], GL_STREAM_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, g_avatar_mesh.size());
		}
	}
	glDisable(GL_BLEND);
}

struct ChatLine
{
	uint8_t id;
	Timestamp timestamp;
	char* text;
};

arraydeque<ChatLine> g_chat_lines;

void client_receive_text_message(const char* message, uint length)
{
	std::vector<Token> tokens;
	tokenize(message, length, /*out*/tokens);

	if (tokens[0] == "left")
	{
		assert(tokens.size() == 2 && is_integer(tokens[1]));
		int id = parse_int(tokens[1]);
		g_avatars.remove(id);
		return;
	}

	if (tokens[0] == "fsync_ack")
	{
		g_fsync_ack = true;
		return;
	}

	if (tokens[0] == "chat")
	{
		assertf(tokens.size() >= 3 && is_integer(tokens[1]), "message [%.*s]", length, message);
		ChatLine line;
		line.id = parse_int(tokens[1]);
		int a = length - (tokens[2].first - message);
		line.text = (char*)malloc(a + 1);
		memcpy(line.text, tokens[2].first, a);
		line.text[a] = 0;
		g_chat_lines.push_front(line);
		return;
	}

	console.Print(">> %.*s\n", length, message);
}

MessageServerStatus g_server_status;
uint32_t g_bytes_received;
uint32_t g_server_frames = 0;

bool client_receive_message()
{
	SocketBuffer& recv = g_recv_buffer;
	if (recv.size() == 0) return false;
	switch ((MessageType)recv.data()[0])
	{
	case MessageType::Text:
	{
		auto message = read_text_message(recv);
		if (!message) return false;
		client_receive_text_message(message->text, message->size);
		return true;
	}
	case MessageType::AvatarState:
	{
		auto message = recv.read<MessageAvatarState>();
		if (!message) return false;
		Avatar& avatar = g_avatars.add(message->id);
		avatar.position = message->position;
		avatar.yaw = message->yaw;
		avatar.pitch = message->pitch;
		avatar.rotation = rotate_z(M_PI / 2) * rotate_x(message->pitch) * rotate_z(message->yaw);
		return true;
	}
	case MessageType::ChunkState:
	{
		auto message = recv.read<MessageChunkState>();
		if (!message) return false;
		Chunk& chunk = g_chunks.get(message->cpos);
		chunk.init(message->cpos, message->blocks);
		FOR2(x, -1, 1) FOR2(y, -1, 1) FOR2(z, -1, 1)
		{
			Chunk* c = g_chunks.get_opt(message->cpos + glm::ivec3(x, y, z));
			if (c) c->m_remesh = true; // TODO optimize this
		}
		return true;
	}
	case MessageType::ServerStatus:
	{
		auto message = recv.read<MessageServerStatus>();
		if (!message) return false;
		g_server_status = *message;
		g_server_frames += 1;
		return true;
	}
	}
	return false;
}

void client_frame()
{
	uint size_before = g_recv_buffer.size();
	CHECK2(g_recv_buffer.recv_any(g_client), exit(1));
	g_bytes_received = g_recv_buffer.size() - size_before;
	while (client_receive_message()) { }

	if (!g_player.broadcasted)
	{
		g_player.broadcasted = true;
		auto message = g_send_buffer.write<MessageAvatarState>();
		message->type = MessageType::AvatarState;
		message->position = g_player.position;
		message->pitch = g_player.pitch;
		message->yaw = g_player.yaw;
	}
	CHECK2(g_send_buffer.send_any(g_client), exit(1));
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);

	if (show_counters && !console.IsVisible())
	{
		text->Reset(width, height, matrix, true);
		int raytrace = std::round(100.0f * (directions.size() - rays_remaining) / directions.size());
		text->Print("[%.1f %.1f %.1f] C:%4d Q:%3dk frame:%2.0f model:%1.0f raytrace:%2.0f %d%% render %2.0f F%c%c%c recv:%u send:%u",
			g_player.position.x, g_player.position.y, g_player.position.z, stats::chunk_count, stats::quad_count / 1000,
			stats::frame_time_ms, stats::model_time_ms, stats::raytrace_time_ms, raytrace, stats::render_time_ms,
			enable_f4 ? '4' : '-', enable_f5 ? '5' : '-', enable_f6 ? '6' : '-', g_recv_buffer.size(), g_send_buffer.size());

		text->Print("collide:%1.0f select:%1.0f simulate:%1.0f [%.1f %.1f %.1f] %.1f%s",
			stats::collide_time_ms, stats::select_time_ms, stats::simulate_time_ms,
			g_player.velocity.x, g_player.velocity.y, g_player.velocity.z, glm::length(g_player.velocity), on_the_ground ? " ground" : "");

		text->Print("exchange:%u inbox:%u simulation:%u chunk:%u avatar:%u received:%ukb frame:%u",
			g_server_status.exchange_time, g_server_status.inbox_time, g_server_status.simulation_time, g_server_status.chunk_time, g_server_status.avatar_time, g_bytes_received / 1024, g_server_frames);

		if (selection)
		{
			Block block = g_chunks.get_block(sel_cube);
			text->Print("selected: %s at [%d %d %d]", block_name[(uint)block], sel_cube.x, sel_cube.y, sel_cube.z);
		}

		if (g_player.digging_on)
		{
			text->Print("digging for %.1f seconds", g_player.digging_start.elapsed_ms() / 1000);
		}
	}

	if (!console.IsVisible())
	{
		text->Reset(width, height, matrix, false);
		text->Print("");
		FOR(i, g_chat_lines.size())
		{
			double e = (10000 - g_chat_lines[i].timestamp.elapsed_ms()) / 10000;
			if (e < 0)
			{
				while (g_chat_lines.size() > i) free(g_chat_lines.pop_back().text);
				break;
			}
			e = sqrt(e);
			glm::vec3 colors[] = { glm::vec3(1, 1, 1), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
			glm::vec3 c = colors[g_chat_lines[i].id % (sizeof(colors) / sizeof(colors[0]))];
			text->fg_color = glm::vec4(c.x, c.y, c.z, e);
			text->bg_color = glm::vec4(0, 0, 0, 0.4 * e);
			text->Print("%s", g_chat_lines[i].text);
		}
	}

	text->Reset(width, height, matrix, true);
	console.Render(text, glfwGetTime());
	glUseProgram(0);

	if (show_palette)
	{
		if (scroll_dy <= 0.1 / 5) scroll_y += (std::round(scroll_y) - scroll_y) * 0.05;

		glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
		glUseProgram(block_program);
		glm::mat4 view;
		view = glm::rotate(view, float(M_PI * 1.75), glm::vec3(1,0,0));
		view = glm::rotate(view, float(M_PI * 0.25), glm::vec3(0,0,1));
		view = glm::translate(view, glm::vec3(-128,-128,-128));
		view = glm::ortho<float>(-8 * width / height, 8 * width / height, -8, 8, -64, 64) * view;
		view = glm::translate(view, glm::vec3(-scroll_y, scroll_y, -1));
		glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(view));

		glm::vec3 eye(8, 8, 8);
		glUniform3fv(block_eye_loc, 1, glm::value_ptr(eye));
		glUniform1i(block_tick_loc, g_tick);
		glUniform1f(block_foglimit2_loc, 1e30);

		glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
		glEnableVertexAttribArray(block_pos0_loc);
		glEnableVertexAttribArray(block_pos1_loc);
		glEnableVertexAttribArray(block_pos2_loc);
		glEnableVertexAttribArray(block_pos3_loc);
		glEnableVertexAttribArray(block_texture_loc);
		glEnableVertexAttribArray(block_light_loc);
		glEnableVertexAttribArray(block_plane_loc);

		glVertexAttribIPointer(block_pos0_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[0]);
		glVertexAttribIPointer(block_pos1_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[1]);
		glVertexAttribIPointer(block_pos2_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[2]);
		glVertexAttribIPointer(block_pos3_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[3]);
		glVertexAttribIPointer(block_texture_loc, 1, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->texture);
		glVertexAttribIPointer(block_light_loc,   1, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->light);
		glVertexAttribIPointer(block_plane_loc,   1, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->plane);

		const uint palette_blocks = block_count - (uint)Block::water;
		WQuad quads[3 * palette_blocks];
		WQuad* e = quads;
		FOR(i, palette_blocks) FOR(face, 6)
		{
			if (face != 0 && face != 2 && face != 5) continue;

			const int* f = Cube::faces[face];
			uint8_t light = 255;
			if (face == 2) light = 127;
			if (face == 5) light = 127 + 64;
			if (face == 0) light = 255;

			glm::ivec3 w(128+i, 128-i, 128);
			FOR(j, 4) e->pos[j] = glm::u16vec3((w + Cube::corner[f[j]]) * 15);
			e->plane = face << 8;
			e->light = 65535;
			e->texture = get_block_texture(Block(i + block_count - palette_blocks), face);
			e += 1;
		}

		glEnable(GL_BLEND);
		glm::ivec3 pos(0, 0, 0);
		glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
		glBufferData(GL_ARRAY_BUFFER, sizeof(WQuad) * 3 * palette_blocks, quads, GL_STREAM_DRAW);
		glDrawArrays(GL_POINTS, 0, 3 * palette_blocks);
		glUseProgram(0);
		glDisable(GL_BLEND);

		text->Reset(width, height, matrix, true);
		text->PrintAt(width / 2 - height / 40, height / 2 - height / 40, 0, block_name[(uint)g_player.palette_block], strlen(block_name[(uint)g_player.palette_block]));

		if (ts_palette.elapsed_ms() >= 1000) show_palette = false;
	}

	if (selection || show_palette)
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

std::atomic<Timestamp> stall_ts;
std::atomic<bool> stall_enable(true);

void stall_alarm_thread()
{
	while (true)
	{
		usleep(1000);
		if (!stall_enable.load(std::memory_order_relaxed)) break;
		Timestamp a = stall_ts;
		if (a.elapsed_ms() > 5000)
		{
			fprintf(stderr, "main thread stalled :(\n");
			exit(1);
		}
	}
}

void wait_for_nearby_chunks(glm::ivec3 cpos)
{
	FOR2(x, -1, 1) FOR2(y, -1, 1) FOR2(z, -1, 1) while (true)
	{
		client_frame();
		glm::ivec3 p(x, y, z);
		Chunk& chunk = g_chunks.get(p + cpos);
		if (chunk.get_cpos() == p + cpos) break;
		usleep(10000);
	}
}

void command_import(char* filename, glm::ivec3 pos, float scale)
{
	std::thread([=]()
	{
		Auto(free(filename));
		console.Print("importing %s at (%d %d %d) scale %f ...\n", filename, pos.x, pos.y, pos.z, scale);

		Mesh mesh;
		if (!mesh.load(filename))
		{
			console.Print("loading file %s failed\n", filename);
			return;
		}

		rasterize_mesh(mesh, pos, scale, Block::grass);
		console.Print("import completed\n");
	}).detach();
}

void command_set()
{
	console.Print("collision = %s\n", g_collision ? "true" : "false");
}

void command_set(Token key, Token value)
{
	if (key == "collision")
	{
		if (value == "true") { g_collision = true; return; }
		if (value == "false") { g_collision = false; return; }
		console.Print("error in syntax: set collision (true | false)\n");
		return;
	}
	console.Print("unknown var %.*s. type 'set' for list of all vars.", key.second, key.first);
}

void MyConsole::Execute(const char* command, int length)
{
	if (command[0] == '/') // shout!
	{
		if (length == 1) return;
		command += 1;
		length -= 1;
		write_text_message(g_send_buffer, "chat %.*s", length, command);
		return;
	}

	std::vector<Token> tokens;
	tokenize(command, length, tokens);

	if (tokens[0] == "import")
	{
		if (tokens.size() != 6 || !is_integer(tokens[2]) || !is_integer(tokens[3]) || !is_integer(tokens[4]) || !is_real(tokens[5]))
		{
			Print("error in syntax: import <filename> <x> <y> <z> <scale>\n");
			return;
		}
		char* filename = (char*)memcpy(malloc(tokens[1].second + 1), tokens[1].first, tokens[1].second);
		filename[tokens[1].second] = 0;
		command_import(filename, glm::ivec3(parse_int(tokens[2]), parse_int(tokens[3]), parse_int(tokens[4])), parse_real(tokens[5]));
		return;
	}

	if (tokens[0] == "set")
	{
		if (tokens.size() == 1) { command_set(); return; }
		if (tokens.size() == 3) { command_set(tokens[1], tokens[2]); return; }
		Print("error in syntax: set [<key> <value>]\n");
		return;
	}

	assert(tokens.size() > 0);
	Print("[%.*s] command not found\n", tokens[0].second, tokens[0].first);
}

GLFWwindow* create_window()
{
	glfwSetErrorCallback(OnError);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode->width*2, mode->height*2, "Arena", glfwGetPrimaryMonitor(), NULL);
	if (!window) return nullptr;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(0/*VSYNC*/);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);
	return window;
}

bool g_run_server = true;
const char* g_connect_to = "localhost";

bool parse_command_args(int argc, char** argv)
{
	bool arg_net = false;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp("--server", argv[i]) == 0)
		{
			if (arg_net) return false;
			arg_net = true;
			g_run_server = true;
			g_connect_to = nullptr;
		}
		else if (strcmp("--join", argv[i]) == 0)
		{
			if (arg_net) return false;
			arg_net = true;
			if (i+1 >= argc) return false;
			g_run_server = false;
			g_connect_to = argv[i+1];
			i += 1;
		}
		else
		{
			return false;
		}
	}
	return true;
}

bool make_dir(const char* name);

int main(int argc, char** argv)
{
	void sigsegv_handler(int sig);
	signal(SIGSEGV, sigsegv_handler);
	CHECK(glfwInit());

	if (!parse_command_args(argc, argv))
	{
		printf("usage: %s [--server | --join <hostname>]\n", argv[0]);
		return 0;
	}

	CHECK(make_dir("../world"));

	if (!g_connect_to)
	{
		server_main();
		return 0;
	}
	if (g_run_server) std::thread(server_main).detach();

	fprintf(stderr, "Connecting to %s:7000 ...\n", g_connect_to);
	int retries = 0;
	while (!g_client.connect(g_connect_to, "7000"))
	{
		if (errno != ECONNREFUSED) return 0;
		usleep(100*1000);
		g_client.close();
		if (++retries == 5) return 0;
	}
	fprintf(stderr, "Connected!\n");
	g_recv_buffer.reserve(1 << 20);

	glm::dvec3 a;
	glm::i64vec3 b;
	a.x = glfwGetTime();
	b.x = rdtsc();
	usleep(100000);
	a.y = glfwGetTime();
	b.y = rdtsc();
	usleep(100000);
	a.z = glfwGetTime();
	b.z = rdtsc();

	GLFWwindow* window = create_window();
	CHECK(window);
	model_init(window);
	render_init();

	glm::dvec3 c;
	glm::i64vec3 d;
	c.x = glfwGetTime();
	d.x = rdtsc();
	usleep(100000);
	c.y = glfwGetTime();
	d.y = rdtsc();
	usleep(100000);
	c.z = glfwGetTime();
	d.z = rdtsc();
	Timestamp::init(a, b, c, d);

	Timestamp t0;
	stall_ts = t0;
	std::thread stall_alarm(stall_alarm_thread);

	glm::ivec3 cplayer = glm::ivec3(glm::floor(g_player.position)) >> ChunkSizeBits;
	wait_for_nearby_chunks(cplayer);

	fprintf(stderr, "Ready!\n");
	while (!glfwWindowShouldClose(window))
	{
		Timestamp ta;
		stall_ts = ta;
		double frame_ms = t0.elapsed_ms(ta);
		t0 = ta;
		glfwPollEvents();

		client_frame();

		model_frame(window, frame_ms);
		glm::mat4 matrix = glm::translate(perspective_rotation, -g_player.position);
		Frustum frustum(matrix);
		g_player.cpos = glm::ivec3(glm::floor(g_player.position)) >> ChunkSizeBits;
		g_player.atomic_cpos = compress_ivec3(g_player.cpos);

		Timestamp tb;
		update_render_list(frustum);

		Timestamp tc;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
		render_world_blocks(matrix, frustum);
		glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
		render_avatars(matrix, frustum);
		if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
	stall_enable.store(false);
	stall_alarm.join();

	fprintf(stderr, "Saving ...\n");
	if (g_run_server)
	{
		g_fsync_ack = false;
		write_text_message(g_send_buffer, "fsync");
		while (g_send_buffer.size() > 0)
		{
			CHECK2(g_send_buffer.send_any(g_client), exit(1));
			usleep(10000);
		}
		while (!g_fsync_ack)
		{
			CHECK2(g_recv_buffer.recv_any(g_client), exit(1));
			while (g_recv_buffer.size() > 0 && client_receive_message()) { }
			usleep(10000);
		}
	}
	g_player.save();
	fprintf(stderr, "Exiting ...\n");
	_exit(0); // exit(0) is not enough
	return 0;
}
