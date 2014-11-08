#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "socket.hh"

#define CHECK(A) do { if (!(A)) { fprintf(stderr, "%s failed at %s line %d. Errno %d (%s)\n", #A, __FILE__, __LINE__, errno, strerror(errno)); return false; } } while(0);

ssize_t Socket::recv(void* buffer, size_t size) const
{
	assert(m_sock != -1);
	return ::recv(m_sock, buffer, size, MSG_DONTWAIT);
}

ssize_t Socket::send(const void* buffer, size_t length) const
{
	assert(m_sock != -1);
	return ::send(m_sock, buffer, length, MSG_DONTWAIT | SO_NOSIGPIPE /*MSG_NOSIGNAL*/);
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
		fprintf(stderr, "%d\n", res->ai_family);
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

void RingBuffer::pop_front(int len)
{
	assert(len <= size);
	begin += len;
	if (begin >= sizeof(buffer)) begin -= sizeof(buffer);
	size -= len;
}

void RingBuffer::write_back(const uint8_t* str, int len)
{
	assert(len > 0);
	assert(space() >= len);
	if (end + len <= sizeof(buffer))
	{
		memcpy(buffer + end, str, len);
		end += len;
		if (end == sizeof(buffer)) end = 0;
	}
	else
	{
		int e = sizeof(buffer) - end;
		memcpy(buffer + end, str, e);
		memcpy(buffer, str + e, len - e);
		end = len - e;
	}
	size += len;
}

int min(int a, int b) { return (a < b) ? a : b; }

bool RingBuffer::recv_any(const Socket& sock)
{
	while (size < sizeof(buffer))
	{
		int length = min(sizeof(buffer) - end, sizeof(buffer) - size);
		int ret = sock.recv(buffer + end, length);
		if (ret > 0)
		{
			end += ret;
			size += ret;
			if (end == sizeof(buffer)) end = 0;
			continue;
		}
		if (ret < 0 && errno == EAGAIN) break;
		if (ret == 0) fprintf(stderr, "recv() failed: connection closed\n");
		if (ret < 0) fprintf(stderr, "recv() failed: %s (%d)\n", strerror(errno), errno);
		return false;
	}
	return true;
}

bool RingBuffer::send_any(const Socket& sock)
{
	while (size > 0)
	{
		int length = min(size, sizeof(buffer) - begin);
		int ret = sock.send(buffer + begin, length);
		if (ret > 0)
		{
			begin += ret;
			size -= ret;
			if (begin == sizeof(buffer)) begin = 0;
			continue;
		}
		if (ret < 0 && errno == EAGAIN) break;
		if (ret == 0) fprintf(stderr, "send() failed: connection closed\n");
		if (ret < 0) fprintf(stderr, "send() failed: %s (%d)\n", strerror(errno), errno);
		return false;
	}
	return true;
}
