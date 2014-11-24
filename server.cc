#include "util.hh"
#include "socket.hh"
#include "message.hh"
#include "parse.hh"
#include "algorithm.hh"
#include "maplock.hh"
#include "auto.hh"
#include "lz4.h"

#include <unordered_map>

void generate_chunk(Blocks& chunk, glm::ivec3 cpos);

// =============

Sphere g_server_render_sphere(40/*RenderDistance*/);

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
	SocketBuffer recv_buffer;
	SocketBuffer send_buffer;
	ServerAvatar avatar;

	glm::ivec3 m_cpos;
	XCube<MapSize, glm::ivec3> m_chunks;
	int m_scaned_chunks;

	Connection()
	{
		m_cpos = x_bad_ivec3;
		m_scaned_chunks = g_server_render_sphere.size();
		m_chunks.clear(x_bad_ivec3);
	}

	void update_cpos()
	{
		glm::ivec3 cpos = glm::ivec3(glm::floor(avatar.position)) >> ChunkSizeBits;
		if (m_cpos != cpos)
		{
			m_cpos = cpos;
			m_scaned_chunks = 0;
		}
	}

	bool exchange()
	{
		return recv_buffer.recv_any(sock) && send_buffer.send_any(sock);
	}

	void send_chunk(glm::ivec3 cpos, const Blocks& chunk)
	{
		assert(glm::distance2(m_cpos, cpos) <= sqr(40/*RenderDistance*/));
		auto message = send_buffer.write<MessageChunkState>();
		message->type = MessageType::ChunkState;
		message->cpos = cpos;
		assert(sizeof(chunk) == sizeof(MessageChunkState::blocks));
		memcpy(message->blocks, &chunk, sizeof(chunk));
		m_chunks[cpos & MapSizeBits] = cpos;
	}
};

static std::vector<Connection*> g_connections;

// =============

struct SuperChunk
{
	static const uint BlockCubeFileSize = (1 << (3 * (ChunkSizeBits + SuperChunkSizeBits))) * sizeof(Block);
	typedef BitCube<(1 << SuperChunkSizeBits)> BitCubeExplored;
	static const uint DataSize = BlockCubeFileSize + sizeof(BitCubeExplored);

	const glm::ivec3 scpos;
	int refs;
	bool modified;
	uint8_t* data;

	BitCube<SuperChunkSize> active;

	bool load();
	bool save();

	SuperChunk(glm::ivec3 _scpos) : scpos(_scpos), refs(0), data(nullptr) { }
	BitCubeExplored& explored() { return *reinterpret_cast<BitCubeExplored*>(data + BlockCubeFileSize); }
	Blocks& chunk(glm::ivec3 cpos);
};

Blocks& SuperChunk::chunk(glm::ivec3 cpos)
{
	int i = (((cpos.x << ChunkSizeBits) | cpos.y) << ChunkSizeBits) | cpos.z;
	Block* blocks = reinterpret_cast<Block*>(data) + i * ChunkSize3;
	return *reinterpret_cast<Blocks*>(blocks);
}

bool SuperChunk::load()
{
	modified = false;
	assert(!data);
	data = (uint8_t*)malloc(DataSize);
	CHECK(data);

	char* filename = nullptr;
	CHECK(0 < asprintf(&filename, "../world/world.%+d%+d%+d.sc", scpos.x, scpos.y, scpos.z));
	Auto(free(filename));

	FILE* file = fopen(filename, "r");
	if (!file && errno == ENOENT)
	{
		explored().clear_all();
		modified = true;
		return true;
	}
	CHECK(file);
	Auto(fclose(file));
	if (fseek(file, 0, SEEK_END) < 0)
	{
		fprintf(stderr, "fseek(%s, 0, 2) failed: %s (%d)", filename, strerror(errno), errno);
		return false;
	}
	long size = ftell(file);
	CHECK(size >= 0);
	CHECK(fseek(file, 0, SEEK_SET) != -1);

	char* buffer = (char*)malloc(size);
	CHECK(buffer);
	Auto(free(buffer));

	CHECK(fread(buffer, 1, size, file) == size);
	CHECK(LZ4_decompress_safe(buffer, (char*)data, size, DataSize) == DataSize);
	return true;
}

