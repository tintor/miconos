// TODO:
// block editing
// PERF async chunk loading
// PERF use OpenGL array buffers
// walk / jump mode
// persistence
// client / server
// multi-player
// sky with day/night cycle
// permanent server
// # texture mipmaps?
// # PERF level-of-detail rendering
// # portals
// # slope generation
// # transparent water blocks
// # real shadows from sun
// # point lights with shadows (for caves)?
// # PERF occulsion culling (for caves / mountains)
// # advanced voxel editing (flood fill, cut/copy/paste, move, drawing shapes)
// # wiki / user change tracking
// # real world elevation data
// # spherical world / spherical gravity ?
// # translating blocks ?
// # psysics: water ?
// # psysics: moving objects / vehicles ?

#include <cmath>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
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

// Config

const int VSYNC = 1;

// GUI
//#define GLFW_INCLUDE_GLCOREARB
#ifndef __APPLE_CC__
	#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

int width;
int height;

#define RENDER_LIMIT 13

// Render::Buffers

int gen_buffer(GLenum target, GLsizei size, const void* data)
{
	GLuint buffer;
	glGenBuffers(1, &buffer);
	glBindBuffer(target, buffer);
	glBufferData(target, size, data, GL_STATIC_DRAW);
	glBindBuffer(target, 0);
	return buffer;
}

void gen_buffers(int components, int faces,
	GLfloat* position_data,  GLfloat* normal_data,  GLfloat* uv_data,
	GLuint*  position_buffer, GLuint* normal_buffer, GLuint* uv_buffer)
{
	if (position_buffer)
	{
		glDeleteBuffers(1, position_buffer);
		*position_buffer = gen_buffer(GL_ARRAY_BUFFER, sizeof(GLfloat) * faces * 6 * components, position_data);
		delete[] position_data;
	}
	if (normal_buffer)
	{
		glDeleteBuffers(1, normal_buffer);
		*normal_buffer = gen_buffer(GL_ARRAY_BUFFER, sizeof(GLfloat) * faces * 6 * components, normal_data);
		delete[] normal_data;
	}
	if (uv_buffer)
	{
		glDeleteBuffers(1, uv_buffer);
		*uv_buffer = gen_buffer(GL_ARRAY_BUFFER, sizeof(GLfloat) * faces * 6 * 2, uv_data);
		delete[] uv_data;
	}
}

void malloc_buffers(int components, int faces, GLfloat** position_data, GLfloat** normal_data, GLfloat** uv_data)
{
	if (position_data)
	{
		*position_data = new GLfloat[faces * 6 * components];
	}
	if (normal_data)
	{
		*normal_data = new GLfloat[faces * 6 * components];
	}
	if (uv_data)
	{
		*uv_data = new GLfloat[faces * 6 * 2];
	}
}

// Render::Texture

#define LODEPNG_COMPILE_CPP
#include "lodepng/lodepng.h"

void load_png_texture(std::string filename)
{
	unsigned char* image;
	unsigned width, height;
	unsigned error = lodepng_decode32_file(&image, &width, &height, filename.c_str());
	if (error)
	{
		fprintf(stderr, "lodepgn_decode32_file error %u: %s\n", error, lodepng_error_text(error));
		exit(1);
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	free(image);
}

// Render::Shader

std::string read_file(std::string filename)
{
	std::ifstream in(filename, std::ios::in | std::ios::binary);
	if (in)
	{
		std::string contents;
		in.seekg(0, std::ios::end);
		contents.resize(in.tellg());
		in.seekg(0, std::ios::beg);
		in.read(&contents[0], contents.size());
		in.close();
		return contents;
	}
	throw errno;
}

GLuint make_shader(GLenum type, std::string source)
{
	GLuint shader = glCreateShader(type);
	const GLchar* c = source.c_str();
	glShaderSource(shader, 1, &c, NULL);
	glCompileShader(shader);
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		GLchar* info = new GLchar[length];
		glGetShaderInfoLog(shader, length, NULL, info);
		info[length] = 0;
		std::cerr << "glCompileShader failed on [" << source << "]:\n" << info << std::endl;
		exit(1);
	}
	return shader;
}

GLuint load_shader(GLenum type, std::string path)
{
	return make_shader(type, read_file(path));
}

GLuint make_program(GLuint shader1, GLuint shader2)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, shader1);
	glAttachShader(program, shader2);
	glLinkProgram(program);
	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint length;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
		GLchar* info = new GLchar[length];
		glGetProgramInfoLog(program, length, NULL, info);
		info[length] = 0;
		fprintf(stderr, "glLinkProgram failed: %s\n", info);
		exit(1);
	}
	glDetachShader(program, shader1);
	glDetachShader(program, shader2);
	glDeleteShader(shader1);
	glDeleteShader(shader2);
	return program;
}

