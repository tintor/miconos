#include "util.hh"
#include "socket.hh"

// TODO: separate chunk files on client and server

struct Connection
{
	Socket sock;
	char host[16];
	RingBuffer recv_buffer;
	RingBuffer send_buffer;

	bool exchange()
	{
		return recv_buffer.recv_any(sock) && send_buffer.send_any(sock);
	}

	void handle()
	{
		if (recv_buffer.size == 0) return;
		int size = recv_buffer.buffer[recv_buffer.begin];
		if (recv_buffer.size < 1 + size) return;
		if (recv_buffer.begin + 1 + size <= sizeof(RingBuffer::buffer))
		{
			char* msg = (char*)recv_buffer.buffer + recv_buffer.begin + 1;
			fprintf(stderr, "Got message from socket: [%.*s]\n", size, msg);
			if (send_buffer.space() >= 1 + size)
			{
				uint8_t s = size;
				send_buffer.write_back(&s, 1);
				send_buffer.write_back((uint8_t*)msg, size);
			}
			recv_buffer.pop_front(1 + size);
		}
		else
		{
			char* msg1 = (char*)recv_buffer.buffer + recv_buffer.begin + 1;
			int size1 = sizeof(recv_buffer.buffer) - recv_buffer.begin - 1;
			char* msg2 = (char*)recv_buffer.buffer;
			int size2 = size - size1;
			fprintf(stderr, "Got message from socket: [%.*s%.*s]\n", size1, msg1, size2, msg2);
			if (send_buffer.space() >= 1 + size)
			{
				uint8_t s = size;
				send_buffer.write_back(&s, 1);
				send_buffer.write_back((uint8_t*)msg1, size1);
				send_buffer.write_back((uint8_t*)msg2, size2);
			}
			recv_buffer.pop_front(1 + size);
		}
	}
};

std::vector<Connection*> g_connections;

void exchange_connections()
{
	for (int i = 0; i < g_connections.size(); i++)
	{
		Connection* conn = g_connections[i];
		if (!conn->exchange())
		{
			fprintf(stderr, "Connection with %s closed\n", conn->host);
			delete conn;
			g_connections[i] = g_connections.back();
			g_connections.pop_back();
			i -= 1;
		}
	}
}

void server_main()
{
	Socket server_sock;
	CHECK2(server_sock.bind(7000), exit(1));
	fprintf(stderr, "Server running on port 7000\n");
	std::atomic<Connection*> new_connection(nullptr);
	std::thread([&]()
	{
		while (true)
		{
			while (new_connection != nullptr) usleep(1000);
			Connection* c = new Connection;
			CHECK2(server_sock.accept(c->sock, c->host), exit(1));
			fprintf(stderr, "Accepted connection from %s\n", c->host);
			new_connection = c;
		}
	}).detach();

	while (true)
	{
		if (new_connection != nullptr)
		{
			g_connections.push_back(new_connection);
			new_connection = nullptr;
		}
		exchange_connections();
		for (Connection* c : g_connections) c->handle();
		usleep(100*1000);
	}
}
