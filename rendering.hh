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
	void Reset(int height, glm::mat4& matrix);
	void Printf(const char* format, ...);
	void PrintAt(float x, float y, float n, const char* text);
private:
	float m_tx;
	float m_ty;
	float m_ts;

	GLuint m_positionBuffer;
	GLuint m_uvBuffer;
};