GLuint load_program(std::string path1, std::string path2)
{
	GLuint shader1 = load_shader(GL_VERTEX_SHADER, path1);
	GLuint shader2 = load_shader(GL_FRAGMENT_SHADER, path2);
	return make_program(shader1, shader2);
}

// Render::Text

GLuint text_texture;
GLuint text_program;
GLuint text_matrix_loc;
GLuint text_sampler_loc;
GLuint text_position_loc;
GLuint text_uv_loc;

void text_init()
{
	text_program = load_program("shaders/text_vertex.glsl", "shaders/text_fragment.glsl");
	text_matrix_loc = glGetUniformLocation(text_program, "matrix");
	text_sampler_loc = glGetUniformLocation(text_program, "sampler");
	text_position_loc = glGetAttribLocation(text_program, "position");
	text_uv_loc = glGetAttribLocation(text_program, "uv");
	//glBindFragDataLocation(text_program, 0, "color");

	glGenTextures(1, &text_texture);
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	load_png_texture("font.png");
}

void make_character(float* vertex, float* texture, float x, float y, float n, float m, char c)
{
	float* v = vertex;
	*v++ = x - n; *v++ = y - m;
	*v++ = x + n; *v++ = y - m;
	*v++ = x + n; *v++ = y + m;
	*v++ = x - n; *v++ = y - m;
	*v++ = x + n; *v++ = y + m;
	*v++ = x - n; *v++ = y + m;

	float a = 0.0625;
	float b = a * 2;
	int w = c - 32;
	float du = (w % 16) * a;
	float dv = 1 - (w / 16) * b - b;
	float p = 0;
	float* t = texture;
	*t++ = du + 0; *t++ = dv + p;
	*t++ = du + a; *t++ = dv + p;
	*t++ = du + a; *t++ = dv + b - p;
	*t++ = du + 0; *t++ = dv + p;
	*t++ = du + a; *t++ = dv + b - p;
	*t++ = du + 0; *t++ = dv + b - p;
}

void text_gen_buffers(GLuint* position_buffer, GLuint* uv_buffer, float x, float y, float n, std::string text)
{
	int length = text.length();
	GLfloat* position_data;
	GLfloat* uv_data;
	malloc_buffers(2, length, &position_data, 0, &uv_data);
	for (int i = 0; i < length; i++)
	{
		make_character(position_data + i * 12, uv_data + i * 12, x, y, n / 2, n, text[i]);
		x += n;
	}
	gen_buffers(2, length, position_data, 0, uv_data, position_buffer, 0, uv_buffer);
}

void text_draw_buffers(GLuint position_buffer, GLuint uv_buffer, GLuint position_loc, GLuint uv_loc, int length)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnableVertexAttribArray(text_position_loc);
	glEnableVertexAttribArray(text_uv_loc);

	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	glVertexAttribPointer(position_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
	glVertexAttribPointer(uv_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDrawArrays(GL_TRIANGLES, 0, length * 6);

	glDisableVertexAttribArray(position_loc);
	glDisableVertexAttribArray(uv_loc);
	glDisable(GL_BLEND);
}

void text_print(GLuint position_loc, GLuint uv_loc, float x, float y, float n, std::string text)
{
	GLuint position_buffer = 0;
	GLuint uv_buffer = 0;
	text_gen_buffers(&position_buffer, &uv_buffer, x, y, n, text);
	text_draw_buffers(position_buffer, uv_buffer, position_loc, uv_loc, text.length());
	glDeleteBuffers(1, &position_buffer);
	glDeleteBuffers(1, &uv_buffer);
}

// Util

float sqr(glm::vec3 a) { return glm::dot(a, a); }

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

const int ChunkSizeBits = 4, MapSizeBits = 5;
const int ChunkSize = 1 << ChunkSizeBits, MapSize = 1 << MapSizeBits;
const int ChunkSizeMask = ChunkSize - 1, MapSizeMask = MapSize - 1;

// ============================

struct MapChunk
{
	Block block[ChunkSize][ChunkSize][ChunkSize];
};

float simplex(glm::vec2 p, int octaves, float persistence)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = glm::simplex(p);
	for (int i = 1; i < octaves; i++)
	{
	    freq /= 2;
	    amp *= persistence;
	    max += amp;
	    total += glm::simplex(p * freq) * amp;
	}
	return total / max;
}

struct Simplex
{
	int height[ChunkSize * MapSize][ChunkSize * MapSize];
	int last_cx[MapSize][MapSize];
	int last_cy[MapSize][MapSize];