bool SuperChunk::save()
{
	if (!modified) return true;

	char* filename = nullptr;
	CHECK(0 < asprintf(&filename, "../world/world.%+d%+d%+d.sc", scpos.x, scpos.y, scpos.z));
	Auto(free(filename));

	fprintf(stderr, "Saving super chunk [%d %d %d]\n", scpos.x, scpos.y, scpos.z);
	char* buffer = (char*)malloc(LZ4_COMPRESSBOUND(DataSize));
	CHECK(buffer);
	Auto(free(buffer));
	int ret = LZ4_compress((char*)data, buffer, DataSize);
	CHECK(ret != 0);

	FILE* file = fopen(filename, "w");
	CHECK(file);
	if (fwrite(buffer, 1, ret, file) != ret)
	{
		fprintf(stderr, "Failed to write %s\n", filename);
		fclose(file);
		unlink(filename);
		return false;
	}
	fclose(file);
	modified = false;
	return true;
}

struct Chunk
{
	glm::ivec3 icpos;
	SuperChunk* sc;

	glm::ivec3 get_cpos() { return icpos + (sc->scpos << SuperChunkSizeBits); }
	void set(glm::ivec3 pos, Block b) { sc->chunk(icpos)[pos] = b; sc->modified = true; }
	Block operator[](glm::ivec3 pos) const { return sc->chunk(icpos)[pos]; }
	Blocks& blocks() { return sc->chunk(icpos); }

	bool is_active() { return sc->active[icpos]; }
	void activate() { sc->active.set(icpos); }
	void deactivate() { sc->active.clear(icpos); }
};

struct SuperChunkManager
{
	Blocks* acquire_chunk(glm::ivec3 cpos, bool generate)
	{
		//AutoLock(m_lock);
		glm::ivec3 scpos = cpos >> SuperChunkSizeBits;
		SuperChunk* sc = m_map[scpos];
		if (sc == nullptr)
		{
			sc = new SuperChunk(scpos);
			sc->active.set_all();
			if (!sc->load()) exit(1);
			m_map[scpos] = sc;
		}
		sc->refs += 1;

		Blocks& chunk = sc->chunk(cpos & SuperChunkSizeMask);
		//m_lock.unlock();
		//AutoMapLock<glm::ivec3> _(cpos, m_chunk_locks);
		//m_lock.lock();

		if (!sc->explored()[cpos & SuperChunkSizeMask])
		{
			if (!generate) return nullptr;
			//m_lock.unlock();
			generate_chunk(chunk, cpos);
			//m_lock.lock();
			sc->explored().set(cpos & SuperChunkSizeMask);
		}

		return &chunk;
	}

	void release_chunk(glm::ivec3 cpos)
	{
		//AutoLock(m_lock);
		glm::ivec3 scpos = cpos >> SuperChunkSizeBits;
		SuperChunk* sc = m_map[scpos];
		assert(sc);
		assert(sc->refs > 0);
		if (--sc->refs == 0)
		{
			if (!sc->save())
			{
				fprintf(stderr, "ERROR: Failed to save super chunk [%d %d %d]\n", scpos.x, scpos.y, scpos.z);
			}
			free(sc->data);
			delete sc;
			m_map.erase(m_map.find(scpos));
		}
	}

	void save()
	{
		fprintf(stderr, "Not saving!\n");
		return;
		//AutoLock(m_lock);
		for (auto it : m_map)
		{
			if (!it.second->save())
			{
				glm::ivec3 a = it.second->scpos;
				fprintf(stderr, "ERROR: Failed to save super chunk [%d %d %d]\n", a.x, a.y, a.z);
			}
		}
	}

	Chunk get(glm::ivec3 cpos)
	{
		Chunk chunk;
		auto it = m_map.find(cpos >> SuperChunkSizeBits);
		chunk.sc = (it == m_map.end()) ? nullptr : it->second;
		chunk.icpos = cpos & SuperChunkSizeMask;
		return chunk;
	}

private:
	//MapLock<glm::ivec3> m_chunk_locks;

