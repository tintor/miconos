#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <array>

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

template<typename T>
bool between(T a, T b, T c)
{
	return (a <= b && b <= c) || (a >= b && b >= c);
}

template<typename T, glm::precision P>
bool between(glm::detail::tvec3<T, P> a, glm::detail::tvec3<T, P> b, glm::detail::tvec3<T, P> c)
{
	return between(a.x, b.x, c.x) && between(a.y, b.y, c.y) && between(a.z, b.z, c.z);
}

// ==========

template<> struct std::hash<glm::ivec3>
{
	std::size_t operator()(glm::ivec3 a) const { return a.x * 7 + a.y * 3341 + a.z * 543523; }
};

// ==========

namespace std
{
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
}

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

#define FOR(I, N) for(int (I)=0; (I)<(N); (I)++)
#define FOR2(I, A, B) for(auto (I)=A; (I)<=(B); (I)++)
#define unless(A) if(!(A))

template<typename T>
void release(std::vector<T>& a) { std::vector<T> v; std::swap(a, v); }
template<typename T>
bool contains(const std::vector<T>& p, T a) { return std::find(p.begin(), p.end(), a) != p.end(); }

template<typename T>
void compress(std::vector<T>& a) { if (a.capacity() > a.size()) { std::vector<T> b(a); std::swap(a, b); assert(a.size() == a.capacity()); } }

// =============

struct IVec2Hash
{
	size_t operator()(glm::ivec2 a) const { return a.x * 7919 + a.y * 7537; }
};

struct IVec3Hash
{
	size_t operator()(glm::ivec3 a) const { return a.x * 7919 + a.y * 7537 + a.z * 7687; }
};
