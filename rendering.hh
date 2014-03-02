#pragma once

#include <string>

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
GLuint load_program(const char* name);
void load_png_texture(std::string filename);

class Text
{
public:
	Text();
	void Reset(int width, int height, glm::mat4& matrix);
	void Print(const char* text, int length);
	void Printf(const char* format, ...);
	void PrintAt(float x, float y, float n, const char* text, int length);

private:
	float m_tx;
	float m_ty;
	float m_ts;

	GLuint m_positionBuffer;
	GLuint m_uvBuffer;
};

// TODO make it work for vertical monitor setup
const int ConsoleWidth = 130;
const int ConsoleHeight = 38;

class Console
{
public:
	Console();
	virtual void Execute(const char* command) { PrintLine("[" + std::string(command) + "]"); }
	void PrintLine(const std::string& line);
	static bool KeyToChar(int key, int mods, char& ch);
	bool OnKey(int key, int mods);
	void Render(Text* text, float time);

	bool IsVisible() { return m_visible; }
	void Show() { m_visible = true; }
	void Hide() { m_visible = false; }

private:
	bool m_visible = false;

	char m_output[ConsoleHeight][ConsoleWidth];
	int m_last_line = 0;

	char m_input[ConsoleWidth];
	int m_cursor_pos = 0;
};