	//std::mutex m_lock;
	std::unordered_map<glm::ivec3, SuperChunk*> m_map;
};

SuperChunkManager g_scm;

// =============

struct BlockRef
{
	Chunk chunk;
	glm::i8vec3 ipos;
	Block block;

	operator Block() { return block; }
	BlockRef() { }
	explicit BlockRef(glm::ivec3 p) : chunk(g_scm.get(p >> ChunkSizeBits)), ipos(p & ChunkSizeMask), block(chunk.sc ? chunk.sc->chunk(chunk.icpos)[glm::ivec3(ipos)] : Block::none) { }
};

static const int SimulationDistance = 7; // in chunks

static const int MaxActiveChunks = 100;
std::vector<glm::ivec3> sim_active_chunks;

void activate_block(glm::ivec3 pos)
{
	glm::ivec3 a = (pos - ii) >> ChunkSizeBits;
	glm::ivec3 b = (pos + ii) >> ChunkSizeBits;
	FOR2(x, a.x, b.x) FOR2(y, a.y, b.y) FOR2(z, a.z, b.z)
	{
		glm::ivec3 cpos(x, y, z);
		Chunk chunk = g_scm.get(cpos);
		if (chunk.sc) chunk.activate();
	}
}

void update_block(BlockRef& ref, Block b)
{
	ref.block = b;
	glm::ivec3 q(ref.ipos);
	glm::ivec3 p = ref.chunk.get_cpos();
	ref.chunk.set(glm::ivec3(ref.ipos), b);
	glm::ivec3 pos = q + (p << ChunkSizeBits);
	activate_block(pos);
}

Block get_block(glm::ivec3 pos)
{
	return g_scm.get(pos >> ChunkSizeBits)[pos & ChunkSizeMask];
}

Block get_block(glm::ivec3 pos, Block def)
{
	Chunk chunk = g_scm.get(pos >> ChunkSizeBits);
	if (chunk.sc == nullptr) return def;
	return chunk[pos & ChunkSizeMask];
}

void update_block(glm::ivec3 pos, Block b)
{
	g_scm.get(pos >> ChunkSizeBits).set(pos & ChunkSizeMask, b);
	activate_block(pos);
}

const char* block_name_safe(Block block)
{
	return ((int)block < block_count) ? block_name[(int)block] : "";
}

// returns true when src becomes empty
bool water_flow(BlockRef& src, BlockRef& dest, int w)
{
	assert(water_level(dest) + w <= (uint)Block::water);
	int base = (dest == Block::none) ? int(Block::water1) - 1 : int(dest.block);
	update_block(dest, Block(base + w));

	int level = water_level(src);
	assertf(w > 0 && w <= level, "w=%d level=%d", w, level);
	update_block(src, (w == level) ? Block::none : Block(int(Block::water1) - 1 + level - w));
	return w == level;
}

Sphere simulation_sphere(SimulationDistance);
std::vector<glm::i8vec2> sim_order;
Initialize
{
	sim_order.reserve(ChunkSize2);
	FOR(x, ChunkSize) FOR(y, ChunkSize) sim_order.push_back(glm::i8vec2(x, y));
}

bool water_under(BlockRef b)
{
	BlockRef m((b.chunk.get_cpos() << ChunkSizeBits) + glm::ivec3(b.ipos) - iz);
	return m.chunk.sc && m.block == Block::water;
}

