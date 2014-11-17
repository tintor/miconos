#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "socket.hh"

#define CHECK2(A, B) do { if (!(A)) { fprintf(stderr, "%s failed at %s line %d. Errno %d (%s)\n", #A, __FILE__, __LINE__, errno, strerror(errno)); B; } } while(0);
#define CHECK(A) CHECK2(A, return false)

ssize_t Socket::recv(void* buffer, size_t size) const
{
	assert(m_sock != -1);
	return ::recv(m_sock, buffer, size, MSG_DONTWAIT);
}

ssize_t Socket::send(const void* buffer, size_t length) const
{
	assert(m_sock != -1);
#ifdef __APPLE__
	return ::send(m_sock, buffer, length, MSG_DONTWAIT);
#else
	return ::send(m_sock, buffer, length, MSG_DONTWAIT || MSG_NOPIPE);
#endif
}

bool Socket::accept(Socket& socket, char host[16]) const
{
	assert(m_sock != -1);
	assert(socket.m_sock == -1);
	sockaddr_in addr;
	socklen_t len = sizeof(addr);
	socket.m_sock = ::accept(m_sock, (sockaddr*) &addr, &len);
	CHECK(socket.m_sock >= 0);
	assert(16 == INET_ADDRSTRLEN);
	inet_ntop(AF_INET, &addr, host, INET_ADDRSTRLEN);
	return true;
}

bool Socket::bind(uint16_t port)
{
	assert(m_sock == -1);
	m_sock = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(m_sock >= 0);
	sockaddr_in addr;
	memset((char*) &addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	int value = 1;
	CHECK(setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == 0);
#ifdef __APPLE__
	CHECK(setsockopt(m_sock, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value)) == 0);
#endif

	CHECK(::bind(m_sock, (sockaddr*) &addr, sizeof(addr)) >= 0);
	CHECK(listen(m_sock, 5) == 0);
	return true;
}

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "auto.hh"

bool Socket::connect(const char* hostname, const char* servname)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo* res0 = nullptr;
	int err = getaddrinfo(hostname, servname, &hints, &res0);
	if (err != 0)
	{
		fprintf(stderr, "getaddrinfo(%s, %s) failed: %s\n", hostname, servname, gai_strerror(err));
		return false;
	}
	Auto(freeaddrinfo(res0));

	for (addrinfo* res = res0; res; res = res->ai_next)
	{
		assert(m_sock == -1);
		m_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		CHECK(m_sock >= 0);
#ifdef __APPLE__
		int value = 1;
		CHECK(setsockopt(m_sock, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value)) == 0);
#endif
		if (::connect(m_sock, res->ai_addr, res->ai_addrlen) == 0) return true;
		int e = errno;
		fprintf(stderr, "connect(%s, %s) failed: %s (%d)\n", hostname, servname, strerror(errno), errno);
		::close(m_sock);
		m_sock = -1;
		errno = e;
	}
	return false;
}

void Socket::close()
{
	if (m_sock == -1) return;
	::close(m_sock);
	m_sock = -1;
}

void SocketBuffer::check()
{
	assert(0 <= m_begin && m_begin <= m_end && m_end <= m_capacity);
}

void SocketBuffer::reserve(uint capacity)
{
	if (capacity <= m_capacity) return;

	CHECK2(capacity <= 0x80000000, exit(1));
	uint a = (m_capacity == 0) ? 1024 : m_capacity;
	while (a < capacity) a += a;
	capacity = a;

	uint8_t* buffer = (uint8_t*)malloc(capacity);
	if (m_buffer)
	{
		memcpy(buffer, m_buffer + m_begin, m_end - m_begin);
		free(m_buffer);
	}
	m_end -= m_begin;
	m_begin = 0;
	m_capacity = capacity;
	m_buffer = buffer;
}

void SocketBuffer::ensure_space(uint _space)
{
	if (space() >= _space) return;
	if (space() + m_begin >= _space)
	{
		memmove(m_buffer, m_buffer + m_begin, m_end - m_begin);
		m_end -= m_begin;
		m_begin = 0;
		return;
	}
	reserve(size() + _space);
}

uint8_t* SocketBuffer::read_message(uint len)
{
	assert(m_begin + len <= m_end);
	uint8_t* p = m_buffer + m_begin;
	m_begin += len;
	return p;
}

uint8_t* SocketBuffer::write_message(uint len)
{
	assert(m_end + len <= m_capacity);
	uint8_t* p = m_buffer + m_end;
	m_end += len;
	return p;
}

void SocketBuffer::write_byte(uint8_t byte)
{
	assert(m_end < m_capacity);
	m_buffer[m_end++] = byte;
}

void SocketBuffer::write(const void* str, uint len)
{
	assert(m_end + len <= m_capacity);
	memcpy(m_buffer + m_end, str, len);
	m_end += len;
}

int min(int a, int b) { return (a < b) ? a : b; }

bool SocketBuffer::recv_any(const Socket& sock)
{
	assert(m_capacity >= 1024);
	if (m_begin > 0)
	{
		memmove(m_buffer, m_buffer + m_begin, m_end - m_begin);
		m_end -= m_begin;
		m_begin = 0;
	}
	while (m_end < m_capacity)
	{
		int ret = sock.recv(m_buffer + m_end, m_capacity - m_end);
		if (ret > 0)
		{
			m_end += ret;
			continue;
		}
		if (ret < 0 && errno == EAGAIN) break;
		if (ret == 0) fprintf(stderr, "recv() failed: connection closed\n");
		if (ret < 0) fprintf(stderr, "recv() failed: %s (%d)\n", strerror(errno), errno);
		return false;
	}
	check();
	return true;
}

bool SocketBuffer::send_any(const Socket& sock)
{
	while (m_begin < m_end)
	{
		int ret = sock.send(m_buffer + m_begin, m_end - m_begin);
		if (ret > 0)
		{
			m_begin += ret;
			continue;
		}
		if (ret < 0 && errno == EAGAIN) break;
		if (ret == 0) fprintf(stderr, "send() failed: connection closed\n");
		if (ret < 0) fprintf(stderr, "send() failed: %s (%d)\n", strerror(errno), errno);
		return false;
	}
	if (m_begin > 0)
	{
		memmove(m_buffer, m_buffer + m_begin, m_end - m_begin);
		m_end -= m_begin;
		m_begin = 0;
	}
	check();
	return true;
}
