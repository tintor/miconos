
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// Config

#define VSYNC 1

// GUI

#ifndef __APPLE_CC__
    #include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>

int width;
int height;

// Render::Matrix

float sqr(float a)
{
	return a * a;
}

void normalize(float* x, float* y, float* z)
{
    float d = sqrtf(sqr(*x) + sqr(*y) + sqr(*z));
    *x /= d;
    *y /= d;
    *z /= d;
}

void matrix_identity(float matrix[16])
{
    matrix[0] = 1;
    matrix[1] = 0;
    matrix[2] = 0;
    matrix[3] = 0;
    matrix[4] = 0;
    matrix[5] = 1;
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = 0;
    matrix[9] = 0;
    matrix[10] = 1;
    matrix[11] = 0;
    matrix[12] = 0;
    matrix[13] = 0;
    matrix[14] = 0;
    matrix[15] = 1;
}

void matrix_translate(float matrix[16], float dx, float dy, float dz)
{
    matrix[0] = 1;
    matrix[1] = 0;
    matrix[2] = 0;
    matrix[3] = 0;
    matrix[4] = 0;
    matrix[5] = 1;
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = 0;
    matrix[9] = 0;
    matrix[10] = 1;
    matrix[11] = 0;
    matrix[12] = dx;
    matrix[13] = dy;
    matrix[14] = dz;
    matrix[15] = 1;
}

void matrix_rotate(float matrix[16], float x, float y, float z, float angle)
{
    normalize(&x, &y, &z);
    float s = sinf(angle);
    float c = cosf(angle);
    float m = 1 - c;
    matrix[0] = m * x * x + c;
    matrix[1] = m * x * y - z * s;
    matrix[2] = m * z * x + y * s;
    matrix[3] = 0;
    matrix[4] = m * x * y + z * s;
    matrix[5] = m * y * y + c;
    matrix[6] = m * y * z - x * s;
    matrix[7] = 0;
    matrix[8] = m * z * x - y * s;
    matrix[9] = m * y * z + x * s;
    matrix[10] = m * z * z + c;
    matrix[11] = 0;
    matrix[12] = 0;
    matrix[13] = 0;
    matrix[14] = 0;
    matrix[15] = 1;
}

void matrix_vector_multiply(float vector[4], float a[16], float b[4])
{
    float result[4];
    for (int i = 0; i < 4; i++) {
        float total = 0;
        for (int j = 0; j < 4; j++) {
            int p = j * 4 + i;
            int q = j;
            total += a[p] * b[q];
        }
        vector[i] = total;
    }
    memcpy(vector, result, 4 * sizeof(float));
}

void matrix_multiply(float matrix[16], float a[16], float b[16])
{
    float result[16];
    for (int c = 0; c < 4; c++)
    {
        for (int r = 0; r < 4; r++)
	{
            float total = 0;
            for (int i = 0; i < 4; i++)
	    {
                total += a[i * 4 + r] * b[c * 4 + i];
            }
            result[c * 4 + r] = total;
        }
    }
    memcpy(matrix, result, 16 * sizeof(float));
}

void matrix_apply(float data[3], float matrix[16], int count)
{
	float vec[4] = {0, 0, 0, 1};
	memcpy(vec, data, sizeof(float) * 3);
        matrix_vector_multiply(vec, matrix, vec);
	memcpy(data, vec, sizeof(float) * 3);
}

void mat_frustum(float* matrix, float left, float right, float bottom, float top, float znear, float zfar)
{
    float temp = 2.0 * znear;
    float temp2 = right - left;
    float temp3 = top - bottom;
    float temp4 = zfar - znear;
    matrix[0] = temp / temp2;
    matrix[1] = 0.0;
    matrix[2] = 0.0;
    matrix[3] = 0.0;
    matrix[4] = 0.0;
    matrix[5] = temp / temp3;
    matrix[6] = 0.0;
    matrix[7] = 0.0;
    matrix[8] = (right + left) / temp2;
    matrix[9] = (top + bottom) / temp3;
    matrix[10] = (-zfar - znear) / temp4;
    matrix[11] = -1.0;
    matrix[12] = 0.0;
    matrix[13] = 0.0;
    matrix[14] = (-temp * zfar) / temp4;
    matrix[15] = 0.0;
}

void matrix_perspective(float matrix[16], float fov, float aspect, float znear, float zfar)
{
    float ymax = znear * tanf(fov * M_PI / 360.0);
    float xmax = ymax * aspect;
    mat_frustum(matrix, -xmax, xmax, -ymax, ymax, znear, zfar);
}