void model_simulate_water(BlockRef b, glm::ivec3 bpos)
{
	int w = water_level(b);

	// Flow down
	BlockRef m(bpos - iz);
	if (m.chunk.sc && accepts_water(m.block))
	{
		int flow = std::min(w, (int)Block::water - water_level(m));
		if (water_flow(b, m, flow)) return;
		w -= flow;
	}

	// Flow sideways
	if (w > 0)
	{
		ivector<BlockRef, 4> side;
		for (auto v : { -ix, ix, -iy, iy })
		{
			BlockRef q(bpos + v);
			if (q.chunk.sc && (q == Block::none || (q >= Block::water1 && q < b))) side.push_back(q);
		}
		while (side.size() > 0)
		{
			int e = rand() % side.size();
			BlockRef m = side[e];
			if ((water_level(m) < w) && (w > 1 || water_under(m)))
			{
				int flow = (w - water_level(m) + 1) / 2;
				water_flow(b, m, flow);
				w -= flow;
			}
			side[e] = side[side.size() - 1];
			side.pop_back();
		}
	}

	// Flow diagonaly
	if (w > 0)
	{
		ivector<BlockRef, 4> side;
		FOR(j, 4)
		{
			glm::ivec3 xx((j%2)*2-1, 0, 0), yy(0, (j/2)*2-1, 0);
			BlockRef px(bpos + xx), py(bpos + yy), q(bpos + xx + yy);
			if (px.chunk.sc && accepts_water(px) && py.chunk.sc && accepts_water(py) && q.chunk.sc && (q == Block::none || (q >= Block::water1 && q < b))) side.push_back(q);
		}
		while (side.size() > 0)
		{
			int e = rand() % side.size();
			BlockRef m = side[e];
			if ((water_level(m) < w) && (w > 1 || water_under(m)))
			{
				int flow = (w - water_level(m) + 1) / 2;
				water_flow(b, m, flow);
				w -= flow;
			}
			side[e] = side[side.size() - 1];
			side.pop_back();
		}
	}

	// Flow down sideways
	if (w == 1)
	{
		ivector<BlockRef, 4> side;
		for (auto v : { -ix, ix, -iy, iy })
		{
			BlockRef p(bpos + v), q(bpos + v - iz);
			if (p.chunk.sc && p == Block::none && q.chunk.sc && accepts_water(q)) side.push_back(q);
		}
		if (side.size() > 0)
		{
			BlockRef m = side[rand() % side.size()];
			if (water_flow(b, m, 1)) return;
		}
	}

	// Flow down diagonaly
	if (w == 1)
	{
		ivector<BlockRef, 4> side;
		FOR(j, 4)
		{
			glm::ivec3 xx((j%2)*2-1, 0, 0), yy(0, (j/2)*2-1, 0);
			BlockRef p(bpos + xx + yy), px(bpos + xx), py(bpos + yy), q(bpos + xx + yy - iz);
			if (p.chunk.sc && p == Block::none && px.chunk.sc && px == Block::none && py.chunk.sc && py == Block::none && q.chunk.sc && accepts_water(q)) side.push_back(q);
		}
		if (side.size() > 0)
		{
			BlockRef m = side[rand() % side.size()];
			if (water_flow(b, m, 1)) return;
		}
	}

	// Evaporate
	if (w == 1)
	{
		if (rand() % 100 == 0)
		{
			update_block(b, Block::none);
		}
		else
		{
			b.chunk.activate();
		}
	}
	if (w == 14)
	{
		if (rand() % 100 == 0)
		{
			update_block(b, Block::water);
		}
		else
		{
			b.chunk.activate();
		}
	}
}

void model_simulate_block(glm::ivec3 pos)
{
	BlockRef b(pos);
	if (is_water(b)) model_simulate_water(b, pos);
	else if (is_sand(b))
	{
		// Flow down swapping with water
		BlockRef m(pos - iz);
		if (m.chunk.sc && (m == Block::none || is_water(m)))
		{
			Block e = m.block;
			update_block(m, b.block);
			update_block(b, e);
		}
	}
	else if (b == Block::water_source)
	{
		BlockRef m(pos - iz);
		if (!m.chunk.sc) return;
		if (m == Block::none || is_water_partial(m))
		{
			int w = water_level(m);
			update_block(m, Block(int(Block::water1) + w));
		}
	}
	else if (b == Block::water_drain)
	{
		BlockRef m(pos + iz);
		if (m.chunk.sc && is_water(m))
		{
			int w = water_level(m);
			update_block(m, (w == 1) ? Block::none : Block(int(Block::water1) + w - 2));
		}
	}
	else if (b == Block::soul_sand)
	{
		// Morph water around it
		bool active = false;
		for (auto v : { -ix, ix, -iy, iy, -iz, iz })
		{
			BlockRef q(pos + v);
			if (q.chunk.sc && is_water(q)) { update_block(q, Block::soul_sand); active = true; }
		}
		if (!active && rand() % 10 == 0)
		{
			update_block(b, Block::none);
		}
		else
		{
			b.chunk.activate();
		}
	}
}

