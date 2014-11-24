#pragma once
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t uint;

class Socket
{
public:
	Socket() : m_sock(-1) { }
	~Socket() { close(); }

	// recv() and send() are non-blocking
	ssize_t recv(void* buffer, size_t length) const;
	ssize_t send(const void* buffer, size_t length) const;
	void close();

	bool bind(uint16_t port);
	bool accept(Socket& socket, char host[16]) const;
	bool connect(const char* hostname, const char* servname);

private:
	int m_sock;
};

struct SocketBuffer
{
	SocketBuffer() : m_begin(0), m_end(0), m_capacity(0), m_buffer(0) { }
	~SocketBuffer() { free(m_buffer); }

	uint space() { return m_capacity - m_end; }
	uint size() { return m_end - m_begin; }
	uint capacity() { return m_capacity; }
	uint8_t* data() { return m_buffer + m_begin; }
	void check();

	void reserve(uint capacity);
	void ensure_space(uint space);

	void write_byte(uint8_t byte);
	void write(const void* str, uint len);

	template<typename T> T* read() { return (size() < sizeof(T)) ? nullptr : (T*)read_message(sizeof(T)); }
	template<typename T> T* write() { ensure_space(sizeof(T)); return (T*)write_message(sizeof(T)); }
	template<typename T> void write(const T& msg) { ensure_space(sizeof(T)); *(T*)write_message(sizeof(T)) = msg; }

	uint8_t* read_message(uint len);
	uint8_t* write_message(uint len);

	bool recv_any(const Socket& sock);
	bool send_any(const Socket& sock);

private:
	SocketBuffer(const SocketBuffer&) { }
	void operator=(const SocketBuffer&) { }

private:
	uint m_begin;
	uint m_end;
	uint m_capacity;
	uint8_t* m_buffer;
};
