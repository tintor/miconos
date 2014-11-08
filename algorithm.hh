#include "util.hh"

template<typename T>
void release(std::vector<T>& a) { std::vector<T> v; std::swap(a, v); }
template<typename T>
bool contains(const std::vector<T>& p, T a) { return std::find(p.begin(), p.end(), a) != p.end(); }
template<typename T>
void compress(std::vector<T>& a) { if (a.capacity() > a.size()) { std::vector<T> b(a); std::swap(a, b); } }

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
	const Type* begin() const { return m_array.begin(); }
	const Type* end() const { return m_array.begin() + m_size; }
	void clear() { m_size = 0; }
private:
	std::array<Type, Capacity> m_array;
	size_t m_size;
};

// ==========

template<int N>
struct BitCube
{
	typedef uint64_t Word;
	static const int W = sizeof(Word) * 8;
	static const int Z = (N * N * N + W - 1) / W;
	static const int Bytes = sizeof(Word) * Z;

	void clear() { FOR(i, Z) m_words[i] = 0; }
	void operator=(const BitCube<N>& q) { FOR(i, Z) m_words[i] = q.m_words[i]; }
	void set(glm::ivec3 a) { int i = index(a); m_words[i / W] |= mask(i); }
	void clear(glm::ivec3 a) { int i = index(a); m_words[i / W] &= ~mask(i); }
	bool operator[](glm::ivec3 a) { int i = index(a); return (m_words[i / W] & mask(i)) != 0; }

	uint count()
	{
		uint c = 0;
		FOR(i, Z)
		{
			Word w = m_words[i];
			static_assert(sizeof(Word) == 8, "");
			c += __builtin_popcount(w);
			c += __builtin_popcount(w >> 32);
		}
		return c;
	}

	bool xset(glm::ivec3 a)
	{
		int i = index(a);
		Word w = m_words[i / W] | mask(i);
		if (m_words[i / W] == w) return false;
		m_words[i / W] = w;
		return true;
	}
private:
	static Word mask(int index) { return Word(1) << (index % W); }
	int index(glm::ivec3 a) { assert(a.x >= 0 && a.x < N && a.y >= 0 && a.y < N && a.z >= 0 && a.z < N); return (a.x*N + a.y)*N + a.z; }
private:
	std::array<Word, Z> m_words;
};

// =================

template<int Bits>
class BitSet
{
public:
	BitSet() : m_garbage(nullptr), m_last(nullptr) { FOR(i, 64) m_map[i] = nullptr; }

	~BitSet()
	{
		FOR(i, 64) free_list(m_map[i]);
		free_list(m_garbage);
	}

	void clear()
	{
		FOR(i, 64) while (m_map[i])
		{
			Node* p = m_map[i];
			m_map[i] = p->next;
			p->next = m_garbage;
			m_garbage = p;
		}
		m_last = nullptr;
	}

	bool xset(glm::ivec3 a)
	{
		const int Size = 1 << Bits;
		const int Mask = Size - 1;

		glm::ivec3 cpos = a >> Bits;
		if (m_last && m_last->cpos == cpos) return m_last->cube->xset(a & Mask);
		glm::ivec3 b = cpos & 3;
		uint index = (b.x * 4 + b.y) * 4 + b.z;
		for (Node* p = m_map[index]; p; p = p->next)
		{
			if (p->cpos == cpos)
			{
				m_last = p;
				return p->cube->xset(a & Mask);
			}
		}

		Node* q = nullptr;
		if (m_garbage)
		{
			q = m_garbage;
			m_garbage = q->next;
		}
		else
		{
			q = new Node;
			q->cube = new BitCube<Size>;
		}
		q->cpos = cpos;
		q->cube->clear();
		q->cube->set(a & Mask);
		m_last = q;
		q->next = m_map[index];
		m_map[index] = q;
		return true;
	}

	uint count()
	{
		uint c = 0;
		FOR(i, 64) for (Node* p = m_map[i]; p; p = p->next) c += p->cube->count();
		return c;
	}

private:
	struct Node
	{
		BitCube<1 << Bits>* cube;
		glm::ivec3 cpos;
		Node* next;
	};

private:
	BitSet(const BitSet& a) { }
	void operator=(const BitSet& a) { }

	static void free_list(Node* a)
	{
		while (a)
		{
			Node* p = a->next;
			a = a->next;
			delete a->cube;
			delete a;
		}
	}

private:
	Node* m_garbage;
	Node* m_last;
	Node* m_map[64];
};

// =============

template<typename T>
struct arraydeque
{
	arraydeque() : m_begin(0), m_end(0), m_size(0), m_capacity(0), m_array(nullptr) { }
	~arraydeque() { free(m_array); }

	void clear()
	{
		m_begin = 0;
		m_end = 0;
		m_size = 0;
	}

	void push_front(T a)
	{
		m_size += 1;
		reserve(m_size);
		m_begin = (m_begin - 1) & (m_capacity - 1);
		m_array[m_begin] = a;
	}

	void push_back(T a)
	{
		m_size += 1;
		reserve(m_size);
		m_array[m_end] = a;
		m_end = (m_end + 1) & (m_capacity - 1);
	}

	T pop_front()
	{
		m_size -= 1;
		Auto(m_begin = (m_begin + 1) & (m_capacity - 1));
		return m_array[m_begin];
	}

	T pop_back()
	{
		m_size -= 1;
		m_end = (m_end - 1) & (m_capacity - 1);
		return m_array[m_end];
	}

	uint size() { return m_size; }

	void reserve(uint size)
	{
		if (size <= m_capacity) return;
		assert(m_begin == m_end);
		uint capacity = m_capacity;
		if (capacity == 0) capacity = 8;
		while (capacity < size) capacity *= 2;
		T* array = (T*)malloc(sizeof(T) * (size_t)capacity);
		memcpy(array, m_array + m_begin, sizeof(T) * (size_t)(m_capacity - m_begin));
		memcpy(array + m_capacity - m_begin, m_array, sizeof(T) * (size_t)m_begin);
		free(m_array);
		m_begin = 0;
		m_end = m_capacity;
		m_capacity = capacity;
		m_array = array;
	}

private:
	uint m_begin;
	uint m_end;
	uint m_size;
	uint m_capacity;
	T* m_array;
};