BitSet<ChunkSizeBits> sim_visited_set;
BitSet<ChunkSizeBits> sim_visited_local;
std::vector<glm::ivec3> sim_visited_list;

bool less(glm::ivec3 a, glm::ivec3 b)
{
	if (a.x != b.x) return a.x < b.x;
	if (a.y != b.y) return a.y < b.y;
	return a.z < b.z;
}

void model_simulate_gravity()
{
	// All unsupported blocks will be moved down by one (except clouds, sand, water)
	sim_visited_set.clear();
	sim_visited_list.clear();
	for (glm::ivec3 cpos : sim_active_chunks)
	{
		Blocks& blocks = g_scm.get(cpos).blocks();
		FOR(z, ChunkSize) FOR(y, ChunkSize) FOR(x, ChunkSize)
		{
			glm::ivec3 v(x, y, z);
			const Block b = blocks[v];
			if (b == Block::none || b == Block::cloud || b == Block::sand || b == Block::red_sand || is_water(b)) continue;
			v += cpos << ChunkSizeBits;
			if (!sim_visited_set.xset(v)) continue;
			sim_visited_local.clear();
			sim_visited_local.xset(v);

			const uint e = sim_visited_list.size();
			sim_visited_list.push_back(v);
			for (uint i = e; i < sim_visited_list.size(); i++)
			{
				v = sim_visited_list[i];
				for (const glm::ivec3 d : face_dir)
				{
					if (!sim_visited_local.xset(v + d)) continue;

					Chunk c = g_scm.get((v + d) >> ChunkSizeBits);
					if (!c.sc)
					{
						sim_visited_list.resize(e);
						break;
					}
					const Block b = c[(v + d) & ChunkSizeMask];
					if (b == Block::none || b == Block::cloud) continue;
					if (d.z == 0 && (b == Block::sand || b == Block::red_sand || is_water(b))) continue;
					if (!sim_visited_set.xset(v + d))
					{
						sim_visited_list.resize(e);
						break;
					}
					sim_visited_list.push_back(v + d);
				}
				if (sim_visited_list.size() - e > 200)
				{
					sim_visited_list.resize(e);
					break;
				}
			}
		}
	}
	std::sort(sim_visited_list.begin(), sim_visited_list.end(), less);

	auto a = sim_visited_list.begin();
	while (a != sim_visited_list.end())
	{
		auto b = a + 1;
		while (b != sim_visited_list.end() && *b == *a + iz) b += 1;

		glm::ivec3 q = *(b - 1);
		while (true)
		{
			Block e = get_block(q + iz, Block::none);
			if (e != Block::sand && e != Block::red_sand && !is_water(e)) break;
			q.z += 1;
		}

		FOR2(i, a->z, q.z)
		{
			update_block(glm::ivec3(q.x, q.y, i-1), get_block(glm::ivec3(q.x, q.y, i)));
		}
		update_block(q, Block::none);

		a = b;
	}
}

