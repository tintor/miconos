#include "rendering.hh"

#include <cmath>
#include <cstdarg>
#include <string>
#include <iostream>
#include <fstream>

#include "glm/gtc/type_ptr.hpp"

void Error(const char* name)
{
	int error = glGetError();
	if (error != GL_NO_ERROR)
	{
		fprintf(stderr, "Error %d: %s\n", error, name);
		exit(1);
	}
}

// Render::Buffers

int gen_buffer(GLenum target, GLsizei size, const void* data)
{
	GLuint buffer;
	fprintf(stderr, "Marko\n");
	glGenBuffers(1, &buffer);
	Error("Ren1a");
	glBindBuffer(target, buffer);
	Error("Ren1b");
	glBufferData(target, size, data, GL_STATIC_DRAW);
	Error("Ren1c");
	glBindBuffer(target, 0);
	Error("Ren1d");
	return buffer;
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

static GLuint text_texture;
static GLuint text_program;
static GLuint text_matrix_loc;
static GLuint text_sampler_loc;
static GLuint text_position_loc;
static GLuint text_uv_loc;

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

void text_gen_buffers(GLuint position_buffer, GLuint uv_buffer, float x, float y, float n, const char* text)
{
	int length = strlen(text);
	GLfloat* position_data = new GLfloat[length * 6 * 2];
	GLfloat* uv_data = new GLfloat[length * 6 * 2];

	for (int i = 0; i < length; i++)
	{
		make_character(position_data + i * 12, uv_data + i * 12, x, y, n / 2, n, text[i]);
		x += n;
	}

	Error("gena0");
	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	Error("gena1");
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * length * 6 * 2, position_data, GL_STATIC_DRAW);
	Error("gena2");
	glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
	Error("gena3");
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * length * 6 * 2, uv_data, GL_STATIC_DRAW);
	Error("gena4");
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	Error("gena5");

	delete[] position_data;
	delete[] uv_data;
}

void text_draw_buffers(GLuint position_buffer, GLuint uv_buffer, GLuint position_loc, GLuint uv_loc, int length)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	Error("RenA");
	glEnableVertexAttribArray(text_position_loc);
	glEnableVertexAttribArray(text_uv_loc);
	Error("RenB");

	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	glVertexAttribPointer(position_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
	Error("RenC");
	glVertexAttribPointer(uv_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDrawArrays(GL_TRIANGLES, 0, length * 6);
	Error("RenD");

	glDisableVertexAttribArray(position_loc);
	glDisableVertexAttribArray(uv_loc);
	glDisable(GL_BLEND);
	Error("RenE");
}

Text::Text()
{
	text_program = load_program("shaders/text_vertex.glsl", "shaders/text_fragment.glsl");
	text_matrix_loc = glGetUniformLocation(text_program, "matrix");
	text_sampler_loc = glGetUniformLocation(text_program, "sampler");
	text_position_loc = glGetAttribLocation(text_program, "position");
	fprintf(stderr, "pos_loc=%d\n", text_position_loc);
	text_uv_loc = glGetAttribLocation(text_program, "uv");
	glBindFragDataLocation(text_program, 0, "color");
	Error("Text::Text");

	glGenBuffers(1, &m_positionBuffer);
	glGenBuffers(1, &m_uvBuffer);

	glGenTextures(1, &text_texture);
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	load_png_texture("font.png");
}

void Text::Reset(int height, glm::mat4& matrix)
{
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glUseProgram(text_program);
	glUniformMatrix4fv(text_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform1i(text_sampler_loc, 0/*text_texture*/);
	m_ts = height / 80;
	m_tx = m_ts / 2;
	m_ty = height - m_ts;
}

void Text::Printf(const char* format, ...)
{
	char buffer[1024];
	va_list va;
	va_start(va, format);
	vsnprintf(buffer, sizeof(buffer), format, va);
	va_end(va);

	PrintAt(m_tx, m_ty, m_ts, buffer);
	m_ty -= m_ts * 2;
}

void Text::PrintAt(float x, float y, float n, const char* text)
{
	text_gen_buffers(m_positionBuffer, m_uvBuffer, x, y, n, text);
	Error("Ren1");
	text_draw_buffers(m_positionBuffer, m_uvBuffer, text_position_loc, text_uv_loc, strlen(text));
	Error("Ren2");
	//glDeleteBuffers(1, &position_buffer);
	//glDeleteBuffers(1, &uv_buffer);
	Error("Ren3");
}
