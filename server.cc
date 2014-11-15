#include "util.hh"
#include "socket.hh"
#include "message.hh"
#include "parse.hh"

struct ServerAvatar
{
	bool broadcasted;
	uint8_t id;
	glm::vec3 position;
	float yaw, pitch;
};

struct Connection
{
	Socket sock;
	char host[16];
	RingBuffer recv_buffer;
	RingBuffer send_buffer;
	ServerAvatar avatar;

	bool exchange()
	{
		return recv_buffer.recv_any(sock) && send_buffer.send_any(sock);
	}
};

static std::vector<Connection*> g_connections;

static std::vector<uint8_t> g_free_ids;

uint8_t create_id()
{
	uint8_t id = g_free_ids.back();
	g_free_ids.pop_back();
	return id;
}

void destroy_id(uint8_t id)
{
	g_free_ids.push_back(id);
}

void server_receive_text_message(Connection& conn, const char* message, uint length)
{
	fprintf(stderr, "Player #%d [%s]: %.*s\n", conn.avatar.id, conn.host, length, message);

	std::vector<Token> tokens;
	tokenize(message, length, /*out*/tokens);
	if (tokens.size() == 0) return;

	if (tokens[0] == "chat")
	{
		if (tokens.size() < 2) return;
		for (Connection* conn : g_connections)
		{
			write_text_message(conn->send_buffer, "chat %d %.*s", conn->avatar.id, length - (tokens[1].first - message), tokens[1].first);
		}
		return;
	}

	if (tokens[0] == "block")
	{
		// TODO modify chunk and send updates to everybody
		return;
	}
}

bool server_receive_message(Connection& conn)
{
	RingBuffer& recv = conn.recv_buffer;
	switch ((MessageType)recv.buffer[recv.begin])
	{
	case MessageType::Text:
	{
		int size;
		if (!has_text_message(recv, size)) return false;
		if (recv.begin + size <= sizeof(RingBuffer::buffer))
		{
			server_receive_text_message(conn, (char*)recv.buffer + recv.begin, size);
			recv.read_ignore(size);
		}
		else if (size <= 1024)
		{
			char message[1024];
			recv.read(message, size);
			server_receive_text_message(conn, message, size);
		}
		else
		{
			char* message = (char*)malloc(size);
			recv.read(message, size);
			server_receive_text_message(conn, message, size);
			free(message);
		}
		return true;
	}
	case MessageType::AvatarState:
	{
		MessageAvatarState msg;
		if (recv.size < 1 + sizeof(msg)) return false;
		recv.read_ignore(1);
		recv.read(&msg, sizeof(msg));
		conn.avatar.pitch = msg.pitch;
		conn.avatar.yaw = msg.yaw;
		conn.avatar.position = msg.position;
		conn.avatar.broadcasted = false;
		return true;
	}
	case MessageType::ChunkState: FAIL;
	case MessageType::EditBlock:
	{
		MessageEditBlock msg;
		if (recv.size < 1 + sizeof(msg)) return false;
		recv.read_ignore(1);
		recv.read(&msg, sizeof(msg));
		FAIL; // UNDONE
	}
	}
	return false;
}

void server_main()
{
	FOR(i, 255) g_free_ids.push_back(254 - i);

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
			new_connection = c;
		}
	}).detach();

	Timestamp ta;
	while (true)
	{
		if (new_connection != nullptr)
		{
			Connection* conn = new_connection;
			conn->avatar.id = create_id();
			conn->avatar.broadcasted = true;
			fprintf(stderr, "Player #%d connected from %s\n", (int)conn->avatar.id, conn->host);
			// TODO: send new player positions of all other avatars (as they may be standing still)
			for (Connection* conn2 : g_connections)
			{
				write_text_message(conn2->send_buffer, "joined %d", (int)conn->avatar.id);
			}
			g_connections.push_back(conn);
			new_connection = nullptr;
		}

		for (int i = 0; i < g_connections.size(); i++)
		{
			Connection* conn = g_connections[i];
			if (!conn->exchange())
			{
				fprintf(stderr, "Player #%d disconnected from %s\n", (int)conn->avatar.id, conn->host);
				for (Connection* conn2 : g_connections)
				{
					if (conn != conn2) write_text_message(conn->send_buffer, "left #%d", (int)conn->avatar.id);
				}
				destroy_id(conn->avatar.id);
				delete conn;
				g_connections[i] = g_connections.back();
				g_connections.pop_back();
				i -= 1;
			}
		}

		// read all messages
		for (Connection* conn : g_connections)
		{
			while (conn->recv_buffer.size > 0 && server_receive_message(*conn)) { }
		}

		// broadcast avatar states
		for (Connection* conn : g_connections)
		{
			if (conn->avatar.broadcasted) continue;
			MessageAvatarState as;
			MessageType type = MessageType::AvatarState;
			as.id = conn->avatar.id;
			as.position = conn->avatar.position;
			as.pitch = conn->avatar.pitch;
			as.yaw = conn->avatar.yaw;
			for (Connection* conn2 : g_connections)
			{
				if (conn2 != conn && conn2->send_buffer.space() >= 1 + sizeof(as))
				{
					conn2->send_buffer.write(&type, 1);
					conn2->send_buffer.write(&as, sizeof(as));
				}
			}
			conn->avatar.broadcasted = true;
		}

		Timestamp tb;
		double ft = ta.elapsed_ms(tb);
		if (ft < 10) usleep(int((10 - ft) * 1000));
	}
}