	Simplex()
	{
		for (int x = 0; x < ChunkSize; x++)
		{
			for (int y = 0; y < ChunkSize; y++)
			{
				last_cx[x][y] = 1000000;
				last_cy[x][y] = 1000000;
			}
		}
	}

	int& LastX(int cx, int cy) { return last_cx[cx & MapSizeMask][cy & MapSizeMask]; }
	int& LastY(int cx, int cy) { return last_cy[cx & MapSizeMask][cy & MapSizeMask]; }
	int& Height(int x, int y) { return height[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
};

Simplex* heights = new Simplex;

int GetColor(int x, int y)
{
	return std::floorf((1 + simplex(glm::vec2(x, y) * -0.044f, 4, 0.5f)) * 8);
}

void GenerateTerrain(glm::ivec3 cpos, MapChunk& chunk)
{
	memset(chunk.block, 0, sizeof(Block) * ChunkSize * ChunkSize * ChunkSize);

	if (heights->LastX(cpos.x, cpos.y) != cpos.x || heights->LastY(cpos.x, cpos.y) != cpos.y)
	{
		for (int x = 0; x < ChunkSize; x++)
		{
			for (int y = 0; y < ChunkSize; y++)
			{
				heights->Height(x + cpos.x * ChunkSize, y + cpos.y * ChunkSize) = simplex(glm::vec2(x + cpos.x * ChunkSize, y + cpos.y * ChunkSize) * 0.02f, 4, 0.5f) * 20;
			}
		}
		heights->LastX(cpos.x, cpos.y) = cpos.x;
		heights->LastY(cpos.x, cpos.y) = cpos.y;
	}

	for (int x = 0; x < ChunkSize; x++)
	{
		for (int y = 0; y < ChunkSize; y++)
		{
			int height = heights->Height(x + cpos.x * ChunkSize, y + cpos.y * ChunkSize);
			if (height >= cpos.z * ChunkSize)
			{
				for (int z = 0; z < ChunkSize; z++)
				{
					if (z + cpos.z * ChunkSize == height)
					{
						chunk.block[x][y][z].shape = 255;
						chunk.block[x][y][z].color = GetColor(cpos.x * ChunkSize + x, cpos.y * ChunkSize + y);
						break;
					}
					chunk.block[x][y][z].shape = 255;
					chunk.block[x][y][z].color = 50;
				}
			}
		}
	}
}

// ============================

struct Map
{
	Map();

	MapChunk Read(glm::ivec3 cpos)
	{
		MapChunk* mc = m_chunks[cpos];
		if (!mc)
		{
			mc = new MapChunk;
			GenerateTerrain(cpos, *mc);
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

	void Set(glm::ivec3 pos, Block block)
	{
		MapChunk* mc = m_chunks[pos >> ChunkSizeBits];
		if (!mc)
		{
			mc = new MapChunk;
			GenerateTerrain(pos >> ChunkSizeBits, *mc);
			m_chunks[pos >> ChunkSizeBits] = mc;
		}
		mc->block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask] = block;
	}

private:
	struct KeyHash
	{
		size_t operator()(glm::ivec3 a) const { return a.x * 7919 + a.y * 7537 + a.z * 7687; }
	};

	std::unordered_map<glm::ivec3, MapChunk*, KeyHash> m_chunks;

	void Set(glm::ivec3 pos, unsigned char shape, glm::vec3 color)
	{
		Block block;
		block.shape = shape;
		block.color = ColorToCode(color);
		Set(pos, block);
	}
};

int S(int a) { return 1 << a; }

Map::Map()
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

				Set(glm::ivec3(x+1, y-10, z+10), shape, glm::vec3(1, 1, 0));
			}
		}
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

struct Chunk
{
	Block block[ChunkSize][ChunkSize][ChunkSize];
	bool empty, full;
	bool full0[3];
	bool full1[3];
};

Chunk* map_cache[MapSize][MapSize][MapSize];

int map_xmin = 1000000000, map_ymin = 1000000000, map_zmin = 1000000000;

Chunk& GetChunk(glm::ivec3 cpos)
{
	return *map_cache[cpos.x & MapSizeMask][cpos.y & MapSizeMask][cpos.z & MapSizeMask];
}

// Used for rendering and collision detection
unsigned char map_get(glm::ivec3 pos)
{
	return GetChunk(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask].shape;
}

unsigned char map_get_color(glm::ivec3 pos)
{
	return GetChunk(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask].color;
}

Block& map_get_block(glm::ivec3 pos)
{
	return GetChunk(pos >> ChunkSizeBits).block[pos.x & ChunkSizeMask][pos.y & ChunkSizeMask][pos.z & ChunkSizeMask];
}

float map_refresh_time_ms = 0;

bool IsEmpty(Chunk& chunk)
{
	for (int x = 0; x < ChunkSize; x++)
	{
		for (int y = 0; y < ChunkSize; y++)
		{
			for (int z = 0; z < ChunkSize; z++)
			{
				if (chunk.block[x][y][z].shape != 0)
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool IsFull(Chunk& chunk, int xmin, int xmax, int ymin, int ymax, int zmin, int zmax)
{
	for (int x = xmin*(ChunkSize-1); x <= xmax*(ChunkSize-1); x++)
	{
		for (int y = ymin*(ChunkSize-1); y <= ymax*(ChunkSize-1); y++)
		{
			for (int z = zmin*(ChunkSize-1); z <= zmax*(ChunkSize-1); z++)
			{
				if (chunk.block[x][y][z].shape == 0)
				{
					return false;
				}
			}
		}
	}
	return true;
}

void LoadChunk(Chunk& chunk, glm::ivec3 cpos)
{
	MapChunk mc = map->Read(cpos);
	memcpy(chunk.block, mc.block, sizeof(mc.block));

	chunk.empty = IsEmpty(chunk);

	chunk.full0[0] = IsFull(chunk, 0,0, 0,1, 0,1);
	chunk.full1[0] = IsFull(chunk, 1,1, 0,1, 0,1);

	chunk.full0[1] = IsFull(chunk, 0,1, 0,0, 0,1);
	chunk.full1[1] = IsFull(chunk, 0,1, 1,1, 0,1);

	chunk.full0[2] = IsFull(chunk, 0,1, 0,1, 0,0);
	chunk.full1[2] = IsFull(chunk, 0,1, 0,1, 1,1);

	chunk.full = chunk.full0[0] && chunk.full0[1] && chunk.full0[2] && chunk.full1[0] && chunk.full1[1] && chunk.full1[2];
}

// extend world if player moves
void map_refresh(glm::ivec3 player)
{
	glm::ivec3 cplayer = player / ChunkSize;
	if (cplayer.x == map_xmin + MapSize / 2 && cplayer.y == map_ymin + MapSize / 2 && cplayer.z == map_zmin + MapSize / 2)
	{
		return;
	}

	float time_start = glfwGetTime();
	int xmin = cplayer.x - MapSize / 2;
	int ymin = cplayer.y - MapSize / 2;
	int zmin = cplayer.z - MapSize / 2;
	for (int x = xmin; x < xmin + MapSize; x++)
	{
		for (int y = ymin; y < ymin + MapSize; y++)
		{
			for (int z = zmin; z < zmin + MapSize; z++)
			{
				if ((x & MapSizeMask) + map_xmin != x || (y & MapSizeMask) + map_ymin != y || (z & MapSizeMask) + map_zmin != z)
				{
					LoadChunk(*map_cache[x & MapSizeMask][y & MapSizeMask][z & MapSizeMask], glm::ivec3(x, y, z));
				}
			}
		} 
	}
	map_xmin = xmin;
	map_ymin = ymin;
	map_zmin = zmin;
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

static const glm::ivec3 ix(1, 0, 0), iy(0, 1, 0), iz(0, 0, 1);

void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{

}

void OnMouseButton(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && selection)
	{
		map_get_block(sel_cube).shape = 0;
		GetChunk(sel_cube).full = false;
		map->Set(sel_cube, map_get_block(sel_cube));
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		map_get_block(sel_cube + dir[sel_face]) = map_get_block(sel_cube);
		GetChunk(sel_cube + dir[sel_face]).empty = false;
		GetChunk(sel_cube + dir[sel_face]).full = false;
		map->Set(sel_cube, map_get_block(sel_cube + dir[sel_face]));
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
			}
		}
	}

	InitColorCodes();
}

float sqr(float a) { return a * a; }

// Move sphere so that it is not coliding with cube
// Note: doesn't handle vertex and edge case (returns false)
int SphereVsCube(glm::vec3& center, float radius, glm::ivec3 cube)
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
		if (y > +e || y < -e) return -1;
		if (z > +e || z < -e) return -1;
		center.x += radius - x + e;
		return 1;
	}

	if (x < -e)
	{
		if (y > +e || y < -e) return -1;
		if (z > +e || z < -e) return -1;
		center.x += -radius - x - e;
		return 1;
	}

	if (y > +e)
	{
		if (z > +e || z < -e) return -1;
		center.y += radius - y + e;
		return 1;
	}
	if (y < -e)
	{
		if (z > +e || z < -e) return -1;
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

void ResolveCollisionsWithBlocks()
{
	int px = roundf(player_position.x);
	int py = roundf(player_position.y);
	int pz = roundf(player_position.z);

	for (int x = px - 2; x <= px + 2; x++)
	{
		for (int y = py - 2; y <= py + 2; y++)
		{
			for (int z = pz - 2; z <= pz + 2; z++)
			{
				if (map_get(glm::ivec3(x, y, z)) != 0)
				{
					SphereVsCube(player_position, 1, glm::ivec3(x, y, z));
				}
			}
		}
	}
}

void model_move_player(GLFWwindow* window, float dt)
{
	glm::vec3 dir(0, 0, 0);

	float* m = glm::value_ptr(player_orientation);
	glm::vec3 forward(m[4], m[5], m[6]);
	glm::vec3 right(m[0], m[1], m[2]);

	if (glfwGetKey(window, 'A')) dir -= right;
	if (glfwGetKey(window, 'D')) dir += right;
	if (glfwGetKey(window, 'W')) dir += forward;
	if (glfwGetKey(window, 'S')) dir -= forward;
	if (glfwGetKey(window, 'Q')) dir[2] += 1;
	if (glfwGetKey(window, 'E')) dir[2] -= 1;

	if (dir.x != 0 || dir.y != 0 || dir.z != 0)
	{
		dir = glm::normalize(dir);
		float dp = 10 * dt; // how much to move this frame
		while (dp > 0)
		{
			float d = (dp > 0.1) ? 0.1 : dp;
			player_position += dir * d;
			ResolveCollisionsWithBlocks();
			dp -= d;
		}
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
	for (int x = px - 5; x <= px + 5; x++)
	{
		for (int y = py - 5; y <= py + 5; y++)
		{
			for (int z = pz - 5; z <= pz + 5; z++)
			{
				glm::ivec3 i(x, y, z);
				if (map_get(i) != 0)
				{
					float dist; int face;
					if (IntersectRayCube(player_position, dir, i, /*out*/dist, /*out*/face) && dist > 0 && dist <= 5 && (!found || dist < sel_dist))
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

void model_frame(GLFWwindow* window)
{
	double time = glfwGetTime();
	double dt = (time - last_time) < 0.5 ? (time - last_time) : 0.5;
	last_time = time;

	if (glfwGetKey(window, GLFW_KEY_ESCAPE))
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	double cursor_x, cursor_y;
	glfwGetCursorPos(window, &cursor_x, &cursor_y);
	if (cursor_x != 0 || cursor_y != 0)
	{
		player_yaw += (cursor_x) / 100;
		player_pitch += (cursor_y) / 100;
		if (player_pitch > M_PI / 2) player_pitch = M_PI / 2;
		if (player_pitch < -M_PI / 2) player_pitch = -M_PI / 2;
		glfwSetCursorPos(window, 0, 0);
		player_orientation = glm::rotate(glm::rotate(glm::mat4(), -player_yaw, glm::vec3(0, 0, 1)), -player_pitch, glm::vec3(1, 0, 0));

		perspective_rotation = glm::rotate(perspective, player_pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, player_yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
	}
	
	model_move_player(window, dt);
	
	map_refresh(glm::ivec3(player_position.x, player_position.y, player_position.z));

	selection = SelectCube(/*out*/sel_cube, /*out*/sel_dist, /*out*/sel_face);
}

// Render

GLuint block_texture;
glm::vec3 light_direction(0.5, 1, -1);

void glVertex(glm::ivec3 v)
{
	glVertex3iv(glm::value_ptr(v));
}

void glVertex(glm::vec3 v)
{
	glVertex3fv(glm::value_ptr(v));
}

void glColor(glm::vec3 v)
{
	glColor3fv(glm::value_ptr(v));
}

float light_cache[8][8][8];

void InitLightCache()
{
	for (int a = 0; a < 8; a++)
	{
		for (int b = 0; b < 8; b++)
		{
			for (int c = 0; c < 8; c++)
			{
				glm::vec3 normal = glm::normalize((glm::vec3)glm::cross(corner[b] - corner[a], corner[c] - corner[a]));
				float cos_angle = std::max<float>(0, -glm::dot(normal, light_direction));
				light_cache[a][b][c] = 0.3f + cos_angle * 0.7f;
			}
		}
	}
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
	load_png_texture("noise.png");

	light_direction = glm::normalize(light_direction);
	InitLightCache();

	GLfloat fogColor[4] = {0.2, 0.4, 1, 1.0};
	glFogfv(GL_FOG_COLOR, fogColor);
	glFogf(GL_FOG_DENSITY, 1);
	glFogf(GL_FOG_START, 110.0f);
	glFogf(GL_FOG_END, 150.0f);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glHint(GL_FOG_HINT, GL_NICEST);

	perspective = glm::perspective<float>(M_PI / 180 * 75, width / (float)height, 0.03, 1000);
}

void light_color(int a, int b, int c, glm::vec3 color)
{
	glColor(color * light_cache[a][b][c]);
}

int face_count = 0;

void draw_quad(glm::ivec3 pos, int a, int b, int c, int d, glm::vec3 color)
{
	face_count += 2;
	light_color(a, b, c, color);

	glTexCoord2f(0, 0); glVertex(pos + corner[a]);
	glTexCoord2f(0, 1); glVertex(pos + corner[b]);
	glTexCoord2f(1, 1); glVertex(pos + corner[c]);

	glTexCoord2f(0, 0); glVertex(pos + corner[a]);
	glTexCoord2f(1, 1); glVertex(pos + corner[c]);
	glTexCoord2f(1, 0); glVertex(pos + corner[d]);
}

void draw_triangle(glm::ivec3 pos, int a, int b, int c, glm::vec3 color)
{
	face_count += 1;
	light_color(a, b, c, color);

	glTexCoord2f(0, 0); glVertex(pos + corner[a]);
	glTexCoord2f(0, 1); glVertex(pos + corner[b]);
	glTexCoord2f(1, 1); glVertex(pos + corner[c]);
}

int B(int a, int i) { return (a & (1 << i)) >> i; }

void draw_face(glm::ivec3 pos, int block, int a, int b, int c, int d, glm::vec3 color)
{
	int vertices = B(block, a) + B(block, b) + B(block, c) + B(block, d);

	if (vertices == 4)
	{
		draw_quad(pos, a, b, c, d, color);
	}
	else if (vertices == 3)
	{
		face_count += 1;
		light_color(a, b, c, color);

		if (block & (1 << a)) { glTexCoord2f(0, 0); glVertex(pos + corner[a]); }
		if (block & (1 << b)) { glTexCoord2f(0, 1); glVertex(pos + corner[b]); }
		if (block & (1 << c)) { glTexCoord2f(1, 1); glVertex(pos + corner[c]); }
		if (block & (1 << d)) { glTexCoord2f(1, 0); glVertex(pos + corner[d]); }
	}
}

void render_block(glm::ivec3 pos, glm::vec3 color)
{
	if (map_get(pos - ix) == 0)
	{
		draw_quad(pos, 0, 4, 6, 2, color);
	}
	if (map_get(pos + ix) == 0)
	{
		draw_quad(pos, 1, 3, 7, 5, color);
	}
	if (map_get(pos - iy) == 0)
	{
		draw_quad(pos, 0, 1, 5, 4, color);
	}
	if (map_get(pos + iy) == 0)
	{
		draw_quad(pos, 2, 6, 7, 3, color);
	}
	if (map_get(pos - iz) == 0)
	{
		draw_quad(pos, 0, 2, 3, 1, color);
	}
	if (map_get(pos + iz) == 0)
	{
		draw_quad(pos, 4, 5, 7, 6, color);
	}
}

// every bit from 0 to 7 in block represents once vertex (can be off or on)
// cube is 255, empty is 0, prisms / pyramids / anti-pyramids are in between
void render_general(glm::ivec3 pos, int block, glm::vec3 color)
{
	if (block == 255)
	{
		render_block(pos, color);
		return;
	}

	// common faces
	if (map_get(pos - ix) != 255) // TODO do more strict elimination!
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
	// far
/*	frustum[4][0] = clip[ 3] - clip[ 2];
	frustum[4][1] = clip[ 7] - clip[ 6];
	frustum[4][2] = clip[11] - clip[10];
	frustum[4][3] = clip[15] - clip[14];
	// near
	frustum[5][0] = clip[ 3] + clip[ 2];
	frustum[5][1] = clip[ 7] + clip[ 6];
	frustum[5][2] = clip[11] + clip[10];
	frustum[5][3] = clip[15] + clip[14];*/

	for (int i = 0; i < 4; i++)
	{
		frustum[i] *= glm::fastInverseSqrt(sqr(frustum[i].xyz()));
	}
}

bool SphereInFrustum(glm::vec3 p, float radius)
{
	for (int i = 0; i < 4; i++)
	{
		if (glm::dot(p, frustum[i].xyz()) + frustum[i].w <= -radius)
			return false;
	}
	return true;
}

int ClassifySphereInFrustum(glm::vec3 p, float radius)
{
	int c = 1; // completely inside
	for (int i = 0; i < 4; i++)
	{
		float d = glm::dot(p, frustum[i].xyz()) + frustum[i].w;
		if (d < -radius)
		{
			return -1; // completely outside
		}
		if (d <= radius)
		{
			c = 0; // intersecting the border
		}
	}
	return c;
}

float block_render_time_ms = 0;
float block_render_time_ms_avg = 0;

const float BlockRadius = sqrtf(3) / 2;

void render_chunk(glm::ivec3 cplayer, glm::ivec3 direction, glm::ivec3 cdelta)
{
	glm::ivec3 c = cplayer + cdelta;
	Chunk& chunk = GetChunk(c);
	if (chunk.empty)
		return;

	// underground chunk culling!
	if (chunk.full)
		if (GetChunk(c - ix).full1[0] && GetChunk(c + ix).full0[0] && GetChunk(c - iy).full1[1] && GetChunk(c + iy).full0[1]  && GetChunk(c - iz).full1[2] && GetChunk(c + iz).full0[2])
			return;

	// TODO very cheap integer behind-the-camera check for chunk!
	/*if (glm::dot(direction, delta) < 0)
		return;*/

	glm::ivec3 d = (cplayer + cdelta) * ChunkSize;
	glm::ivec3 p = d + (ChunkSize / 2);
	int cl = ClassifySphereInFrustum(glm::vec3(p.x, p.y, p.z), ChunkSize * BlockRadius);
	if (cl == -1)
		return;

	if (cl == 0)
	{
		// do frustrum checks for each voxel
		for (int x = 0; x < ChunkSize; x++)
		{
			for (int y = 0; y < ChunkSize; y++)
			{
				for (int z = 0; z < ChunkSize; z++)
				{
					Block block = chunk.block[x][y][z];
					if (block.shape == 0)
						continue;
					/*if (glm::dot(direction, delta) < 0)
						return;*/
					glm::ivec3 pos = d + glm::ivec3(x, y, z);
					if (!SphereInFrustum(glm::vec3(pos.x + 0.5, pos.y + 0.5, pos.z + 0.5), BlockRadius))
						continue;
					render_general(pos, block.shape, color_codes[block.color]);
				}
			}
		}
	}
	if (cl == 1)
	{
		for (int x = 0; x < ChunkSize; x++)
		{
			for (int y = 0; y < ChunkSize; y++)
			{
				for (int z = 0; z < ChunkSize; z++)
				{
					Block block = chunk.block[x][y][z];
					if (block.shape == 0)
						continue;
					glm::ivec3 pos = d + glm::ivec3(x, y, z);
					render_general(pos, block.shape, color_codes[block.color]);
				}
			}
		}
	}
}

glm::vec3 Q(glm::ivec3 a, glm::vec3 c)
{
	return (glm::vec3(a) - c) * 1.02f + c;
}

void render_world_blocks(const glm::mat4& matrix)
{
	float time_start = glfwGetTime();
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, block_texture);
	glm::ivec3 cplayer = glm::ivec3(player_position) >> ChunkSizeBits;

	InitFrustum(matrix);

	float* ma = glm::value_ptr(player_orientation);
	glm::ivec3 direction(ma[4] * (1 << 20), ma[5] * (1 << 20), ma[6] * (1 << 20));

	glBegin(GL_TRIANGLES);
	for (int cx = -RENDER_LIMIT; cx <= RENDER_LIMIT; cx++)
	{
		render_chunk(cplayer, direction, glm::ivec3(cx, 0, 0));
		int RX = RENDER_LIMIT * RENDER_LIMIT - cx * cx;
		for (int cz = 1; cz * cz <= RX; cz++)
		{
			render_chunk(cplayer, direction, glm::ivec3(cx, 0, cz));
			render_chunk(cplayer, direction, glm::ivec3(cx, 0, -cz));
		}

		for (int cy = 1; cy * cy <= RX; cy++)
		{
			render_chunk(cplayer, direction, glm::ivec3(cx, cy, 0));
			render_chunk(cplayer, direction, glm::ivec3(cx, -cy, 0));

			int RXY = RX - cy * cy;
			for (int cz = 1; cz * cz <= RXY; cz++)
			{
				render_chunk(cplayer, direction, glm::ivec3(cx, cy, cz));
				render_chunk(cplayer, direction, glm::ivec3(cx, cy, -cz));
				render_chunk(cplayer, direction, glm::ivec3(cx, -cy, cz));
				render_chunk(cplayer, direction, glm::ivec3(cx, -cy, -cz));
			}
		}
	}

	glEnd();
	glDisable(GL_TEXTURE_2D);
	block_render_time_ms = (glfwGetTime() - time_start) * 1000;
	block_render_time_ms_avg = block_render_time_ms_avg * 0.8f + block_render_time_ms * 0.2f;

	if (selection)
	{
		glLineWidth(2);
		glLogicOp(GL_XOR);
		glColor3f(1,1,1);

		glEnable(GL_COLOR_LOGIC_OP);
		glBegin(GL_LINES);
		glm::vec3 c = glm::vec3(sel_cube) + 0.5f;
		for (int i = 0; i <= 1; i++)
		{
			for (int j = 0; j <= 1; j++)
			{
				glm::ivec3 sel = sel_cube;
				glVertex(Q(sel + i*iy + j*iz, c)); glVertex(Q(sel + ix + i*iy + j*iz, c));
				glVertex(Q(sel + i*ix + j*iz, c)); glVertex(Q(sel + iy + i*ix + j*iz, c));
				glVertex(Q(sel + i*ix + j*iy, c)); glVertex(Q(sel + iz + i*ix + j*iy, c));
			}
		}
		glEnd();

/*		int faces[6][4] = { { 0, 4, 6, 2 },
					  { 1, 3, 7, 5 },
					  { 0, 1, 5, 4 },
					  { 2, 6, 7, 3 },
					  { 0, 2, 3, 1 },
					  { 4, 5, 7, 6 } };
		int* face = faces[sel_face];
		glBegin(GL_QUADS);
		for(int i = 0; i< 4; i++) glVertex(Q(sel_cube + corner[face[i]], c));
		glEnd();*/
		glDisable(GL_COLOR_LOGIC_OP);
	}
}

void render_world()
{
	glm::mat4 matrix = glm::translate(perspective_rotation, -player_position);
	glLoadMatrixf(glm::value_ptr(matrix));
	
	glEnable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_FOG);
	render_world_blocks(matrix);
	glDisable(GL_FOG);

	glDisable(GL_DEPTH_TEST);
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);
	
	// Text test
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glUseProgram(text_program);
	glUniformMatrix4fv(text_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform1i(text_sampler_loc, 0/*text_texture*/);
	char text_buffer[1024];
	float ts = height / 80;
	float tx = ts / 2;
	float ty = height - ts;
	snprintf(text_buffer, sizeof(text_buffer), "position: %.1f %.1f %.1f, face_count: %d, blocks: %.0fms, map: %.0f",
		player_position[0], player_position[1], player_position[2], face_count, block_render_time_ms_avg, map_refresh_time_ms);
	face_count = 0;
	text_print(text_position_loc, text_uv_loc, tx, ty, ts, text_buffer);

	if (selection)
	{
		ty -= ts * 2;
		snprintf(text_buffer, sizeof(text_buffer), "selected: cube [%d %d %d], face %d, distance %.1f", sel_cube.x, sel_cube.y, sel_cube.z, sel_face, sel_dist);
		text_print(text_position_loc, text_uv_loc, tx, ty, ts, text_buffer);
	}
	glUseProgram(0);

	glLineWidth(4);
	glLogicOp(GL_INVERT);
	glEnable(GL_COLOR_LOGIC_OP);
	glLoadMatrixf(glm::value_ptr(matrix));
	glBegin(GL_LINES);
	glColor3f(1,1,1);

	glVertex2f(width/2 - 20, height / 2);
	glVertex2f(width/2 + 20, height / 2);

	glVertex2f(width/2, height / 2 - 20);
	glVertex2f(width/2, height / 2 + 20);
	glEnd();
	glDisable(GL_COLOR_LOGIC_OP);


	#ifdef NEVER
	// Simple quad
	glUseProgram(text_program);
	glUniformMatrix4fv(text_matrix_loc, 1, GL_FALSE, matrix);
		glUniform1i(text_sampler_loc, 0/*text_texture*/);

	GLuint vao_quad;
	glGenVertexArrays(1, &vao_quad);
		glBindVertexArray(vao_quad);

	float vertices[] = { 0, 0, 100, 0, 0, 100 };
	int v = gen_buffer(GL_ARRAY_BUFFER, sizeof(vertices), vertices);
	float colors[] = { 0, 0, 100, 0, 0, 100 };
	int c = gen_buffer(GL_ARRAY_BUFFER, sizeof(colors), colors);

		glDrawArrays(GL_QUADS, 0, 4);

	glDeleteVertexArrays(1, &vao_quad);
	#endif
}

void render_frame()
{
	glViewport(0, 0, width, height);
	render_world();
	render_gui();
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

	//glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	//glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(1524, 1200, "Arena", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		return 0;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(VSYNC);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);

	model_init(window);
	render_init();
	text_init();
	
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
