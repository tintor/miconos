#include "rendering.hh"
#include "util.hh"
#include "algorithm.hh"
#include "auto.hh"

#include <iostream>
#include <fstream>

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

GLuint load_shader(GLenum type, const char* name, const char* ext)
{
	char* filename;
	asprintf(&filename, "shaders/%s.%s", name, ext);
	Auto(free(filename));
	return make_shader(type, read_file(filename));
}

GLuint make_program(const ivector<GLuint, 3>& shaders)
{
	GLuint program = glCreateProgram();
	for (GLuint shader : shaders) glAttachShader(program, shader);
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
	for (GLuint shader : shaders) glDetachShader(program, shader);
	for (GLuint shader : shaders) glDeleteShader(shader);
	return program;
}

GLuint load_program(const char* name, bool geometry)
{
	ivector<GLuint, 3> shaders;
	shaders.push_back(load_shader(GL_VERTEX_SHADER, name, "vert"));
	if (geometry) shaders.push_back(load_shader(GL_GEOMETRY_SHADER, name, "geom"));
	shaders.push_back(load_shader(GL_FRAGMENT_SHADER, name, "frag"));
	return make_program(shaders);
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

void text_gen_buffers(GLuint position_buffer, GLuint uv_buffer, float x, float y, float n, const char* text, std::vector<GLfloat>& position_data, std::vector<GLfloat>& uv_data)
{
	int length = strlen(text);
	position_data.resize(length * 6 * 2);
	uv_data.resize(length * 6 * 2);

	for (int i = 0; i < length; i++)
	{
		make_character(&position_data[0] + i * 12, &uv_data[0] + i * 12, x, y, n / 2, n, text[i]);
		x += n;
	}

	glBindBuffer(GL_ARRAY_BUFFER, position_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * length * 6 * 2, &position_data[0], GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * length * 6 * 2, &uv_data[0], GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
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

void Text::Print(const char* format, ...)
{
	char buffer[1024];
	va_list va;
	va_start(va, format);
	int length = vsnprintf(buffer, sizeof(buffer), format, va);
	va_end(va);

	PrintBuffer(buffer, length);
}

void Text::PrintBuffer(const char* buffer, int length)
{
	PrintAt(m_tx, m_ty, m_ts, buffer, length);
	m_ty -= m_ts * 2;
}

void Text::PrintAt(float x, float y, float n, const char* text, int length)
{
	text_gen_buffers(m_positionBuffer, m_uvBuffer, x, y, (n == 0) ? m_ts : n, text, m_position_data, m_uv_data);
	text_draw_buffers(m_positionBuffer, m_uvBuffer, text_position_loc, text_uv_loc, length);
}

Console::Console()
{
	memset(m_output, ' ', ConsoleWidth * ConsoleHeight);

	Input* p = new Input;
	p->cursor = 1;
	p->buffer[0] = '>';
	memset(p->buffer + 1, ' ', ConsoleWidth - 1);
	m_inputs.push_back(p);
	m_current_input = 0;
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

void Console::Print(const char* fmt, ...)
{
	AutoLock(m_mutex);
	char buffer[10240];
	va_list va;
	va_start(va, fmt);
	int length = vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	char* w = m_output[m_last_line];
	FOR(i, length)
	{
		if (buffer[i] == '\n')
		{
			m_last_column = ConsoleWidth;
			continue;
		}
		if (m_last_column == ConsoleWidth)
		{
			m_last_line += 1;
			if (m_last_line == ConsoleHeight) m_last_line = 0;
			m_last_column = 0;
			w = m_output[m_last_line];
			memset(w, ' ', ConsoleWidth);
		}
		w[m_last_column++] = buffer[i];
	}
}

bool contains_non_space(const char* a, int length)
{
	FOR(i, length) if (a[i] != ' ') return true;
	return false;
}

// TODO left / right move cursor to make edits
bool Console::OnKey(int key, int mods)
{
	char ch;
	Input& input = *m_inputs[m_current_input];
	if (key == GLFW_KEY_BACKSPACE)
	{
		if (input.cursor > 1) input.buffer[input.cursor--]  = ' ';
		return true;
	}
	if (key == GLFW_KEY_ENTER && contains_non_space(input.buffer + 1, input.cursor - 1))
	{
		Execute(input.buffer + 1, input.cursor - 1);
		if (m_current_input != m_inputs.size() - 1)
		{
			delete m_inputs.back();
			m_inputs.pop_back();
			while (m_current_input != m_inputs.size() - 1)
			{
				std::swap(m_inputs[m_current_input], m_inputs[m_current_input + 1]);
				m_current_input += 1;
			}
		}
		Input* p = new Input;
		p->cursor = 1;
		p->buffer[0] = '>';
		memset(p->buffer + 1, ' ', ConsoleWidth - 1);
		m_inputs.push_back(p);
		m_current_input = m_inputs.size() - 1;
		return true;
	}
	if (key == GLFW_KEY_UP)
	{
		if (m_current_input > 0) m_current_input -= 1;
		return true;
	}
	if (key == GLFW_KEY_DOWN)
	{
		if (m_current_input + 1 < m_inputs.size()) m_current_input += 1;
		return true;
	}
	if (KeyToChar(key, mods, /*out*/ch))
	{
		if (input.cursor < ConsoleWidth) input.buffer[input.cursor++] = ch;
		return true;
	}
	return false;
}

void Console::Render(Text* text, float time)
{
	if (!m_visible)
		return;

	{
		AutoLock(m_mutex);
		for (int i = m_last_line + 1; i < ConsoleHeight; i++)
		{
			text->PrintBuffer(m_output[i], ConsoleWidth);
		}
		for (int i = 0; i <= m_last_line; i++)
		{
			text->PrintBuffer(m_output[i], ConsoleWidth);
		}
	}

	Input& input = *m_inputs[m_current_input];
	if (input.cursor < ConsoleWidth)
	{
		input.buffer[input.cursor] = (fmod(time, 1.6f) <= 0.8) ? '_' : ' ';
	}
	text->PrintBuffer(input.buffer, ConsoleWidth);
}

json_t* Console::save()
{
	json_t* doc = json_object();
	json_t* output = json_array();
	{
		AutoLock(m_mutex);
		FOR2(i, m_last_line + 1, ConsoleHeight - 1)
		{
			int len = ConsoleWidth;
			while (len > 0 && m_output[i][len-1] == ' ') len -= 1;
			json_array_append(output, json_stringn(m_output[i], len));
		}
		FOR2(i, 0, m_last_line)
		{
			int len = ConsoleWidth;
			while (len > 0 && m_output[i][len-1] == ' ') len -= 1;
			json_array_append(output, json_stringn(m_output[i], len));
		}
	}
	json_object_set(doc, "output", output);
	json_object_set(doc, "last_column", json_integer(m_last_column));
	json_t* commands = json_array();
	FOR(i, m_inputs.size()) json_array_append(commands, json_stringn(m_inputs[i]->buffer, m_inputs[i]->cursor));
	json_object_set(doc, "commands", commands);
	return doc;
}

bool Console::load(json_t* doc)
{
	if (!doc) return true;
	CHECK(json_is_object(doc));

	json_t* output = json_object_get(doc, "output");
	if (output)
	{
		CHECK(json_is_array(output));
		memset(m_output, ' ', ConsoleWidth * ConsoleHeight);
		m_last_line = 0;
		m_last_column = 0;
		FOR(i, json_array_size(output))
		{
			json_t* line = json_array_get(output, i);
			CHECK(json_is_string(line));
			Print("%.*s\n", (int) json_string_length(line), json_string_value(line));
		}
	}

	json_t* last_column = json_object_get(doc, "last_column");
	if (last_column)
	{
		CHECK(json_is_integer(last_column));
		m_last_column = json_integer_value(last_column);
	}

	json_t* commands = json_object_get(doc, "commands");
	if (commands)
	{
		CHECK(json_is_array(commands));
		while (m_inputs.size() > 0)
		{
			delete m_inputs.back();
			m_inputs.pop_back();
		}
		FOR(i, json_array_size(commands))
		{
			json_t* line = json_array_get(commands, i);
			CHECK(json_is_string(line));
			Input* p = new Input;
			p->cursor = std::min<int>(ConsoleWidth, json_string_length(line));
			memset(p->buffer, ' ', ConsoleWidth);
			memcpy(p->buffer, json_string_value(line), p->cursor);
			m_inputs.push_back(p);
		}

		Input* p = new Input;
		p->cursor = 1;
		p->buffer[0] = '>';
		memset(p->buffer + 1, ' ', ConsoleWidth - 1);
		m_inputs.push_back(p);
		m_current_input = m_inputs.size() - 1;
	}
	return true;
}
