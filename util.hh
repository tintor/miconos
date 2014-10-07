#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>

typedef uint32_t uint;
#define FOR(I, N) for(int (I)=0; (I)<(N); (I)++)
#define FOR2(I, A, B) for(auto (I)=A; (I)<=(B); (I)++)

#define GLM_SWIZZLE
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/noise.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/random.hpp"
#include "glm/gtx/fast_square_root.hpp"
#include "glm/gtx/intersect.hpp"
#include "glm/gtx/closest_point.hpp"
#include "glm/gtx/norm.hpp"
#include "glm/gtx/extented_min_max.hpp"

// ==========

template<typename Type, size_t Capacity>
class ivector
{
public:
	ivector(): m_size(0) { }
	Type& operator[](size_t index) { assert(index < m_size); return m_array[index]; }
	size_t size() const { return m_size; }
	void push_back(Type elem) { assert(m_size < Capacity); m_array[m_size++] = elem; }
	Type pop_back() { assert(m_size > 0); return m_array[--m_size]; }
	Type* begin() { return m_array.begin(); }
	Type* end() { return m_array.begin() + m_size; }
	void clear() { m_size = 0; }
private:
	std::array<Type, Capacity> m_array;
	size_t m_size;
};

// ==========

template<int N>
struct BitCube
{
	void clear() { memset(&m_words[0], 0, Z * sizeof(Word)); }
	void operator=(const BitCube<N>& q) { memcpy(&m_words[0], q.m_words[0], Z * sizeof(Word)); }
	void set(glm::ivec3 a) { int i = index(a); m_words[i / W] |= mask(i); }
	void clear(glm::ivec3 a) { int i = index(a); m_words[i / W] &= ~mask(i); }
	bool operator[](glm::ivec3 a) { int i = index(a); return (m_words[i / W] & mask(i)) != 0; }	

	bool xset(glm::ivec3 a)
	{
		int i = index(a);
		Word w = m_words[i / W] | mask(i);
		if (m_words[i / W] == w) return false;
		m_words[i / W] = w;
		return true; 
	}
private:
	static uint mask(int index) { return 1u << (index % W); }
	int index(glm::ivec3 a) { return (a.x*N + a.y)*N + a.z; }
private:
	typedef uint64_t Word;
	static const int W = sizeof(Word) * 8;
	static const int Z = (N * N * N + W - 1) / W;
	std::array<Word, Z> m_words;
};

// ==========

struct Plucker
{
	glm::vec3 d, m;

	Plucker() {}
	static Plucker points(glm::vec3 a, glm::vec3 b) { return Plucker(b - a, glm::cross(a, b)); }
	static Plucker orig_dir(glm::vec3 orig, glm::vec3 dir) { return Plucker(dir, glm::cross(orig, dir)); }
private:
	Plucker(glm::vec3 D, glm::vec3 M) : d(D), m(M) { }
};

inline float line_crossing(Plucker a, Plucker b) { return glm::dot(a.d, b.m) + glm::dot(b.d, a.m); }
inline bool opposite_sign_strict(double a, double b) { return a * b < 0; }
inline bool is_unit_length(glm::vec3 a) { return std::abs(glm::length2(a) - 1) <= 5e-7; }
inline glm::vec3 random_vec3() { return glm::vec3(glm::linearRand(0.0f, 1.0f), glm::linearRand(0.0f, 1.0f), glm::linearRand(0.0f, 1.0f)); }
template<typename T> T sqr(T a) { return a * a; }
float sqr(glm::vec3 a) { return glm::dot(a, a); }
int sqr(glm::ivec3 a) { return glm::dot(a, a); }

inline float distance2_point_and_line(glm::vec3 point, glm::vec3 orig, glm::vec3 dir)
{
	assert(is_unit_length(dir));
	return glm::distance2(point, orig + dir * glm::dot(point - orig, dir));
}

template<int size>
bool intersects_line_polygon(Plucker line, const Plucker edges[size])
{
	FOR(i, size) if (line_crossing(line, edges[i]) > 0) return false;
	return true;
}

static const glm::ivec3 ix(1, 0, 0), iy(0, 1, 0), iz(0, 0, 1);

template<typename T>
void release(std::vector<T>& a) { std::vector<T> v; std::swap(a, v); }
template<typename T>
bool contains(const std::vector<T>& p, T a) { return std::find(p.begin(), p.end(), a) != p.end(); }
template<typename T>
void compress(std::vector<T>& a) { if (a.capacity() > a.size()) { std::vector<T> b(a); std::swap(a, b); } }

