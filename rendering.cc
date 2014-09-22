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
	glBindBuffer(target, buffer);
	glBufferData(target, size, data, GL_STATIC_DRAW);
	glBindBuffer(target, 0);
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

std::string read_file(const char* filename)
{
	std::ifstream in(filename, std::ios::in | std::ios::binary);
	if (!in)
	{
		fprintf(stderr, "File '%s' not found\n", filename);
	}
	std::string contents;
	in.seekg(0, std::ios::end);
	contents.resize(in.tellg());
	in.seekg(0, std::ios::beg);
	in.read(&contents[0], contents.size());
	in.close();
	return contents;
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

GLuint load_shader(GLenum type, const char* filename)
{
	return make_shader(type, read_file(filename));
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

GLuint load_program(const char* name)
{
	char* path1;
	char* path2;
	asprintf(&path1, "shaders/%s.vert", name);
	asprintf(&path2, "shaders/%s.frag", name);
	GLuint shader1 = load_shader(GL_VERTEX_SHADER, path1);
	GLuint shader2 = load_shader(GL_FRAGMENT_SHADER, path2);
	free(path1);
	free(path2);
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

	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * length * 6 * 2, position_data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * length * 6 * 2, uv_data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	delete[] position_data;
	delete[] uv_data;
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

Text::Text()
{
	text_program = load_program("text");
	text_matrix_loc = glGetUniformLocation(text_program, "matrix");
	text_sampler_loc = glGetUniformLocation(text_program, "sampler");
	text_position_loc = glGetAttribLocation(text_program, "position");
	fprintf(stderr, "pos_loc=%d\n", text_position_loc);
	text_uv_loc = glGetAttribLocation(text_program, "uv");
	glBindFragDataLocation(text_program, 0, "color");

	glGenBuffers(1, &m_positionBuffer);
	glGenBuffers(1, &m_uvBuffer);

	glGenTextures(1, &text_texture);
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	load_png_texture("font.png");
}

void Text::Reset(int width, int height, glm::mat4& matrix)
{
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glUseProgram(text_program);
	glUniformMatrix4fv(text_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform1i(text_sampler_loc, 0/*text_texture*/);
	m_ts = (height > width) ? height / 160 : height / 80;
	m_tx = m_ts / 2;
	m_ty = height - m_ts;
}

void Text::Print(const char* text, int length)
{
	PrintAt(m_tx, m_ty, m_ts, text, length);
	m_ty -= m_ts * 2;
}

void Text::Printf(const char* format, ...)
{
	char buffer[1024];
	va_list va;
	va_start(va, format);
	vsnprintf(buffer, sizeof(buffer), format, va);
	va_end(va);

	PrintAt(m_tx, m_ty, m_ts, buffer, strlen(buffer));
	m_ty -= m_ts * 2;
}

void Text::PrintAt(float x, float y, float n, const char* text, int length)
{
	text_gen_buffers(m_positionBuffer, m_uvBuffer, x, y, n, text);
	text_draw_buffers(m_positionBuffer, m_uvBuffer, text_position_loc, text_uv_loc, length);
}

Console::Console()
{
	memset(m_output, ' ', ConsoleWidth * ConsoleHeight);
	memset(m_input, ' ', ConsoleWidth);
	m_input[m_cursor_pos++] = '>';
}

bool Console::KeyToChar(int key, int mods, char& ch)
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
	case GLFW_KEY_SLASH: ch = shift ? '?' : '/'; break;
	case GLFW_KEY_SEMICOLON: ch = shift ? ':' : ';'; break;
	case GLFW_KEY_APOSTROPHE: ch = shift ? '"' : '\''; break;
	case GLFW_KEY_EQUAL: ch = shift ? '+' : '='; break;
	case GLFW_KEY_MINUS: ch = shift ? '_' : '-'; break;
	case GLFW_KEY_LEFT_BRACKET: ch = shift ? '{' : '['; break;
	case GLFW_KEY_RIGHT_BRACKET: ch = shift ? '}' : ']'; break;
	case GLFW_KEY_BACKSLASH: ch = shift ? '|' : '\\'; break;
	case GLFW_KEY_WORLD_1: ch = shift ? '~' : '`'; break;
	case GLFW_KEY_SPACE: ch = ' '; break;
	default: return false;
	}
	return true;
}

void Console::PrintLine(const char* str)
{
	m_last_line = (m_last_line + 1) % ConsoleHeight;
	int length = std::min<int>(ConsoleWidth, strlen(str));
	memcpy(m_output[m_last_line], str, length);
	memset(m_output[m_last_line] + length, ' ', ConsoleWidth - length);
}

bool Console::OnKey(int key, int mods)
{
	char ch;
	if (key == GLFW_KEY_BACKSPACE)
	{
		if (m_cursor_pos > 1)
		{
			m_input[m_cursor_pos--] = ' ';
		}
		return true;
	}
	if (key == GLFW_KEY_ENTER)
	{
		m_input[m_cursor_pos] = 0;
		Execute(m_input + 1);
		memset(m_input + 1, ' ', ConsoleWidth);
		m_cursor_pos = 1;
		return true;
	}
	if (KeyToChar(key, mods, /*out*/ch))
	{
		if (m_cursor_pos + 1 < ConsoleWidth)
		{
			m_input[m_cursor_pos++] = ch;
		}
		return true;
	}
	return false;
}

void Console::Render(Text* text, float time)
{
	if (!m_visible)
		return;

	for (int i = m_last_line + 1; i < ConsoleHeight; i++)
	{
		text->Print(m_output[i], ConsoleWidth);
	}
	for (int i = 0; i <= m_last_line; i++)
	{
		text->Print(m_output[i], ConsoleWidth);
	}

	m_input[m_cursor_pos] = (fmod(time, 1.6f) <= 0.8) ? '_' : ' ';
	text->Print(m_input, ConsoleWidth);
}
