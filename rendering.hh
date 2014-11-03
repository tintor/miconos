#pragma once

#include <string>
#include <vector>
#include <mutex>

#include "jansson/jansson.h"

#define GLFW_INCLUDE_GLCOREARB
#ifndef __APPLE_CC__
	#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

#define GLM_SWIZZLE
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"

void Error(const char* name);

int gen_buffer(GLenum target, GLsizei size, const void* data);
GLuint load_program(const char* name, bool geometry = false);
void load_png_texture(std::string filename);

class Text
{
public:
	Text();
	void Reset(int width, int height, glm::mat4& matrix);
	void Print(const char* fmt, ...) __printflike(2, 3);
	void PrintBuffer(const char* buffer, int length);
	void PrintAt(float x, float y, float n, const char* text, int length);

private:
	float m_tx;
	float m_ty;
	float m_ts;

	GLuint m_positionBuffer;
	GLuint m_uvBuffer;
	std::vector<GLfloat> m_position_data;
	std::vector<GLfloat> m_uv_data;
};

// TODO make it work for vertical monitor setup
const int ConsoleWidth = 131;
const int ConsoleHeight = 40; // TODO: magic numbers?

class Console
{
public:
	Console();
	virtual void Execute(const char* command, int length) { Print("[%.*s]\n", length, command); }
	void Print(const char* fmt, ...) __printflike(2, 3);
	static bool KeyToChar(int key, int mods, char& ch);
	bool OnKey(int key, int mods);
	void Render(Text* text, float time);

	bool IsVisible() { return m_visible; }
	void Show() { m_visible = true; }
	void Hide() { m_visible = false; }

	json_t* save();
	bool load(json_t* doc);

private:
	bool m_visible = false;
	std::mutex m_mutex;

	char m_output[ConsoleHeight][ConsoleWidth];
	int m_last_line = 0;
	int m_last_column = 0;

	struct Input
	{
		char buffer[ConsoleWidth];
		int cursor;
	};

	int m_current_input = 0;
	std::vector<Input*> m_inputs;
};