// ==============

inline const char* str(glm::ivec3 a)
{
	char buffer[100];
	snprintf(buffer, 100, "[%d %d %d]", a.x, a.y, a.z);
	int size = strlen(buffer);
	char* p = new char[size + 1];
	memcpy(p, buffer, size + 1);
	return p;
}

inline const char* str(glm::dvec3 a)
{
	char buffer[100];
	snprintf(buffer, sizeof(buffer), "[%lf %lf %lf]", a.x, a.y, a.z);
	int size = strlen(buffer);
	char* p = new char[size + 1];
	memcpy(buffer, p, size + 1);
	return p;
}

inline const char* str(glm::vec3 a)
{
	char buffer[100];
	snprintf(buffer, sizeof(buffer), "[%f %f %f]", a.x, a.y, a.z);
	int size = strlen(buffer);
	char* p = new char[size + 1];
	memcpy(buffer, p, size + 1);
	return p;
}

// ===============

float SimplexNoise(glm::vec2 p, int octaves, float freqf, float ampf, bool turbulent)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = turbulent ? fabs(glm::simplex(p)) : glm::simplex(p);
	FOR(i, octaves - 1)
	{
		freq *= freqf;
		amp *= ampf;
		max += amp;
		total += (turbulent ? fabs(glm::simplex(p * freq)) : glm::simplex(p * freq)) * amp;
	}
	return total / max;
}

float SimplexNoise(glm::vec3 p, int octaves, float freqf, float ampf, bool turbulent)
{
	float freq = 1.0f, amp = 1.0f, max = amp;
	float total = turbulent ? fabs(glm::simplex(p)) : glm::simplex(p);
	FOR(i, octaves - 1)
	{
		freq *= freqf;
		amp *= ampf;
		max += amp;
		total += (turbulent ? fabs(glm::simplex(p * freq)) : glm::simplex(p * freq)) * amp;
	}
	return total / max;
}

// ===============

struct Sphere : public std::vector<glm::ivec3>
{
	Sphere(int size)
	{
		FOR2(x, -size, size) FOR2(y, -size, size) FOR2(z, -size, size)
		{
			glm::ivec3 d(x, y, z);
			if (glm::dot(d, d) > 0 && glm::dot(d, d) <= size * size) push_back(d);
		}
		std::sort(begin(), end(), [](glm::ivec3 a, glm::ivec3 b) { return glm::length2(a) < glm::length2(b); });
	}
};

// ================

struct Frustum
{
	Frustum(const glm::mat4& matrix)
	{
		const float* m = glm::value_ptr(matrix);
		glm::vec4 a(m[0], m[4], m[8], m[12]);
		glm::vec4 b(m[1], m[5], m[9], m[13]);
		glm::vec4 d(m[3], m[7], m[11], m[15]);

		m_plane[0] = d - a; // left
		m_plane[1] = d + a; // right
		m_plane[2] = d + b; // bottom
		m_plane[3] = d - b; // top

		FOR(i, 4) m_plane[i] *= glm::fastInverseSqrt(sqr(m_plane[i].xyz()));
	}

	bool contains_point(glm::vec3 p) const
	{
		FOR(i, 4) if (glm::dot(p, m_plane[i].xyz()) + m_plane[i].w < 0) return false;
		return true;
	}

	bool is_sphere_outside(glm::vec3 p, float radius) const
	{
		FOR(i, 4) if (glm::dot(p, m_plane[i].xyz()) + m_plane[i].w < -radius) return true;
		return false;
	}

private:
	std::array<glm::vec4, 4> m_plane;
};

// ===============

inline int64_t rdtsc()
{
	uint lo, hi;
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	return (static_cast<uint64_t>(hi) << 32) | lo;
}

struct Timestamp
{
	Timestamp() : m_ticks(rdtsc()) { }

	int64_t elapsed(Timestamp a = Timestamp()) { return a.m_ticks - m_ticks; }
	double elapsed_ms(Timestamp a = Timestamp()) { return elapsed(a) * milisec_per_tick; }

	static void init(glm::dvec3 a, glm::i64vec3 b, glm::dvec3 c, glm::i64vec3 d)
	{
		glm::dvec3 q = (c - a) / glm::dvec3(d - b);
		if (q.x > q.y) std::swap(q.x, q.y);
		if (q.y > q.z) std::swap(q.y, q.z);
		if (q.x > q.y) std::swap(q.x, q.y);
		milisec_per_tick = q.y * 1000;
	}

	static double milisec_per_tick;
private:
	int64_t m_ticks;
};