void server_simulate_blocks()
{
	sim_active_chunks.clear();
	for (glm::ivec3 d : simulation_sphere)
	{
		for (Connection* conn : g_connections)
		{
			glm::ivec3 cpos = conn->m_cpos + d;
			Chunk chunk = g_scm.get(cpos);
			if (!chunk.sc || !chunk.is_active()) continue;
			chunk.deactivate();
			sim_active_chunks.push_back(cpos);
			if (sim_active_chunks.size() == MaxActiveChunks) goto exit_loop;
		}
	}
	exit_loop:;

	// shuffle sim_order
	if (sim_active_chunks.size() > 0) FOR(i, ChunkSize2 / 4)
	{
		std::swap(sim_order[rand() % ChunkSize2], sim_order[rand() % ChunkSize2]);
	}

	for (glm::ivec3 cpos : sim_active_chunks)
	{
		Chunk chunk = g_scm.get(cpos);
		FOR(z, ChunkSize) for (glm::i8vec2 xy : sim_order)
		{
			model_simulate_block(glm::ivec3(xy.x, xy.y, z) + (cpos << ChunkSizeBits));
		}
	}

	model_simulate_gravity();
}

// =============

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

void server_edit_block(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	Blocks& chunk = *g_scm.acquire_chunk(cpos, true); // TODO: release?
	chunk[pos & ChunkSizeMask] = block;
	for (Connection* conn : g_connections)
	{
		// ISSUE: if distance is >40, but still inside Map then client will skip update to chunk
		// TODO: ensure robust synchnorization of chunks between client and server.
		//       client can only change its local Map origin (cpos) when it gets ack from server for its position update.
		if (glm::distance2(conn->m_cpos, cpos) <= sqr(40/*RenderDistance*/))
		{
			conn->send_chunk(cpos, chunk);
		}
	}
}

int g_simulate = 0;

void server_receive_text_message(Connection& conn, const char* message, uint length)
{
	fprintf(stderr, "Player #%d [%s]: %.*s\n", conn.avatar.id, conn.host, length, message);

	std::vector<Token> tokens;
	tokenize(message, length, /*out*/tokens);
	if (tokens.size() == 0) return;

	if (tokens[0] == "chat")
	{
		if (tokens.size() < 2) return;
		for (Connection* conn2 : g_connections)
		{
			write_text_message(conn2->send_buffer, "chat %d %.*s", conn2->avatar.id, length - (tokens[1].first - message), tokens[1].first);
		}
		return;
	}

	if (tokens[0] == "block")
	{
		if (tokens.size() < 5 || !is_integer(tokens[1]) || !is_integer(tokens[2]) || !is_integer(tokens[3]) || !is_integer(tokens[4])) return;
		glm::ivec3 pos(parse_int(tokens[1]), parse_int(tokens[2]), parse_int(tokens[3]));
		int block = parse_int(tokens[4]);
		if (block < 0 || block >= block_count) return;
		server_edit_block(pos, (Block)block);
		return;
	}

	if (tokens[0] == "simulate")
	{
		if (tokens.size() < 2 || !is_integer(tokens[1])) return;
		g_simulate = parse_int(tokens[1]);
		return;
	}

	if (tokens[0] == "fsync")
	{
		g_scm.save(); // blocking operation!
		write_text_message(conn.send_buffer, "fsync_ack");
		return;
	}
}

bool server_receive_message(Connection& conn)
{
	SocketBuffer& recv = conn.recv_buffer;
	if (recv.size() == 0) return false;
	switch ((MessageType)recv.data()[0])
	{
	case MessageType::Text:
	{
		auto message = read_text_message(recv);
		if (!message) return false;
		server_receive_text_message(conn, message->text, message->size);
		return true;
	}
	case MessageType::AvatarState:
	{
		auto message = recv.read<MessageAvatarState>();
		if (!message) return false;
		conn.avatar.pitch = message->pitch;
		conn.avatar.yaw = message->yaw;
		conn.avatar.position = message->position;
		conn.avatar.broadcasted = false;
		conn.update_cpos();
		return true;
	}
	case MessageType::ChunkState: FAIL;
	}
	return false;
}

