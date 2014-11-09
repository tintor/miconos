#pragma once
#include <stdio.h>
#include <stdlib.h>

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

struct RingBuffer
{
	int begin;
	int end;
	int size;
	uint8_t buffer[1 << 20];

	RingBuffer() : begin(0), end(0), size(0) { }
	int space() { return sizeof(buffer) - size; }
	void check();

	void read_ignore(int len);
	void read(void* str, int len);
	void write(const void* str, int len);

	bool recv_any(const Socket& sock);
	bool send_any(const Socket& sock);
};

bool has_text_message(RingBuffer& recv, int& size);
bool write_text_message(RingBuffer& send, const char* fmt, ...);
