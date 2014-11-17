#include "block.hh"
#include "algorithm.hh"

struct Heightmap
{
	int height[ChunkSize * MapSize][ChunkSize * MapSize];
	uint8_t treeType[ChunkSize * MapSize][ChunkSize * MapSize];
	Block color[ChunkSize * MapSize][ChunkSize * MapSize];
	glm::ivec2 last[MapSize][MapSize];

	Heightmap()
	{
		FOR(x, ChunkSize) FOR(y, ChunkSize) last[x][y] = glm::ivec2(1000000, 1000000);
	}

	void Populate(int cx, int cy);
	int& Height(int x, int y) { return height[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
	uint8_t& TreeType(int x, int y) { return treeType[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
	Block& Color(int x, int y) { return color[x & (ChunkSize * MapSize - 1)][y & (ChunkSize * MapSize - 1)]; }
};

int GetHeight(int x, int y)
{
	float q = noise(glm::vec2(x, y) * 0.004f, 6, 0.5f, 0.5f, true);
	return q * q * q * q * 200;
}

Block GetColor(int x, int y)
{
	static Block surface[] = { Block::lava_flow, Block::lava_still, Block::netherrack, Block::red_sand,
		Block::sand, Block::coarse_dirt, Block::dirt, Block::grass, Block::grass_snowed, Block::snow };
	float n = noise(glm::vec2(x, y) * -0.003f, 8, 0.5f, 0.75f, false);
	return surface[((int)(n * 8) + 4) % 10];
}

float Tree(int x, int y)
{
	return noise(glm::vec2(x+321398, y+8901) * 0.002f, 4, 2.0f, 0.5f, true);
}

uint8_t GetTreeType(int x, int y)
{
	float a = Tree(x, y);
	FOR2(xx, -1, 1) FOR2(yy, -1, 1)
	{
		if ((xx != 0 || yy != 0) && a <= Tree(x + xx, y + yy))
			return 0;
	}
	return 1 + (uint)(noise(glm::vec2(x, y) * 0.245f, 1.0f, 0.5f, 0.5f, true) * 60) % 6;
}

void Heightmap::Populate(int cx, int cy)
{
	if (last[cx & MapSizeMask][cy & MapSizeMask] != glm::ivec2(cx, cy))
	{
		FOR(x, ChunkSize) FOR(y, ChunkSize)
		{
			Height(x + cx * ChunkSize, y + cy * ChunkSize) = GetHeight(x + cx * ChunkSize, y + cy * ChunkSize);
			TreeType(x + cx * ChunkSize, y + cy * ChunkSize) = GetTreeType(x + cx * ChunkSize, y + cy * ChunkSize);
			Color(x + cx * ChunkSize, y + cy * ChunkSize) = GetColor(x + cx * ChunkSize, y + cy * ChunkSize);
		}
		last[cx & MapSizeMask][cy & MapSizeMask] = glm::ivec2(cx, cy);
	}
}

static Heightmap* g_heightmap = new Heightmap;

const int CraterRadius = 500;
const glm::ivec3 CraterCenter(CraterRadius * -0.8, CraterRadius * -0.8, 0);

const int MoonRadius = 500;
const glm::ivec3 MoonCenter(MoonRadius * 0.8, MoonRadius * 0.8, 0);

Block generate_block(glm::ivec3 pos)
{
	// crater
	if (pos.z < 100)
	{
		int64_t sx = sqr<int64_t>(pos.x - CraterCenter.x) - sqr<int64_t>(CraterRadius);
		if (sx <= 0)
		{
			int64_t sy = sx + sqr<int64_t>(pos.y - CraterCenter.y);
			if (sy <= 0)
			{
				int64_t sz = sy + sqr<int64_t>(pos.z - CraterCenter.z);
				if (sz <= 0) return Block::none;
			}
		}
	}

	// moon
	int64_t sx = sqr<int64_t>(pos.x - MoonCenter.x) - sqr<int64_t>(MoonRadius);
	if (sx <= 0)
	{
		int64_t sy = sx + sqr<int64_t>(pos.y - MoonCenter.y);
		if (sy <= 0)
		{
			int64_t sz = sy + sqr<int64_t>(pos.z - MoonCenter.z);
			if (sz <= 0)
			{
				double q = noise(glm::vec3(pos) * 0.03f, 4, 0.5f, 0.5f, false);
				static Block ores[6] = { Block::gold_ore, Block::coal_ore, Block::diamond_ore, Block::redstone_ore, Block::emerald_ore, Block::lapis_ore};
				if (q >= 0.6) return ores[uint(pos.x ^ pos.y ^ pos.z) / 3 % 6];
				return Block::stone;
			}
		}
	}

	if (pos.x >= 0 && pos.x < 64 && pos.z == 3 && pos.y >= 0 && pos.y < 16 && (pos.x % 3) == 0 && (pos.y % 3) == 0)
	{
		int i = (pos.x / 3) * 6 + pos.y / 3 + 1;
		if (i < block_count && Block(i) != Block::water_source) return (Block)i;
	}

	if (pos.z == 0 && (pos.x == 81 || pos.x == 80) && pos.y <= -13 && pos.y >= -22)
	{
		return Block::water_source;
	}

	// Tree
	if (g_heightmap->TreeType(pos.x, pos.y))
	{
		int height = g_heightmap->Height(pos.x, pos.y);
		if (pos.z > height && pos.z < height + 6) return Block(uint(Block::log_acacia) + g_heightmap->TreeType(pos.x, pos.y) - 1);
	}
	else for(glm::ivec2 i : { glm::ivec2(0, -1), glm::ivec2(0, 1), glm::ivec2(-1, 0), glm::ivec2(1, 0) })
	{
		if (g_heightmap->TreeType(pos.x + i.x, pos.y + i.y))
		{
			int height = g_heightmap->Height(pos.x + i.x, pos.y + i.y);
			if (pos.z > height + 2 && pos.z < height + 6) return Block(uint(Block::leaves_acacia) + g_heightmap->TreeType(pos.x + i.x, pos.y + i.y) - 1);
			break;
		}
	}

	if (pos.z > 100 && pos.z < 200)
	{
		// clouds
		double q = noise(glm::vec3(pos) * 0.01f, 4, 0.5f, 0.5f, false);
		if (q < -0.35) return Block::cloud;
	}
	else if (pos.z <= g_heightmap->Height(pos.x, pos.y))
	{
		// ground and caves
		double q = noise(glm::vec3(pos) * 0.03f, 4, 0.5f, 0.5f, false);
		static Block ores[6] = { Block::gold_ore, Block::coal_ore, Block::diamond_ore, Block::redstone_ore, Block::emerald_ore, Block::lapis_ore};
		if (q >= 0.6) return ores[uint(pos.x ^ pos.y ^ pos.z) / 3 % 6];
		if (q >= -0.25)
		{
			int d = g_heightmap->Height(pos.x, pos.y) - pos.z;
			Block b = g_heightmap->Color(pos.x, pos.y);
			if (d > 3 && is_sand(b)) b = Block::dirt;
			return b;
		}
	}

	return Block::none;
}

void generate_chunk(XCube<ChunkSize, Block>& chunk, glm::ivec3 cpos)
{
	g_heightmap->Populate(cpos.x, cpos.y);
	FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
	{
		glm::ivec3 v(x, y, z);
		chunk[v] = generate_block(cpos * ChunkSize + v);
	}
}