void matrix_ortho(float matrix[16], float left, float right, float bottom, float top, float near, float far)
{
    matrix[0] = 2 / (right - left);
    matrix[1] = 0;
    matrix[2] = 0;
    matrix[3] = 0;
    matrix[4] = 0;
    matrix[5] = 2 / (top - bottom);
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = 0;
    matrix[9] = 0;
    matrix[10] = -2 / (far - near);
    matrix[11] = 0;
    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[14] = -(far + near) / (far - near);
    matrix[15] = 1;
}

void matrix_2d(float matrix[16], int width, int height)
{
    matrix_ortho(matrix, 0, width, 0, height, -1, 1);
}

void matrix_3d(float matrix[16], float x, float y, float z, float aspect, float fov)
{
	float a[16];
	float b[16];
	matrix_translate(a, -x, -y, -z);
	matrix_rotate(b, 1, 0, 0, M_PI/2);
	matrix_multiply(a, b, a);
	matrix_perspective(b, fov, aspect, 0.1, 100.0);
	matrix_multiply(matrix, b, a);
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

void gen_buffers(int components, int faces,
    GLfloat* position_data,  GLfloat* normal_data,  GLfloat* uv_data,
    GLuint*  position_buffer, GLuint* normal_buffer, GLuint* uv_buffer)
{
    if (position_buffer)
    {
        glDeleteBuffers(1, position_buffer);
        *position_buffer = gen_buffer(GL_ARRAY_BUFFER, sizeof(GLfloat) * faces * 6 * components, position_data);
        free(position_data);
    }
    if (normal_buffer)
    {
        glDeleteBuffers(1, normal_buffer);
        *normal_buffer = gen_buffer(GL_ARRAY_BUFFER, sizeof(GLfloat) * faces * 6 * components, normal_data);
        free(normal_data);
    }
    if (uv_buffer)
    {
        glDeleteBuffers(1, uv_buffer);
        *uv_buffer = gen_buffer(GL_ARRAY_BUFFER, sizeof(GLfloat) * faces * 6 * 2, uv_data);
        free(uv_data);
    }
}

void malloc_buffers(int components, int faces, GLfloat** position_data, GLfloat** normal_data, GLfloat** uv_data)
{
    if (position_data)
    {
        *position_data = malloc(sizeof(GLfloat) * faces * 6 * components);
    }
    if (normal_data)
    {
        *normal_data = malloc(sizeof(GLfloat) * faces * 6 * components);
    }
    if (uv_data)
    {
        *uv_data = malloc(sizeof(GLfloat) * faces * 6 * 2);
    }
}

// Render::Texture

#include "lodepng/lodepng.h"

void load_png_texture(const char* file_name)
{
    unsigned char* image;
    unsigned width, height;
    unsigned error = lodepng_decode32_file(&image, &width, &height, file_name);
    if (error)
    {
        fprintf(stderr, "lodepgn_decode32_file error %u: %s\n", error, lodepng_error_text(error));
    	exit(1);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    free(image);
}

// Render::Shader

char* load_text_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    fseek(file, 0, SEEK_END);
    int length = ftell(file);
    rewind(file);
    char* data = malloc(length + 1);
    fread(data, 1, length, file);
    data[length] = 0;
    fclose(file);
    return data;
}

GLuint make_shader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        GLchar* info = calloc(length, sizeof(GLchar));
        glGetShaderInfoLog(shader, length, NULL, info);
        fprintf(stderr, "glCompileShader failed:\n%s\n", info);
        exit(1);
    }
    return shader;
}

GLuint load_shader(GLenum type, const char *path)
{
    char* data = load_text_file(path);
    GLuint result = make_shader(type, data);
    free(data);
    return result;
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
        GLchar* info = calloc(length, sizeof(GLchar));
        glGetProgramInfoLog(program, length, NULL, info);
        fprintf(stderr, "glLinkProgram failed: %s\n", info);
        exit(1);
    }
    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);
    return program;
}

GLuint load_program(const char* path1, const char* path2)
{
    GLuint shader1 = load_shader(GL_VERTEX_SHADER, path1);
    GLuint shader2 = load_shader(GL_FRAGMENT_SHADER, path2);
    GLuint program = make_program(shader1, shader2);
    return program;
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

	glGenTextures(1, &text_texture);
	//glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, text_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	load_png_texture("font.png");
}

void make_character(float* vertex, float* texture, float x, float y, float n, float m, char c)
{
    float* v = vertex;
    float* t = texture;
    float s = 0.0625;
    float a = s;
    float b = s * 2;
    int w = c - 32;
    float du = (w % 16) * a;
    float dv = 1 - (w / 16) * b - b;
    float p = 0;
    *v++ = x - n; *v++ = y - m;
    *v++ = x + n; *v++ = y - m;
    *v++ = x + n; *v++ = y + m;
    *v++ = x - n; *v++ = y - m;
    *v++ = x + n; *v++ = y + m;
    *(v++) = x - n; *(v++) = y + m;
    *(t++) = du + 0; *(t++) = dv + p;
    *(t++) = du + a; *(t++) = dv + p;
    *(t++) = du + a; *(t++) = dv + b - p;
    *(t++) = du + 0; *(t++) = dv + p;
    *(t++) = du + a; *(t++) = dv + b - p;
    *(t++) = du + 0; *(t++) = dv + b - p;
}        