float exchange_time_ms = 0;
float inbox_time_ms = 0;
float simulation_time_ms = 0;
float chunk_time_ms = 0;
float avatar_time_ms = 0;

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

	MessageServerStatus mss;
	mss.type = MessageType::ServerStatus;
	mss.frame = 0;

	Timestamp ta;
	while (true)
	{
		if (new_connection != nullptr)
		{
			Connection* conn = new_connection;
			conn->recv_buffer.reserve(1 << 20);
			// TODO: increase kernel socket recv and send buffer sizes!
			conn->avatar.id = create_id();
			conn->avatar.broadcasted = true;
			fprintf(stderr, "Player #%d connected from %s\n", conn->avatar.id, conn->host);
			// TODO: send to new player positions of all other avatars (as they may be standing still)
			for (Connection* conn2 : g_connections)
			{
				write_text_message(conn2->send_buffer, "joined %d", conn->avatar.id);
			}
			g_connections.push_back(conn);
			new_connection = nullptr;
		}

		Timestamp tb;
		for (int i = 0; i < g_connections.size(); i++)
		{
			Connection* conn = g_connections[i];

			if (!conn->exchange())
			{
				fprintf(stderr, "Player #%d disconnected from %s\n", conn->avatar.id, conn->host);
				for (Connection* conn2 : g_connections)
				{
					if (conn != conn2) write_text_message(conn->send_buffer, "left #%d", conn->avatar.id);
				}
				destroy_id(conn->avatar.id);
				delete conn;
				g_connections[i] = g_connections.back();
				g_connections.pop_back();
				i -= 1;
			}
			else if (conn->send_buffer.size() >= (1 << 20))
			{
				fprintf(stderr, "Player %d send_buffer.size %u\n", conn->avatar.id, conn->send_buffer.size());
			}
		}

		Timestamp tc;
		for (Connection* conn : g_connections)
		{
			while (server_receive_message(*conn)) { }
		}

		Timestamp td;
		server_simulate_blocks();

		// send chunk updates
		Timestamp te;
		for (Connection* conn : g_connections)
		{
			Timestamp ta;
			while (conn->m_scaned_chunks < g_server_render_sphere.size())
			{
				glm::ivec3 cpos = conn->m_cpos + g_server_render_sphere[conn->m_scaned_chunks];
				if (conn->m_chunks[cpos & MapSizeBits] != cpos)
				{
					Blocks& chunk = *g_scm.acquire_chunk(cpos, true); // TODO: use chunk_gen_budget // TODO: release?
					conn->send_chunk(cpos, chunk);
				}
				conn->m_scaned_chunks += 1;
				if (ta.elapsed_ms() > 10) break;
			}
		}

		// broadcast avatar states
		Timestamp tf;
		for (Connection* conn : g_connections)
		{
			if (conn->avatar.broadcasted) continue;
			MessageAvatarState message;
			message.type = MessageType::AvatarState;
			message.id = conn->avatar.id;
			message.position = conn->avatar.position;
			message.pitch = conn->avatar.pitch;
			message.yaw = conn->avatar.yaw;
			for (Connection* conn2 : g_connections)
			{
				if (conn2 != conn) conn2->send_buffer.write(message);
			}
			conn->avatar.broadcasted = true;
		}
		Timestamp tg;

		exchange_time_ms   = glm::mix<float>(exchange_time_ms,   tb.elapsed_ms(tc), 0.15f);
		inbox_time_ms      = glm::mix<float>(inbox_time_ms,      tc.elapsed_ms(td), 0.15f);
		simulation_time_ms = glm::mix<float>(simulation_time_ms, td.elapsed_ms(te), 0.15f);
		chunk_time_ms      = glm::mix<float>(chunk_time_ms,      te.elapsed_ms(tf), 0.15f);
		avatar_time_ms     = glm::mix<float>(avatar_time_ms,     tf.elapsed_ms(tg), 0.15f);

		mss.exchange_time = exchange_time_ms * 10;
		mss.inbox_time = inbox_time_ms * 10;
		mss.simulation_time = simulation_time_ms * 10;
		mss.chunk_time = chunk_time_ms * 10;
		mss.avatar_time = avatar_time_ms * 10;
		mss.frame += 1;
		for (Connection* conn : g_connections) conn->send_buffer.write(mss);

		Timestamp tx;
		double ft = ta.elapsed_ms(tx);
		if (ft < 10) usleep(int((10 - ft) * 1000));
		ta = tx;
	}
}