void text_gen_buffers(GLuint* position_buffer, GLuint* uv_buffer, float x, float y, float n, char* text)
{
    int length = strlen(text);
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

void text_print(GLuint position_loc, GLuint uv_loc, float x, float y, float n, char* text)
{
    GLuint position_buffer = 0;
    GLuint uv_buffer = 0;
    text_gen_buffers(&position_buffer, &uv_buffer, x, y, n, text);
    text_draw_buffers(position_buffer, uv_buffer, position_loc, uv_loc, strlen(text));
    glDeleteBuffers(1, &position_buffer);
    glDeleteBuffers(1, &uv_buffer);
}

// Model

typedef struct _Vector3f
{
	float x, y, z;
} Vector3f;

static Vector3f player = { 0, 0, 2 };

double last_time;

Vector3f vector(float x, float y, float z)
{
	Vector3f v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

void model_init()
{
	last_time = glfwGetTime();
}

void model_frame(GLFWwindow* window)
{
	double time = glfwGetTime();
	double dt = (time - last_time) < 0.5 ? (time - last_time) : 0.5;
	last_time = time;

 	if (glfwGetKey(window, 'Q'))
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	Vector3f dir = vector(0, 0, 0);
 	if (glfwGetKey(window, GLFW_KEY_LEFT))
	{
		dir.x -= 1;
	}
 	if (glfwGetKey(window, GLFW_KEY_RIGHT))
	{
		dir.x += 1;
	}
 	if (glfwGetKey(window, GLFW_KEY_UP))
	{
		dir.y += 1;
	}
 	if (glfwGetKey(window, GLFW_KEY_DOWN))
	{
		dir.y -= 1;
	}

	float speed = 2;
	player.x += dir.x * speed * dt;
	player.y += dir.y * speed * dt;
	player.z += dir.z * speed * dt;
}

// Render

void render_init()
{
	glEnable(GL_CULL_FACE);
	glClearColor(0, 0, 0, 1.0);
}

void render_world()
{
	float matrix[16];
        matrix_3d(matrix, player.x, player.y, player.z, width / (float)height, 65.0);
	
	glEnable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Simple grid
	glLoadMatrixf(matrix);
	glLineWidth(1);
	glBegin(GL_LINES);
	int ix = player.x;
	int iy = player.y;
	for (int i = -20; i <= 20; i++)
	{
		if (ix+i < 0) glColor3f(1,0,0); else if (ix+i > 0) glColor3f(0,0,1); else glColor3f(1,1,1);
		glVertex3f(ix + i, iy - 20, 0);
		glVertex3f(ix + i, iy + 20, 0);
	}
	for (int i = -20; i <= 20; i++)
	{
		if (iy+i < 0) glColor3f(1,0,0); else if (iy+i > 0) glColor3f(0,0,1); else glColor3f(1,1,1);
		glVertex3f(ix - 20, iy + i, 0);
		glVertex3f(ix + 20, iy + i, 0);
	}
	glEnd();
	
	glDisable(GL_DEPTH_TEST);
}

void render_gui()
{
	float matrix[16];
        matrix_2d(matrix, width, height);
	
	// Text test
	glUseProgram(text_program);
	glUniformMatrix4fv(text_matrix_loc, 1, GL_FALSE, matrix);
        glUniform1i(text_sampler_loc, 0/*text_texture*/);
        char text_buffer[1024];
        float ts = height / 80;
        float tx = ts / 2;
        float ty = height - ts;
        snprintf(text_buffer, sizeof(text_buffer), "%.1f %.1f %.1f", player.x, player.y, player.z);
        text_print(text_position_loc, text_uv_loc, tx, ty, ts, text_buffer);
	glUseProgram(0);
}

void render_frame()
{
	glViewport(0, 0, width, height);
	render_world();
	render_gui();
}

int main(int argc, char** argv)
{
	if (!glfwInit())
	{
		return 0;
	}
	GLFWwindow* window = glfwCreateWindow(1024, 768, "Arena", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		return 0;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(VSYNC);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	model_init();
	text_init();
	render_init();
	
	while (!glfwWindowShouldClose(window))
	{
		glfwGetFramebufferSize(window, &width, &height);

		model_frame(window);
		render_frame();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwTerminate();
	return 0;
}
