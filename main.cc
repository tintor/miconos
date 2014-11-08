#include "lz4.h"
#include "ply_io.h"
#include <unordered_map>

#include "util.hh"
#include "algorithm.hh"
#include "rendering.hh"
#include "auto.hh"
#include "socket.hh"

#define LODEPNG_COMPILE_CPP
#include "lodepng/lodepng.h"

// GUI

int width;
int height;

// Map

const int ChunkSizeBits = 4, SuperChunkSizeBits = 4, MapSizeBits = 7;
const int ChunkSize = 1 << ChunkSizeBits, SuperChunkSize = 1 << SuperChunkSizeBits, MapSize = 1 << MapSizeBits;
const int CMin = 0, CMax = ChunkSize - 1;
const int ChunkSizeMask = ChunkSize - 1, SuperChunkSizeMask = SuperChunkSize - 1, MapSizeMask = MapSize - 1;

const int ChunkSize2 = ChunkSize * ChunkSize;
const int ChunkSize3 = ChunkSize * ChunkSize * ChunkSize;

const int RenderDistance = 40;
static_assert(RenderDistance < MapSize / 2, "");

Sphere render_sphere(RenderDistance);

// ========================

#define FuncStr(E) #E,
#define FuncCount(E) +1
#define FuncList(E) E,

#define Blocks(A, F, B) A \
	F(none) \
	F(water1) \
	F(water2) \
	F(water3) \
	F(water4) \
	F(water5) \
	F(water6) \
	F(water7) \
	F(water8) \
	F(water9) \
	F(water10) \
	F(water11) \
	F(water12) \
	F(water13) \
	F(water14) \
	F(water) \
	F(cloud) \
	F(leaves_acacia) \
	F(leaves_big_oak) \
	F(leaves_birch) \
	F(leaves_jungle) \
	F(leaves_oak) \
	F(leaves_spruce) \
	F(ice) \
	F(glass_white) \
	F(log_acacia) \
	F(log_big_oak) \
	F(log_birch) \
	F(log_jungle) \
	F(log_oak) \
	F(log_spruce) \
	F(planks_acacia) \
	F(planks_big_oak) \
	F(planks_birch) \
	F(planks_jungle) \
	F(planks_oak) \
	F(planks_spruce) \
	F(bookshelf) \
	F(bedrock) \
	F(brick) \
	F(cactus) \
	F(cauldron) \
	F(clay) \
	F(coal_block) \
	F(coal_ore) \
	F(cobblestone) \
	F(dirt) \
	F(daylight_detector) \
	F(furnace) \
	F(pumpkin) \
	F(quartz_block) \
	F(quartz_ore) \
	F(gold_block) \
	F(gold_ore) \
	F(grass) \
	F(hay_block) \
	F(iron_block) \
	F(iron_ore) \
	F(ice_packed) \
	F(red_sand) \
	F(red_sandstone) \
	F(red_sandstone_carved) \
	F(redstone_block) \
	F(redstone_lamp) \
	F(redstone_ore) \
	F(sand) \
	F(sandstone) \
	F(sandstone_carved) \
	F(stone) \
	F(stone_andesite) \
	F(stone_diorite) \
	F(stone_granite) \
	F(soul_sand) \
	F(coarse_dirt) \
	F(cobblestone_mossy) \
	F(command_block) \
	F(crafting_table) \
	F(diamond_block) \
	F(diamond_ore) \
	F(emerald_block) \
	F(emerald_ore) \
	F(enchanting_table) \
	F(dirt_podzol) \
	F(grass_snowed) \
	F(gravel) \
	F(lapis_block) \
	F(lapis_ore) \
	F(lava_still) \
	F(lava_flow) \
	F(melon) \
	F(tnt) \
	F(mycelium) \
	F(nether_brick) \
	F(netherrack) \
	F(obsidian) \
	F(netherreator) \
	F(piston) \
	F(piston_sticky) \
	F(prismarine_bricks) \
	F(prismarine_dark) \
	F(prismarine_rough) \
	F(sea_lantern) \
	F(slime) \
	F(snow) \
	F(sponge) \
	F(stonebrick) \
	F(stonebrick_mossy) \
	F(stonebrick_cracked) \
	F(stonebrick_carved) \
	F(water_source) \
	F(water_drain) \
	B

const char* block_name[] = Blocks({, FuncStr, });
static const uint block_count = Blocks(0, FuncCount, +0);
static_assert(block_count <= 256, "");
enum class Block : uint8_t Blocks({, FuncList, });

static_assert((uint)Block::none == 0, "common in conditions");

#define BlockTextures(A, F, B) A \
	F(leaves_acacia) \
	F(leaves_big_oak) \
	F(leaves_birch) \
	F(leaves_jungle) \
	F(leaves_oak) \
	F(leaves_spruce) \
	F(lava_flow) \
	F(lava_flow_1) \
	F(lava_flow_2) \
	F(lava_flow_3) \
	F(lava_flow_4) \
	F(lava_flow_5) \
	F(lava_flow_6) \
	F(lava_flow_7) \
	F(lava_flow_8) \
	F(lava_flow_9) \
	F(lava_flow_10) \
	F(lava_flow_11) \
	F(lava_flow_12) \
	F(lava_flow_13) \
	F(lava_flow_14) \
	F(lava_flow_15) \
	F(lava_flow_16) \
	F(lava_flow_17) \
	F(lava_flow_18) \
	F(lava_flow_19) \
	F(lava_flow_20) \
	F(lava_flow_21) \
	F(lava_flow_22) \
	F(lava_flow_23) \
	F(lava_flow_24) \
	F(lava_flow_25) \
	F(lava_flow_26) \
	F(lava_flow_27) \
	F(lava_flow_28) \
	F(lava_flow_29) \
	F(lava_flow_30) \
	F(lava_flow_31) \
	F(lava_still) \
	F(lava_still_1) \
	F(lava_still_2) \
	F(lava_still_3) \
	F(lava_still_4) \
	F(lava_still_5) \
	F(lava_still_6) \
	F(lava_still_7) \
	F(lava_still_8) \
	F(lava_still_9) \
	F(lava_still_10) \
	F(lava_still_11) \
	F(lava_still_12) \
	F(lava_still_13) \
	F(lava_still_14) \
	F(lava_still_15) \
	F(lava_still_16) \
	F(lava_still_17) \
	F(lava_still_18) \
	F(lava_still_19) \
	F(lava_still_20) \
	F(lava_still_21) \
	F(lava_still_22) \
	F(lava_still_23) \
	F(lava_still_24) \
	F(lava_still_25) \
	F(lava_still_26) \
	F(lava_still_27) \
	F(lava_still_28) \
	F(lava_still_29) \
	F(lava_still_30) \
	F(lava_still_31) \
	F(water_still) \
	F(water_still_1) \
	F(water_still_2) \
	F(water_still_3) \
	F(water_still_4) \
	F(water_still_5) \
	F(water_still_6) \
	F(water_still_7) \
	F(water_still_8) \
	F(water_still_9) \
	F(water_still_10) \
	F(water_still_11) \
	F(water_still_12) \
	F(water_still_13) \
	F(water_still_14) \
	F(water_still_15) \
	F(water_still_16) \
	F(water_still_17) \
	F(water_still_18) \
	F(water_still_19) \
	F(water_still_20) \
	F(water_still_21) \
	F(water_still_22) \
	F(water_still_23) \
	F(water_still_24) \
	F(water_still_25) \
	F(water_still_26) \
	F(water_still_27) \
	F(water_still_28) \
	F(water_still_29) \
	F(water_still_30) \
	F(water_still_31) \
	F(water_still_32) \
	F(water_still_33) \
	F(water_still_34) \
	F(water_still_35) \
	F(water_still_36) \
	F(water_still_37) \
	F(water_still_38) \
	F(water_still_39) \
	F(water_still_40) \
	F(water_still_41) \
	F(water_still_42) \
	F(water_still_43) \
	F(water_still_44) \
	F(water_still_45) \
	F(water_still_46) \
	F(water_still_47) \
	F(water_still_48) \
	F(water_still_49) \
	F(water_still_50) \
	F(water_still_51) \
	F(water_still_52) \
	F(water_still_53) \
	F(water_still_54) \
	F(water_still_55) \
	F(water_still_56) \
	F(water_still_57) \
	F(water_still_58) \
	F(water_still_59) \
	F(water_still_60) \
	F(water_still_61) \
	F(water_still_62) \
	F(water_still_63) \
	F(pumpkin_face_off) \
	F(pumpkin_face_on) \
	F(furnace_front_on) \
	F(ff_1) \
	F(ff_2) \
	F(ff_3) \
	F(ff_4) \
	F(ff_5) \
	F(ff_6) \
	F(ff_7) \
	F(ff_8) \
	F(ff_9) \
	F(ff_10) \
	F(ff_11) \
	F(ff_12) \
	F(ff_13) \
	F(ff_14) \
	F(ff_15) \
	F(sea_lantern) \
	F(sl_1) \
	F(sl_2) \
	F(sl_3) \
	F(sl_4) \
	F(redstone_lamp_off) \
	F(redstone_lamp_on) \
	F(redstone_lamp_top_off) \
	F(redstone_lamp_top_on) \
	F(water_flow) \
	F(water_flow_1) \
	F(water_flow_2) \
	F(water_flow_3) \
	F(water_flow_4) \
	F(water_flow_5) \
	F(water_flow_6) \
	F(water_flow_7) \
	F(water_flow_8) \
	F(water_flow_9) \
	F(water_flow_10) \
	F(water_flow_11) \
	F(water_flow_12) \
	F(water_flow_13) \
	F(water_flow_14) \
	F(water_flow_15) \
	F(water_flow_16) \
	F(water_flow_17) \
	F(water_flow_18) \
	F(water_flow_19) \
	F(water_flow_20) \
	F(water_flow_21) \
	F(water_flow_22) \
	F(water_flow_23) \
	F(water_flow_24) \
	F(water_flow_25) \
	F(water_flow_26) \
	F(water_flow_27) \
	F(water_flow_28) \
	F(water_flow_29) \
	F(water_flow_30) \
	F(water_flow_31) \
	F(pumpkin_side) \
	F(pumpkin_top) \
	F(log_acacia) \
	F(log_acacia_top) \
	F(log_big_oak) \
	F(log_big_oak_top) \
	F(log_birch) \
	F(log_birch_top) \
	F(log_jungle) \
	F(log_jungle_top) \
	F(log_oak) \
	F(log_oak_top) \
	F(log_spruce) \
	F(log_spruce_top) \
	F(planks_acacia) \
	F(planks_big_oak) \
	F(planks_birch) \
	F(planks_jungle) \
	F(planks_oak) \
	F(planks_spruce) \
	F(bookshelf) \
	F(bedrock) \
	F(brick) \
	F(cactus_bottom) \
	F(cactus_side) \
	F(cactus_top) \
	F(cauldron_side) \
	F(cauldron_bottom) \
	F(cauldron_top) \
	F(clay) \
	F(coal_ore) \
	F(cobblestone) \
	F(dirt) \
	F(daylight_detector_side) \
	F(daylight_detector_top) \
	F(furnace_front_off) \
	F(furnace_side) \
	F(furnace_top) \
	F(glass_white) \
	F(gold_block) \
	F(gold_ore) \
	F(grass_side) \
	F(grass_top) \
	F(hay_block_side) \
	F(hay_block_top) \
	F(iron_block) \
	F(iron_ore) \
	F(ice_packed) \
	F(quartz_block_bottom) \
	F(quartz_block_top) \
	F(quartz_block_side) \
	F(quartz_ore) \
	F(red_sand) \
	F(red_sandstone_normal) \
	F(red_sandstone_carved) \
	F(red_sandstone_bottom) \
	F(red_sandstone_top) \
	F(redstone_block) \
	F(redstone_ore) \
	F(sand) \
	F(sandstone_bottom) \
	F(sandstone_top) \
	F(sandstone_normal) \
	F(sandstone_carved) \
	F(stone) \
	F(stone_andesite) \
	F(stone_diorite) \
	F(stone_granite) \
	F(soul_sand) \
	F(coal_block) \
	F(coarse_dirt) \
	F(cobblestone_mossy) \
	F(command_block) \
	F(crafting_table_front) \
	F(crafting_table_bottom) \
	F(crafting_table_side) \
	F(crafting_table_side2) \
	F(crafting_table_top) \
	F(destroy_stage_0) \
	F(destroy_stage_1) \
	F(destroy_stage_2) \
	F(destroy_stage_3) \
	F(destroy_stage_4) \
	F(destroy_stage_5) \
	F(destroy_stage_6) \
	F(destroy_stage_7) \
	F(destroy_stage_8) \
	F(destroy_stage_9) \
	F(diamond_block) \
	F(diamond_ore) \
	F(emerald_block) \
	F(emerald_ore) \
	F(enchanting_table_bottom) \
	F(enchanting_table_side) \
	F(enchanting_table_top) \
	F(dirt_podzol_top) \
	F(dirt_podzol_side) \
	F(grass_side_snowed) \
	F(gravel) \
	F(ice) \
	F(lapis_block) \
	F(lapis_ore) \
	F(melon_side) \
	F(melon_top) \
	F(tnt_side) \
	F(tnt_bottom) \
	F(tnt_top) \
	F(mycelium_side) \
	F(mycelium_top) \
	F(nether_brick) \
	F(netherrack) \
	F(obsidian) \
	F(netherreator) \
	F(netherreator_base) \
	F(piston_bottom) \
	F(piston_side) \
	F(piston_top_normal) \
	F(piston_top_sticky) \
	F(prismarine_bricks) \
	F(prismarine_dark) \
	F(prismarine_rough) \
	F(slime) \
	F(snow) \
	F(sponge) \
	F(stonebrick) \
	F(stonebrick_mossy) \
	F(stonebrick_cracked) \
	F(stonebrick_carved) \
	F(dispenser_front_vertical) \
	F(cloud) \
	B

const char* block_texture_name[] = BlockTextures({, FuncStr, });
static const uint block_texture_count = BlockTextures(0, FuncCount, +0);
static_assert(block_texture_count <= 65535, "");
enum class BlockTexture : uint16_t BlockTextures({, FuncList, });

bool is_leaves(BlockTexture a) { return a >= BlockTexture::leaves_acacia && a <= BlockTexture::leaves_spruce; }
bool is_leaves(Block a) { return a >= Block::leaves_acacia && a <= Block::leaves_spruce; }
bool is_log(Block a) { return a >= Block::log_acacia && a <= Block::log_spruce; }
bool is_sand(Block a) { return a == Block::sand || a == Block::red_sand; }
bool is_water(Block a) { return a <= Block::water && a >= Block::water1; }
bool is_water_partial(Block a) { return a >= Block::water1 && a < Block::water; }
bool can_move_through(Block block) { return block <= Block::cloud; }

bool can_see_through_non_water(Block block) { return block == Block::none || is_leaves(block) || block == Block::ice || block == Block::glass_white; }
bool can_see_through(Block block) { return block <= Block::glass_white; }

bool is_blended(BlockTexture a) { return a == BlockTexture::ice || a == BlockTexture::glass_white || a == BlockTexture::water_still; }

static_assert(Block::water1 == (Block)1, "");
bool accepts_water(Block b) { return b < Block::water; }
int water_level(Block b) { return int(b); }

static_assert((uint)BlockTexture::leaves_acacia == 0, "used in shader");
static_assert((uint)BlockTexture::leaves_spruce == 5, "used in shader");
static_assert((uint)BlockTexture::lava_flow == 6, "used in shader");
static_assert((uint)BlockTexture::lava_still == 38, "used in shader");
static_assert((uint)BlockTexture::water_still == 70, "used in shader");
static_assert((uint)BlockTexture::pumpkin_face_off == 134, "used in shader");
static_assert((uint)BlockTexture::pumpkin_face_on == 135, "used in shader");
static_assert((uint)BlockTexture::furnace_front_on == 136, "used in shader");
static_assert((uint)BlockTexture::sea_lantern == 152, "used in shader");
static_assert((uint)BlockTexture::redstone_lamp_off == 157, "used in shader");
static_assert((uint)BlockTexture::redstone_lamp_top_off == 159, "used in shader");
static_assert((uint)BlockTexture::water_flow == 161, "used in shader");

#define FAIL { fprintf(stderr, "Failed at line %d\n", __LINE__); assert(false); exit(1); }

#define SC(A) case Block::A: return BlockTexture::A
#define S1(A, XYZ) case Block::A: return BlockTexture::XYZ
#define S2(A, XY, Z) case Block::A: return (face < 4) ? BlockTexture::XY : BlockTexture::Z;
#define S3(A, XY, ZMIN, ZMAX) case Block::A: return (face < 4) ? BlockTexture::XY : ((face == 4) ? BlockTexture::ZMIN : BlockTexture::ZMAX);

BlockTexture get_block_texture(Block block, int face)
{
	switch (block)
	{
	case Block::none: FAIL;
	SC(cloud);
	SC(leaves_acacia);
	SC(leaves_big_oak);
	SC(leaves_birch);
	SC(leaves_jungle);
	SC(leaves_oak);
	SC(leaves_spruce);
	S2(log_acacia, log_acacia, log_acacia_top);
	S2(log_big_oak, log_big_oak, log_big_oak_top);
	S2(log_birch, log_birch, log_birch_top);
	S2(log_jungle, log_jungle, log_jungle_top);
	S2(log_oak, log_oak, log_oak_top);
	S2(log_spruce, log_spruce, log_spruce_top);
	SC(planks_acacia); \
	SC(planks_big_oak); \
	SC(planks_birch); \
	SC(planks_jungle); \
	SC(planks_oak); \
	SC(planks_spruce); \
	S2(bookshelf, bookshelf, planks_oak);
	SC(bedrock);
	SC(brick);
	S3(cactus, cactus_side, cactus_bottom, cactus_top);
	S3(cauldron, cauldron_side, cauldron_bottom, cauldron_top);
	SC(clay);
	SC(cobblestone);
	SC(dirt);
	S3(daylight_detector, daylight_detector_side, daylight_detector_side, daylight_detector_top);
	SC(glass_white);
	S3(grass, grass_side, dirt, grass_top);
	S2(hay_block, hay_block_side, hay_block_top);
	SC(ice_packed);
	SC(coarse_dirt);
	SC(cobblestone_mossy);
	SC(command_block);
	case Block::crafting_table:
		if (face == 0) return BlockTexture::crafting_table_front;
		if (face == 1) return BlockTexture::crafting_table_side;
		if (face == 2) return BlockTexture::crafting_table_side2;
		if (face == 3) return BlockTexture::crafting_table_front;
		if (face == 4) return BlockTexture::crafting_table_bottom;
		if (face == 5) return BlockTexture::crafting_table_top;
		FAIL;
	SC(coal_ore);
	SC(coal_block);
	SC(iron_block);
	SC(iron_ore);
	SC(gold_block);
	SC(gold_ore);
	SC(diamond_block);
	SC(diamond_ore);
	SC(emerald_block);
	SC(emerald_ore);
	S3(enchanting_table, enchanting_table_side, enchanting_table_bottom, enchanting_table_top);
	S3(dirt_podzol, dirt_podzol_side, dirt, dirt_podzol_top);
	S3(grass_snowed, grass_side_snowed, dirt, snow);
	SC(gravel);
	case Block::furnace:
		if (face == 0) return BlockTexture::furnace_front_on;
		if (face == 1) return BlockTexture::furnace_side;
		if (face == 2) return BlockTexture::furnace_side;
		if (face == 3) return BlockTexture::furnace_side;
		if (face == 4) return BlockTexture::furnace_top;
		if (face == 5) return BlockTexture::furnace_top;
		FAIL;
	case Block::pumpkin:
		if (face == 0) return BlockTexture::pumpkin_face_off;
		if (face == 1) return BlockTexture::pumpkin_side;
		if (face == 2) return BlockTexture::pumpkin_side;
		if (face == 3) return BlockTexture::pumpkin_side;
		if (face == 4) return BlockTexture::pumpkin_top;
		if (face == 5) return BlockTexture::pumpkin_top;
		FAIL;
	S3(quartz_block, quartz_block_side, quartz_block_bottom, quartz_block_top) \
	SC(quartz_ore);
	SC(ice);
	SC(lapis_block);
	SC(lapis_ore);
	SC(lava_flow);
	SC(lava_still);
	S2(melon, melon_side, melon_top);
	S3(tnt, tnt_side, tnt_bottom, tnt_top);
	S3(mycelium, mycelium_side, dirt, mycelium_top);
	SC(nether_brick);
	SC(netherrack);
	SC(obsidian);
	S2(netherreator, netherreator, netherreator_base);
	S3(piston, piston_side, piston_bottom, piston_top_normal);
	S3(piston_sticky, piston_side, piston_bottom, piston_top_sticky);
	SC(prismarine_bricks);
	SC(prismarine_dark);
	SC(prismarine_rough);
	SC(redstone_block);
	SC(redstone_ore);
	S2(redstone_lamp, redstone_lamp_off, redstone_lamp_top_off);
	SC(red_sand);
	S3(red_sandstone, red_sandstone_normal, red_sandstone_bottom, red_sandstone_top);
	S3(red_sandstone_carved, red_sandstone_carved, red_sandstone_bottom, red_sandstone_top);
	SC(sand);
	S3(sandstone, sandstone_normal, sandstone_bottom, sandstone_top);
	S3(sandstone_carved, sandstone_carved, sandstone_bottom, sandstone_top);
	SC(stone);
	SC(stone_andesite);
	SC(stone_diorite);
	SC(stone_granite);
	SC(soul_sand);
	SC(sea_lantern);
	SC(slime);
	SC(snow);
	SC(sponge);
	SC(stonebrick);
	SC(stonebrick_mossy);
	SC(stonebrick_cracked);
	SC(stonebrick_carved);
	S1(water, water_still);
	S1(water14, water_still);
	S1(water13, water_still);
	S1(water12, water_still);
	S1(water11, water_still);
	S1(water10, water_still);
	S1(water9, water_still);
	S1(water8, water_still);
	S1(water7, water_still);
	S1(water6, water_still);
	S1(water5, water_still);
	S1(water4, water_still);
	S1(water3, water_still);
	S1(water2, water_still);
	S1(water1, water_still);
	S3(water_source, furnace_side, dispenser_front_vertical, furnace_top);
	S3(water_drain,  furnace_side, furnace_top, dispenser_front_vertical);
	}
	FAIL;
}
#undef SC
#undef S2
#undef S3

// ============================

static const std::array<glm::ivec3, 6> face_dir = { -ix, ix, -iy, iy, -iz, iz };

bool between(glm::ivec3 a, glm::ivec3 b, glm::ivec3 c)
{
	return a.x <= b.x && b.x <= c.x && a.y <= b.y && b.y <= c.y && a.z <= b.z && b.z <= c.z;
}

namespace Cube
{
	glm::ivec3 corner[8];
	const int faces[6][4] = { { 0, 4, 6, 2 }/*xmin*/, { 1, 3, 7, 5 }/*xmax*/, { 0, 1, 5, 4 }/*ymin*/, { 2, 6, 7, 3 }/*ymax*/, { 0, 2, 3, 1 }/*zmin*/, { 4, 5, 7, 6 }/*zmax*/ };
	glm::i8vec3 lightmap[6/*face*/][8/*vertex*/][4/*four blocks around vertex affecting light of vertex*/];
	glm::i8vec3 lightmap2[6/*face*/][8/*vertex*/][4*3/*three blocks around four blocks around vertex affecting light of vertex*/];

	Initialize
	{
		FOR(i, 8)
		{
			FOR(i, 8) corner[i] = glm::ivec3(i&1, (i>>1)&1, (i>>2)&1);
			FOR(f, 6)
			{
				glm::ivec3 max = corner[i], min = max - ii;
				if (f % 2 == 0) max[f / 2] -= 1;
				if (f % 2 == 1) min[f / 2] += 1;
				int p = 0;
				int q = 0;
				FOR2(x, min.x, max.x) FOR2(y, min.y, max.y) FOR2(z, min.z, max.z)
				{
					lightmap[f][i][p++] = glm::i8vec3(x, y, z);
					FOR(ff, 6) if (ff != (f ^ 1))
					{
						glm::ivec3 b = glm::ivec3(x, y, z) + face_dir[ff];
						if (!between(min, b, max)) lightmap2[f][i][q++] = glm::i8vec3(b);
					}
					assert(q == p * 3);
				}
			}
		}
	}
}

// ============================

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

Heightmap* heightmap = new Heightmap;

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
	if (heightmap->TreeType(pos.x, pos.y))
	{
		int height = heightmap->Height(pos.x, pos.y);
		if (pos.z > height && pos.z < height + 6) return Block(uint(Block::log_acacia) + heightmap->TreeType(pos.x, pos.y) - 1);
	}
	else for(glm::ivec2 i : { glm::ivec2(0, -1), glm::ivec2(0, 1), glm::ivec2(-1, 0), glm::ivec2(1, 0) })
	{
		if (heightmap->TreeType(pos.x + i.x, pos.y + i.y))
		{
			int height = heightmap->Height(pos.x + i.x, pos.y + i.y);
			if (pos.z > height + 2 && pos.z < height + 6) return Block(uint(Block::leaves_acacia) + heightmap->TreeType(pos.x + i.x, pos.y + i.y) - 1);
			break;
		}
	}

	if (pos.z > 100 && pos.z < 200)
	{
		// clouds
		double q = noise(glm::vec3(pos) * 0.01f, 4, 0.5f, 0.5f, false);
		if (q < -0.35) return Block::cloud;
	}
	else if (pos.z <= heightmap->Height(pos.x, pos.y))
	{
		// ground and caves
		double q = noise(glm::vec3(pos) * 0.03f, 4, 0.5f, 0.5f, false);
		static Block ores[6] = { Block::gold_ore, Block::coal_ore, Block::diamond_ore, Block::redstone_ore, Block::emerald_ore, Block::lapis_ore};
		if (q >= 0.6) return ores[uint(pos.x ^ pos.y ^ pos.z) / 3 % 6];
		if (q >= -0.25)
		{
			int d = heightmap->Height(pos.x, pos.y) - pos.z;
			Block b = heightmap->Color(pos.x, pos.y);
			if (d > 3 && is_sand(b)) b = Block::dirt;
			return b;
		}
	}

	return Block::none;
}

bool is_simulated(Block b) { return is_sand(b) || is_water(b); }

static_assert(ChunkSize3 % 4096 == 0, "must be pagesize aligned");

struct MapChunkLight
{
	MapChunkLight(Block* block) : m_block(block) { }
	void set(glm::ivec3 a, Block b) { m_block[index(a)] = b; }
	static int index(glm::ivec3 a) { return (((a.z << ChunkSizeBits) | a.y) << ChunkSizeBits) | a.x; }

private:
	Block* m_block;
};

struct MapChunk
{
	MapChunk() : m_block(nullptr), m_empty(true) { }

	void reset(Block* block, bool* modified)
	{
		m_block = block;
		m_modified = modified;
		update_empty();
	}

	bool valid() const { return m_block; }
	bool empty() const { return m_empty; }

	void update_empty()
	{
		m_empty = true;
		FOR(i, ChunkSize3) if (m_block[i] != Block::none) { m_empty = false; break; }
	}

	Block get(glm::ivec3 a) const { return m_block[index(a)]; }
	void set(glm::ivec3 a, Block b) { m_empty = false; *m_modified = true; m_block[index(a)] = b; }
	static int index(glm::ivec3 a) { return (((a.z << ChunkSizeBits) | a.y) << ChunkSizeBits) | a.x; }

private:
	bool m_empty;
	Block* m_block;
	bool* m_modified;
};

void generate_chunk(Block* block, glm::ivec3 cpos)
{
	heightmap->Populate(cpos.x, cpos.y);
	FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
	{
		glm::ivec3 v(x, y, z);
		block[MapChunk::index(v)] = generate_block(cpos * ChunkSize + v);
	}
}

// =======================

static const uint BlockCubeFileSize = (1 << (3 * (ChunkSizeBits + SuperChunkSizeBits))) * sizeof(Block);
typedef BitCube<(1 << SuperChunkSizeBits)> BitCubeExplored;
static const uint DataSize = BlockCubeFileSize + sizeof(BitCubeExplored);

struct SuperChunk
{
	glm::ivec3 scpos;
	int refs;
	bool modified;
	uint8_t* data;

	bool load();
	bool save();

	BitCubeExplored& explored() { return *reinterpret_cast<BitCubeExplored*>(data + BlockCubeFileSize); }
	Block* chunk(glm::ivec3 cpos);
};

Block* SuperChunk::chunk(glm::ivec3 cpos)
{
	int i = (((cpos.x << ChunkSizeBits) | cpos.y) << ChunkSizeBits) | cpos.z;
	return reinterpret_cast<Block*>(data) + i * ChunkSize3;
}

bool SuperChunk::load()
{
	modified = false;
	assert(!data);
	data = (uint8_t*)malloc(DataSize);
	CHECK(data);

	char* filename = nullptr;
	CHECK(0 < asprintf(&filename, "world.%+d%+d%+d.sc", scpos.x, scpos.y, scpos.z));
	Auto(free(filename));

	FILE* file = fopen(filename, "r");
	if (!file && errno == ENOENT)
	{
		explored().clear();
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
	CHECK(0 < asprintf(&filename, "world.%+d%+d%+d.sc", scpos.x, scpos.y, scpos.z));
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

struct AutoVecLock;

struct AutoVecLockManager
{
	struct Bucket
	{
		std::mutex mutex;
		std::condition_variable cond;
		AutoVecLock* head;
		Bucket() : head(nullptr) { }
	};

	Bucket& get_bucket(glm::ivec3 v) { return m_bucket[uint(v.x ^ v.y ^ v.z) % 64]; }
private:
	Bucket m_bucket[64];
};

struct AutoVecLock
{
	AutoVecLock(glm::ivec3 vec, AutoVecLockManager& mgr) : m_vec(vec), m_bucket(mgr.get_bucket(vec))
	{
		std::unique_lock<std::mutex> lock(m_bucket.mutex);
		while (is_locked())
		{
			m_bucket.cond.wait(lock);
		}
		m_next = m_bucket.head;
		m_bucket.head = this;
	}

	~AutoVecLock()
	{
		AutoLock(m_bucket.mutex);
		AutoVecLock** ptr = &m_bucket.head;
		while (*ptr != this) ptr = &(*ptr)->m_next;
		*ptr = m_next;
		m_bucket.cond.notify_all();
	}

private:
	bool is_locked()
	{
		for (AutoVecLock* i = m_bucket.head; i; i = i->m_next)
		{
			if (i->m_vec == m_vec) return true;
		}
		return false;
	}

private:
	glm::ivec3 m_vec;
	AutoVecLock* m_next;
	AutoVecLockManager::Bucket& m_bucket;
};

struct Hasher
{
	size_t operator()(const glm::ivec3& v) const { return v.x + 11 * v.y + 23 * v.z; }
};

struct SuperChunkManager
{
	bool acquire_chunk(glm::ivec3 cpos, bool generate, MapChunk& mc)
	{
		AutoLock(m_lock);
		glm::ivec3 scpos = cpos >> SuperChunkSizeBits;
		SuperChunk* sc = m_map[scpos];
		if (sc == nullptr)
		{
			sc = new SuperChunk;
			sc->scpos = scpos;
			sc->refs = 0;
			if (!sc->load()) exit(1);
			m_map[scpos] = sc;
		}
		sc->refs += 1;

		Block* blocks = sc->chunk(cpos & SuperChunkSizeMask);
		m_lock.unlock();
		AutoVecLock _(cpos, chunk_locks);
		m_lock.lock();

		if (!sc->explored()[cpos & SuperChunkSizeMask])
		{
			if (!generate) return false;
			m_lock.unlock();
			generate_chunk(blocks, cpos);
			m_lock.lock();
			sc->explored().set(cpos & SuperChunkSizeMask);
		}

		mc.reset(blocks, &sc->modified);
		return true;
	}

	void release_chunk(glm::ivec3 cpos)
	{
		AutoLock(m_lock);
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
		AutoLock(m_lock);
		for (auto it : m_map)
		{
			if (!it.second->save())
			{
				glm::ivec3 a = it.second->scpos;
				fprintf(stderr, "ERROR: Failed to save super chunk [%d %d %d]\n", a.x, a.y, a.z);
			}
		}
	}

	AutoVecLockManager chunk_locks;
private:
	std::mutex m_lock;
	std::unordered_map<glm::ivec3, SuperChunk*, Hasher> m_map;
};

SuperChunkManager g_scm;

// ============================

struct Quad
{
	glm::u8vec3 pos[4]; // TODO: replace with x y w h
	BlockTexture texture; // highest bit: isUnderwater
	uint16_t light; // 4 bits per vertex
	uint16_t plane; // hi byte: plane normal (face), lo byte: plane offset
};

struct WQuad
{
	glm::u16vec3 pos[4]; // TODO: replace with x y w h
	BlockTexture texture; // highest bit: isUnderwater
	uint16_t light; // 4 bits per vertex
	uint16_t plane; // hi byte: plane normal (face), lo byte: plane offset
};

float distance(const Quad& q, glm::vec3 e)
{
	glm::vec3 a(q.pos[0]), c(q.pos[2]);
	return glm::distance2((a + c) * 0.5f, e);
}

bool operator<(const Quad& a, const Quad& b)
{
	if (a.texture != b.texture)
	{
		int ga = is_blended(a.texture) ? 1 : 0;
		int gb = is_blended(b.texture) ? 1 : 0;
		if (ga != gb) return ga < gb;
		return a.texture < b.texture;
	}
	if (a.plane != b.plane) return a.plane < b.plane;
	return a.light < b.light;
}

const float BlockRadius = sqrtf(3) / 2;

struct VisibleChunks
{
	VisibleChunks() { set.clear(); }

	void add(glm::ivec3 v)
	{
		if (set.xset(v & MapSizeMask)) array.push_back(v);
	}

	void cleanup(glm::ivec3 center, glm::vec3 direction)
	{
		int w = 0;
		for (glm::ivec3 c : array)
		{
			if (glm::dot(direction, glm::vec3(c - center)) < 0 || glm::distance2(c, center) > sqr(RenderDistance))
			{
				assert(set[c & MapSizeMask]);
				set.clear(c & MapSizeMask);
			}
			else
			{
				array[w++] = c;
			}
		}
		array.resize(w);
	}

	void sort(glm::vec3 camera)
	{
		camera -= ii * ChunkSize / 2;
		camera /= ChunkSize;
		auto cmp = [camera](glm::ivec3 a, glm::ivec3 b) { return glm::distance2(glm::vec3(a), camera) > glm::distance2(glm::vec3(b), camera); };
		std::sort(array.begin(), array.end(), cmp);
	}

	glm::ivec3* begin() { return &array[0]; }
	glm::ivec3* end() { return begin() + array.size(); }

private:
	BitCube<MapSize> set;
	std::vector<glm::ivec3> array;
};

struct Chunk;

struct CachedChunk
{
	CachedChunk() : initialized(false) { }
	~CachedChunk() { if (mc.valid()) g_scm.release_chunk(release_cpos); }
	void init(glm::ivec3 cpos);
	Block operator[](glm::ivec3 pos);

	bool initialized;
	glm::ivec3 release_cpos;
	MapChunk mc;
};

bool enable_f4 = true;
bool enable_f5 = true;
bool enable_f6 = true;

uint8_t Light(const Quad& q, int i)
{
	int s = (i % 4) * 2;
	int a = (q.light >> s) & 3;
	return (a + 1) * 64 - 1;
}

uint8_t Light2(const Quad& q, int i)
{
	int s = (i % 4) * 4;
	int a = (q.light >> s) & 15;
	return (a + 1) * 16 - 1;
}

struct BlockRenderer
{
	std::vector<Quad>* m_quads;
	std::vector<Quad> m_quadsp;
	glm::ivec3 m_pos;
	Block m_block;

	std::vector<Quad> m_xy, m_yx;

	// V1
	glm::ivec3 m_cxpos;
	MapChunk* m_mc;
	CachedChunk* m_cc;

	// V2
	// Idea: if mapchunks are shifted 8 blocks on each axis then each render chunk would only depend on 2x2x2 mapchunks (8 instead of 27)
	// Problem: how to parallelize loading when player moves?
	// - worker threads only load superchunks and generate chunks (if mapchunk is not ready by buffer time, just use pointer to zero memory or full with some special block types)
	// - renderchunks are generated by main thread on demand (if chunk is going to be rendered and is dirty)
	// How does it affect the current and replacement raytracers?/
	//MapChunkLight m_mcl[3][3][3];

	/*Block get_v2(glm::ivec3 rel)
	{
		glm::ivec3 a = m_fpos + rel;
		glm::ivec3 b = (a - corner) >> ChunkSizeBits;
		return m_mcl[b.x][b.y][b.z].get(a & ChunkSizeMask);
	}*/

	Block get(glm::ivec3 rel)
	{
		glm::ivec3 a = m_pos + rel;
		glm::ivec3 b = a >> ChunkSizeBits;
		if (b.x == 0 && b.y == 0 && b.z == 0) return m_mc->get(a & ChunkSizeMask);
		int i = b.x*9 + b.y*3 + b.z + 13; // ((b.x+1)*3 + b.y+1)*3 + b.z+1
		return m_cc[i][a + m_cxpos];
	}

	uint8_t face_light(int face)
	{
		const int* f = Cube::faces[face];
		int q = 0;
		FOR(i, 4)
		{
			glm::i8vec3* map = Cube::lightmap[face][f[i]];
			int s = 0;
			FOR(j, 4) if (get(glm::ivec3(map[j])) == Block::none) s += 1;
			q |= (s - 1) << (i * 2);
		}
		return q;
	}

	uint16_t face_light2(int face)
	{
		const int* f = Cube::faces[face];
		int q = 0;
		FOR(i, 4)
		{
			glm::i8vec3* map = Cube::lightmap[face][f[i]];
			glm::i8vec3* map2 = Cube::lightmap2[face][f[i]];
			int s = 0;
			FOR(j, 4)
			{
				Block b = get(glm::ivec3(map[j]));
				if (can_see_through(b))
				{
					s += (b == Block::none) ? 2 : 1;
					FOR(k, 3)
					{
						Block b2 = get(glm::ivec3(map2[j*3+k]));
						if (can_see_through(b2)) s += (b2 == Block::none) ? 2 : 1;
					}
				}
			}
			s = s / 2;
			q |= (s - 1) << (i * 4);
		}
		return q;
	}

	void draw_quad(int face, bool reverse, bool underwater_overlay);
	void draw_quad(int face, int zmin, int zmax, bool reverse, bool underwater_overlay);

	Block get(int face) { return get(face_dir[face]); }

	template<bool side>
	void draw_non_water_face(int face)
	{
		Block q = get(face);
		if (side && is_water(q))
		{
			if (q == Block::water)
			{
				draw_quad(face, false, true);
			}
			else if (q != m_block)
			{
				draw_quad(face, 0, water_level(q), false, true);
				draw_quad(face, water_level(q), 15, false, false);
			}
			return;
		}
		if (!side && q == Block::water)
		{
			draw_quad(face, false, true);
			return;
		}
		if (can_see_through(q) && q != m_block)
		{
			draw_quad(face, false, false);
		}
	}

	void draw_water_side(int face, int w)
	{
		Block q = get(face);
		if (is_water_partial(q))
		{
			if (w > water_level(q))
			{
				draw_quad(face, water_level(q), w, false, false);
				draw_quad(face, water_level(q), w, true, false);
			}
		}
		else if (can_see_through_non_water(q))
		{
			draw_quad(face, 0, w, false, false);
			draw_quad(face, 0, w, true, false);
		}
	}

	void draw_water_bottom()
	{
		Block q = get(/*m_pos.z != CMin, 4,*/ -iz);
		if (q != Block::water && can_see_through(q)) draw_quad(4, false, false);
		if (q == Block::none) draw_quad(4, true, false);
	}

	void draw_water_top(int w)
	{
		if (m_block == Block::water)
		{
			Block q = get(/*m_pos.z != CMax, 5,*/ iz);
			if (can_see_through_non_water(q)) draw_quad(5, false, false);
			if (q == Block::none) draw_quad(5, true, false);
		}
		else
		{
			draw_quad(5, w, w, false, false);
			draw_quad(5, w, w, true, false);
		}
	}

	void generate_quads(glm::ivec3 cpos, MapChunk& mc, bool merge, std::vector<Quad>& out, int& blended_quads)
	{
		out.clear();
		m_quadsp.clear();
		m_quads = merge ? &m_quadsp : &out;

		CachedChunk cc[27];
		m_cc = cc;
		m_mc = &mc;
		m_cxpos = cpos << ChunkSizeBits;
		FOR(z, ChunkSize) FOR(y, ChunkSize) FOR(x, ChunkSize)
		{
			glm::ivec3 p(x, y, z);
			Block block = mc.get(p);
			if (block == Block::none) continue;
			m_pos = p;
			m_block = block;

			if (is_water(block))
			{
				int w = uint(block) - uint(Block::water1) + 1;
				draw_water_side(0, w);
				draw_water_side(1, w);
				draw_water_side(2, w);
				draw_water_side(3, w);
				draw_water_bottom();
				draw_water_top(w);
			}
			else
			{
				draw_non_water_face<true>(0);
				draw_non_water_face<true>(1);
				draw_non_water_face<true>(2);
				draw_non_water_face<true>(3);
				draw_non_water_face<false>(4);
				draw_non_water_face<false>(5);
			}
		}
		if (merge) merge_quads(out);
		if (!merge) std::partition(out.begin(), out.end(), [](const Quad& q) { return !is_blended(q.texture); });
		blended_quads = 0;
		while (blended_quads < out.size() && is_blended(out[out.size() - 1 - blended_quads].texture)) blended_quads += 1;
	}

	static bool combine(Quad& a, Quad b, int X, int Y)
	{
		if (a.pos[0][X] == b.pos[0][X] && a.pos[2][X] == b.pos[2][X] && a.pos[2][Y] == b.pos[0][Y])
		{
			a.pos[2] = b.pos[2];
			if (a.pos[1][Y] == b.pos[0][Y]) a.pos[1] = b.pos[1];
			if (a.pos[3][Y] == b.pos[0][Y]) a.pos[3] = b.pos[3];
			return true;
		}
		return false;
	}

	static void merge_axis(std::vector<Quad>& quads, int X, int Y)
	{
		std::sort(quads.begin(), quads.end(), [X, Y](Quad a, Quad b) { return a.pos[0][X] < b.pos[0][X] || (a.pos[0][X] == b.pos[0][X] && a.pos[0][Y] < b.pos[0][Y]); });
		int w = 0;
		for (Quad q : quads)
		{
			if (w == 0 || !combine(quads[w-1], q, X, Y)) quads[w++] = q;
		}
		quads.resize(w);
	}

	void merge_quads(std::vector<Quad>& out)
	{
		std::sort(m_quadsp.begin(), m_quadsp.end());
		auto a = m_quadsp.begin();
		while (a != m_quadsp.end())
		{
			auto b = a + 1;
			while (b != m_quadsp.end() && a->texture == b->texture && a->plane == b->plane && a->light == b->light) b += 1;

			if (is_leaves(a->texture))
			{
				while (a < b) out.push_back(*a++);
				continue;
			}

			m_xy.clear();
			m_yx.clear();
			while (a < b)
			{
				m_xy.push_back(*a);
				m_yx.push_back(*a);
				a += 1;
			}

			int axis = m_xy[0].plane >> 9;
			int x = (axis == 0) ? 1 : 0;
			int y = (axis == 2) ? 1 : 2;

			merge_axis(m_xy, x, y);
			merge_axis(m_xy, y, x);
			merge_axis(m_yx, y, x);
			merge_axis(m_yx, x, y);

			if (m_xy.size() > m_yx.size()) std::swap(m_xy, m_yx);
			for (Quad& q : m_xy)
			{
				out.push_back(q);
			}
		}
	}
};

typedef uint64_t CompressedIVec3;

const CompressedIVec3 NullChunk = 0xFFFFFFFF;

uint64_t compress(int v, int bits)
{
	uint64_t c = v;
	return c & ((1lu << bits) - 1);
}

int decompress(uint64_t c, int bits)
{
	int64_t q = c << (64 - bits);
	return q >> (64 - bits);
}

CompressedIVec3 compress_ivec3(glm::ivec3 v)
{
	uint64_t c = 0;
	c |= compress(v.x, 21);
	c <<= 21;
	c |= compress(v.y, 21);
	c <<= 21;
	c |= compress(v.z, 21);
	return c;
}

glm::ivec3 decompress_ivec3(CompressedIVec3 c)
{
	glm::ivec3 v;
	v.z = decompress(c, 21);
	c >>= 21;
	v.y = decompress(c, 21);
	c >>= 21;
	v.x = decompress(c, 21);
	return v;
}

// ===============

class MyConsole : public Console
{
public:
	virtual void Execute(const char* command, int length) override;
};

MyConsole console;

struct Player
{
	// Persisted
	glm::vec3 position;
	float yaw, pitch;
	glm::vec3 velocity;
	bool creative_mode;
	Block palette_block;
	// TODO: active tool
	// TODO: inventory items
	// TODO: health
	// TODO: armor

	// Non-persisted
	glm::ivec3 cpos;
	std::atomic<CompressedIVec3> atomic_cpos;
	glm::mat4 orientation;

	bool digging_on;
	Timestamp digging_start;
	glm::ivec3 digging_block;

	bool load();
	bool save();

	void start_digging(glm::ivec3 cube);
	void turn(float dx, float dy);
};

bool g_collision = true;

Player g_player;
double scroll_y = 0, scroll_dy = 0;

void Player::turn(float dx, float dy)
{
	yaw += dx;
	pitch += dy;
	if (pitch > M_PI / 2 * 0.999) pitch = M_PI / 2 * 0.999;
	if (pitch < -M_PI / 2 * 0.999) pitch = -M_PI / 2 * 0.999;
	orientation = glm::rotate(glm::rotate(glm::mat4(), -yaw, glm::vec3(0, 0, 1)), -pitch, glm::vec3(1, 0, 0));
}

void Player::start_digging(glm::ivec3 cube)
{
	digging_on = true;
	digging_block = cube;
	digging_start = Timestamp();
}

json_t* json_vec3(glm::vec3 a)
{
	json_t* v = json_array();
	json_array_append(v, json_real(a.x));
	json_array_append(v, json_real(a.y));
	json_array_append(v, json_real(a.z));
	return v;
}

bool Player::save()
{
	json_t* doc = json_object();
	Auto(json_decref(doc));
	json_object_set(doc, "position", json_vec3(position));
	json_object_set(doc, "yaw", json_real(yaw));
	json_object_set(doc, "pitch", json_real(pitch));
	json_object_set(doc, "velocity", json_vec3(velocity));
	json_object_set(doc, "creative_mode", json_boolean(creative_mode));
	json_object_set(doc, "palette_block", json_integer((int)palette_block - (int)Block::water));
	json_object_set(doc, "console", console.save());
	CHECK(0 == json_dump_file(doc, "player.json", JSON_INDENT(4) | JSON_PRESERVE_ORDER));
	return true;
}

bool json_read(json_t* a, float& out)
{
	CHECK(json_is_real(a));
	out = json_real_value(a);
	return true;
}

bool json_read(json_t* a, glm::vec3& out)
{
	CHECK(json_is_array(a));
	CHECK(json_array_size(a) == 3);
	CHECK(json_read(json_array_get(a, 0), out.x));
	CHECK(json_read(json_array_get(a, 1), out.y));
	CHECK(json_read(json_array_get(a, 2), out.z));
	return true;
}

bool json_read(json_t* a, int64_t& out)
{
	CHECK(json_is_integer(a));
	out = json_integer_value(a);
	return true;
}

bool json_read(json_t* a, bool& out)
{
	CHECK(json_is_boolean(a));
	out = json_boolean_value(a);
	return true;
}

bool Player::load()
{
	// load defaults
	position = glm::vec3(0, 0, 20);
	yaw = 0;
	pitch = 0;
	velocity = glm::vec3(0, 0, 0);
	creative_mode = true;
	palette_block = Block::water;

	FILE* file = fopen("player.json", "r");
	CHECK(file || errno == ENOENT);
	if (file)
	{
		Auto(fclose(file));

		json_error_t error;
		json_t* doc = json_loadf(file, 0, &error);
		if (!doc)
		{
			fprintf(stderr, "JSON error: line=%d column=%d position=%d source='%s' text='%s'\n", error.line, error.column, error.position, error.source, error.text);
			return false;
		}
		assert(doc->refcount == 1);
		Auto(json_decref(doc));

		CHECK(json_is_object(doc));
		#define read(K, V) { json_t* value = json_object_get(doc, K); CHECK(value); CHECK(json_read(value, V)); }

		read("position", position);
		read("yaw", yaw);
		read("pitch", pitch);
		read("velocity", velocity);
		read("creative_mode", creative_mode);
		CHECK(console.load(json_object_get(doc, "console")));

		int64_t pb;
		read("palette_block", pb);
		CHECK(pb >= 0 && pb < block_count - (int)Block::water);
		palette_block = (Block)(pb + (int)Block::water);

		#undef read
	}

	scroll_y = (uint)palette_block - (uint)Block::water;
	orientation = glm::rotate(glm::rotate(glm::mat4(), -yaw, glm::vec3(0, 0, 1)), -pitch, glm::vec3(1, 0, 0));
	cpos = glm::ivec3(glm::floor(position)) >> ChunkSizeBits;
	atomic_cpos = compress_ivec3(cpos);
	digging_on = false;
	return true;
}

// ===============

struct Mesh
{
	bool load(const char* filename);
	void bounding_box(glm::vec3& min, glm::vec3& max) const;

	struct Face
	{
		uint verts[3];
	};

	std::vector<glm::vec3> vertices;
	std::vector<Face> faces;
};

bool Mesh::load(const char* filename)
{
	FILE* f = fopen(filename, "r");
	PlyFile* pf = read_ply(f);

	PlyProperty vert_prop_x = { const_cast<char*>("x"), Float32, Float32, offsetof(glm::vec3,x), 0, 0, 0, 0 };
	PlyProperty vert_prop_y = { const_cast<char*>("y"), Float32, Float32, offsetof(glm::vec3,y), 0, 0, 0, 0 };
	PlyProperty vert_prop_z = { const_cast<char*>("z"), Float32, Float32, offsetof(glm::vec3,z), 0, 0, 0, 0 };
	PlyProperty face_prop = { const_cast<char*>("vertex_indices"), Int32, Int32, offsetof(Face, verts), 3/*LIST_FIXED*/, Uint8, Uint8, 3/*num verts*/ };

	CHECK(pf->num_elem_types == 2);

	// Read vertex list
	int count;
	char* elem_name = setup_element_read_ply(pf, 0, &count);
	CHECK(strcmp(elem_name, "vertex") == 0);
	CHECK(count >= 0);
	CHECK(pf->elems[0]->nprops == 3);
	CHECK(strcmp(pf->elems[0]->props[0]->name, "x") == 0);
	CHECK(strcmp(pf->elems[0]->props[1]->name, "y") == 0);
	CHECK(strcmp(pf->elems[0]->props[2]->name, "z") == 0);
	CHECK(pf->elems[0]->props[0]->is_list == 0);
	CHECK(pf->elems[0]->props[1]->is_list == 0);
	CHECK(pf->elems[0]->props[2]->is_list == 0);
	setup_property_ply(pf, &vert_prop_x);
	setup_property_ply(pf, &vert_prop_y);
	setup_property_ply(pf, &vert_prop_z);
	vertices.resize(count);
	for (int i = 0; i < count; i++)
	{
		get_element_ply(pf, &vertices[i]);
		std::swap(vertices[i].y, vertices[i].z);
	}

	// Read face list
	elem_name = setup_element_read_ply(pf, 1, &count);
	CHECK(strcmp(elem_name, "face") == 0);
	CHECK(count >= 0);
	CHECK(pf->elems[1]->nprops == 1);
	CHECK(strcmp(pf->elems[1]->props[0]->name, "vertex_indices") == 0);
	CHECK(pf->elems[1]->props[0]->is_list == 1);
	setup_property_ply(pf, &face_prop);
	faces.resize(count);
	for (int i = 0; i < count; i++)
	{
		Face& face = faces[i];
		get_element_ply(pf, &face);
		CHECK(pf->error == 0);
	}

	free_ply(pf);
	fclose(f);
	return true;
}

void Mesh::bounding_box(glm::vec3& min, glm::vec3& max) const
{
	min = max = vertices[0];
	for (uint i = 1; i < vertices.size(); i++)
	{
		glm::vec3 v = vertices[i];
		if (v.x < min.x) min.x = v.x;
		if (v.y < min.y) min.y = v.y;
		if (v.z < min.z) min.z = v.z;
		if (v.x > max.x) max.x = v.x;
		if (v.y > max.y) max.y = v.y;
		if (v.z > max.z) max.z = v.z;
	}
}

// ===============

struct Chunk
{
	Chunk() : m_cpos(NullChunk) { }

	Block get(glm::ivec3 a) const { return m_mc.get(a); }
	void set(glm::ivec3 a, Block b) { m_mc.set(a, b); }
	bool empty() const { return m_mc.empty(); }
	void update_empty() { m_mc.update_empty(); }

	void sort(glm::vec3 camera)
	{
		if (m_blended_quads > 0)
		{
			camera -= get_cpos() << ChunkSizeBits;
			auto cmp = [camera](const Quad& a, const Quad& b) { return distance(a, camera) > distance(b, camera); };
			std::sort(m_quads.end() - m_blended_quads, m_quads.end(), cmp);
		}
	}

	void remesh(BlockRenderer& renderer)
	{
		AutoLock(m_buffer_mutex);
		renderer.generate_quads(get_cpos(), m_mc, !m_active, m_quads, m_blended_quads);
		m_remesh = false;
	}

	int render()
	{
		glBufferData(GL_ARRAY_BUFFER, sizeof(Quad) * m_quads.size(), &m_quads[0], GL_STREAM_DRAW);
		glDrawArrays(GL_POINTS, 0, m_quads.size());
		return m_quads.size();
	}

	void init(glm::ivec3 cpos, BlockRenderer& renderer)
	{
		assert(unloaded());

		assert(!m_mc.valid());
		// TODO if (m_buffer) g_scm.release_chunk();
		g_scm.acquire_chunk(cpos, true, m_mc);
		m_cpos = compress_ivec3(cpos);
		remesh(renderer);
		m_renderable = true;
		m_active = true;
	}

	void unload() { m_cpos = NullChunk; m_renderable = false; }
	bool unloaded() { return m_cpos == NullChunk; }
	bool renderable() { return m_renderable; }

	glm::ivec3 get_cpos() { return decompress_ivec3(m_cpos); }

	bool m_active;
	bool m_remesh;
	friend class Chunks;
private:
	MapChunk m_mc;
	std::atomic<CompressedIVec3> m_cpos;
	std::atomic<bool> m_renderable;

	std::mutex m_buffer_mutex; // Protects m_vertices and m_render_size
	std::vector<Quad> m_quads;
	int m_blended_quads;
};

class Chunks
{
public:
	static const int Threads = 7;

	Chunks(): m_map(new Chunk[MapSize * MapSize * MapSize]) { }

	void fork_threads()
	{
		m_running.store(true, std::memory_order_relaxed);
		FOR(i, Threads)
		{
			std::thread t(loader_thread, this, i);
			std::swap(t, m_worker[i]);
		}
	}

	void join_threads()
	{
		m_running.store(false, std::memory_order_relaxed);
		FOR(i, Threads) m_worker[i].join();
	}

	Block get_block(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		assert(chunk.get_cpos() == (pos >> ChunkSizeBits));
		return chunk.get(pos & ChunkSizeMask);
	}

	Block get_block(glm::ivec3 pos, Block def)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		if (chunk.get_cpos() != (pos >> ChunkSizeBits)) return def;
		return chunk.get(pos & ChunkSizeMask);
	}

	bool selectable_block(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		return chunk.get_cpos() == (pos >> ChunkSizeBits) && chunk.get(pos & ChunkSizeMask) != Block::none;
	}

	bool can_move_through(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		return chunk.get_cpos() == (pos >> ChunkSizeBits) && ::can_move_through(chunk.get(pos & ChunkSizeMask));
	}

	void reset(glm::ivec3 cpos, BlockRenderer& renderer)
	{
		Chunk& c = get(cpos);
		if (c.get_cpos() != cpos) return;
		c.m_renderable = false;
		c.remesh(renderer);
		c.m_renderable = true;
	}

	Chunk& get(glm::ivec3 cpos)
	{
		cpos &= MapSizeMask;
		return m_map[((cpos.x * MapSize) + cpos.y) * MapSize + cpos.z];
	}

	Chunk* get_opt(glm::ivec3 cpos)
	{
		Chunk& chunk = get(cpos);
		return (chunk.get_cpos() == cpos) ? &chunk : nullptr;
	}

private:
	static void loader_thread(Chunks* chunks, int k)
	{
		glm::ivec3 last_cpos(0x80000000, 0, 0);
		BlockRenderer renderer;
		while (chunks->m_running.load(std::memory_order_relaxed))
		{
			glm::ivec3 cpos = decompress_ivec3(g_player.atomic_cpos);
			if (cpos == last_cpos)
			{
				usleep(100000);
				continue;
			}

			int q = 0;
			for (int i = 0; i < render_sphere.size() && chunks->m_running.load(std::memory_order_relaxed); i++)
			{
				glm::ivec3 c = cpos + glm::ivec3(render_sphere[i]);
				Chunk& chunk = chunks->get(c);
				if (owner(chunk) == k && chunk.unloaded())
				{
					chunk.init(c, renderer);
					q += 1;
					if (q >= 1000) { last_cpos.x = 0x80000000; break; }
				}
			}
			last_cpos = cpos;
		}
	}

	static int owner(Chunk& chunk) { return reinterpret_cast<size_t>(&chunk) % Threads; }

private:
	Chunk* m_map; // Huge array in memory!
	std::thread m_worker[Threads];
	std::atomic<bool> m_running;
};

Chunks g_chunks;

void CachedChunk::init(glm::ivec3 cpos)
{
	initialized = true;
	if (g_scm.acquire_chunk(cpos, false, mc)) release_cpos = cpos;
}

Block CachedChunk::operator[](glm::ivec3 pos)
{
	if (!initialized) init(pos >> ChunkSizeBits);
	if (mc.valid()) return mc.get(pos & ChunkSizeMask);
	return generate_block(pos);
}

// ======================

// <scale> is size (in blocks) of the largest extent of mesh.
void rasterize_mesh(const Mesh& mesh, glm::ivec3 base, float scale, Block block)
{
	glm::vec3 min, max;
	mesh.bounding_box(/*out*/min, /*out*/max);
	glm::vec3 mesh_size = max - min;
	float extent = glm::max(mesh_size.x, mesh_size.y, mesh_size.z);
	float mesh_block = extent / scale; // size of block in model space

	glm::ivec3 bmax = base + glm::ivec3(glm::floor(mesh_size / mesh_block));

	FOR(i, mesh.faces.size())
	{
		const Mesh::Face& face = mesh.faces[i];
		glm::vec3 va = mesh.vertices[face.verts[0]];
		glm::vec3 vb = mesh.vertices[face.verts[1]];
		glm::vec3 vc = mesh.vertices[face.verts[2]];

		FOR(p, 3)
		{
			glm::vec3 v2 = glm::mix(va, vb, p * 0.5f);
			FOR(q, 3)
			{
				glm::vec3 v3 = glm::mix(v2, vc, q * 0.5f);

				glm::ivec3 pos = base + glm::ivec3(glm::floor((v3 - min) / mesh_block));
				glm::ivec3 cpos = pos >> ChunkSizeBits;
				Chunk& chunk = g_chunks.get(cpos);
				chunk.set(pos & ChunkSizeMask, block);
			}
		}
	}

	glm::ivec3 cmin = (base - ii) >> ChunkSizeBits;
	glm::ivec3 cmax = (bmax + ii) >> ChunkSizeBits;
	FOR2(z, cmin.z, cmax.z) FOR2(y, cmin.y, cmax.y) FOR2(x, cmin.x, cmax.x)
	{
		glm::ivec3 cpos(x, y, z);
		Chunk& chunk = g_chunks.get(cpos);
		assert(chunk.get_cpos() == cpos);
		chunk.m_remesh = true;
		// TODO activate chunk for simulation
	}
}

// ======================

struct BoundaryBlocks : public std::vector<glm::ivec3>
{
	BoundaryBlocks()
	{
		const int M = ChunkSize - 1;
		FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
		{
			if (x == 0 || x == M || y == 0 || y == M || z == 0 || z == M) push_back(glm::ivec3(x, y, z));
		}
	}
} boundary_blocks;

struct Directions : public std::vector<glm::ivec3>
{
	enum { Bits = 9 };
	Directions()
	{
		const int M = 1 << Bits, N = M - 1;
		FOR2(x, 1, M) FOR2(y, 1, M) FOR2(z, 1, M)
		{
			int d2 = x*x + y*y + z*z;
			if (N*N < d2 && d2 <= M*M) for (int i : {-x, x}) for (int j : {-y, y})
			{
				push_back(glm::ivec3(i, j, z));
				push_back(glm::ivec3(i, j, -z));
			}
		}
		for (int m : {M, -M})
		{
			push_back(glm::ivec3(m, 0, 0));
			push_back(glm::ivec3(0, m, 0));
			push_back(glm::ivec3(0, 0, m));
		}
		FOR(i, size()) std::swap(operator[](std::rand() % size()), operator[](i));
	}
} directions;

int ray_it = 0;
int rays_remaining = 0;

VisibleChunks visible_chunks;

const int E = Directions::Bits + 1;

void raytrace(Chunk* chunk, glm::ivec3 photon, glm::ivec3 dir)
{
	const int MaxDist2 = sqr(RenderDistance * ChunkSize);
	const int B = ChunkSizeBits + E;
	glm::ivec3 svoxel = photon >> E, voxel = svoxel;
	while (true)
	{
		do photon += dir; while ((photon >> E) == voxel);
		glm::ivec3 cvoxel = photon >> B;
		if (chunk->get_cpos() != cvoxel) while (true)
		{
			chunk = &g_chunks.get(cvoxel);
			if (chunk->get_cpos() != cvoxel) return; // TODO: chunk not loaded yet. Render it as big fog cube. Need to retrace it again once it is loaded.
			if (!chunk->empty()) break;
			do photon += dir; while ((photon >> B) == cvoxel);
			voxel = photon >> E;
			if (glm::distance2(voxel, svoxel) > MaxDist2) return;
			cvoxel = photon >> B;
		}
		voxel = photon >> E;
		if (glm::distance2(voxel, svoxel) > MaxDist2) break;
		Block block = chunk->get(voxel & ChunkSizeMask);
		if (block == Block::none) continue;
		visible_chunks.add(cvoxel);
		if (!can_see_through(block)) break;
	}
}

// which chunks must be rendered from the center chunk?
void update_render_list(glm::ivec3 cpos, Frustum& frustum)
{
	if (rays_remaining == 0) return;

	Chunk* chunk = &g_chunks.get(cpos);
	assert(chunk->get_cpos() == cpos);

	float k = 1 << E;
	glm::ivec3 origin = glm::ivec3(glm::floor(g_player.position * k));

	int64_t budget = 40 / Timestamp::milisec_per_tick;
	Timestamp ta;

	int q = 0;
	while (rays_remaining > 0)
	{
		glm::ivec3 dir = directions[ray_it++];
		if (ray_it == directions.size()) ray_it = 0;
		rays_remaining -= 1;
		if (frustum.contains_point(g_player.position + glm::vec3(dir))) raytrace(chunk, origin, dir);
		if ((q++ % 2048) == 0 && ta.elapsed() > budget) break;
	}
	visible_chunks.sort(g_player.position);
}

namespace stats
{
	int quad_count = 0;
	int chunk_count = 0;

	float frame_time_ms = 0;
	float render_time_ms = 0;
	float model_time_ms = 0;
	float raytrace_time_ms = 0;

	float collide_time_ms = 0;
	float select_time_ms = 0;
	float simulate_time_ms = 0;
}

// Model

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
bool selection = false;
bool wireframe = false;
bool show_counters = true;

Timestamp ts_palette;
bool show_palette = false;

void activate_block(glm::ivec3 pos)
{
	glm::ivec3 a = (pos - ii) >> ChunkSizeBits;
	glm::ivec3 b = (pos + ii) >> ChunkSizeBits;
	FOR2(x, a.x, b.x) FOR2(y, a.y, b.y) FOR2(z, a.z, b.z)
	{
		glm::ivec3 cpos(x, y, z);
		Chunk& chunk = g_chunks.get(cpos);
		if (chunk.get_cpos() == cpos) chunk.m_active = true;
	}
}

void edit_block(glm::ivec3 pos, Block block)
{
	glm::ivec3 cpos = pos >> ChunkSizeBits;
	glm::ivec3 p = pos & ChunkSizeMask;
	static BlockRenderer renderer;

	Chunk& c = g_chunks.get(cpos);
	if (c.get_cpos() != cpos) return;

	c.set(p, block);
	if (block == Block::none) c.update_empty();
	c.remesh(renderer);
	activate_block(pos);

	if (p.x == 0) g_chunks.reset(cpos - ix, renderer);
	if (p.y == 0) g_chunks.reset(cpos - iy, renderer);
	if (p.z == 0) g_chunks.reset(cpos - iz, renderer);
	if (p.x == ChunkSizeMask) g_chunks.reset(cpos + ix, renderer);
	if (p.y == ChunkSizeMask) g_chunks.reset(cpos + iy, renderer);
	if (p.z == ChunkSizeMask) g_chunks.reset(cpos + iz, renderer);
}

void on_key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	show_palette = false;
	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
		return;
	}

	if (console.IsVisible())
	{
		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			if (key == GLFW_KEY_F1 || key == GLFW_KEY_CAPS_LOCK)
			{
				console.Hide();
			}
			else
			{
				console.OnKey(key, mods);
			}
		}
	}
	else if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_F1 || key == GLFW_KEY_CAPS_LOCK)
		{
			console.Show();
		}
		else if (key == GLFW_KEY_F2)
		{
			show_counters = !show_counters;
		}
		else if (key == GLFW_KEY_F3)
		{
			wireframe = !wireframe;
		}
		else if (key == GLFW_KEY_F4)
		{
			enable_f4 = !enable_f4;
		}
		else if (key == GLFW_KEY_F5)
		{
			enable_f5 = !enable_f5;
		}
		else if (key == GLFW_KEY_F6)
		{
			enable_f6 = !enable_f6;
		}
		else if (key == GLFW_KEY_TAB)
		{
			g_player.creative_mode = !g_player.creative_mode;
			g_player.velocity = glm::vec3(0, 0, 0);
			g_player.digging_on = false;
		}
	}
}

void on_mouse_button(GLFWwindow* window, int button, int action, int mods)
{
	show_palette = false;

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && selection && g_player.creative_mode)
	{
		edit_block(sel_cube, Block::none);
	}

	if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT)
	{
		g_player.digging_on = false;
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		edit_block(sel_cube + dir[sel_face], g_player.palette_block);
	}
}

void on_scroll(GLFWwindow* window, double x, double y)
{
	ts_palette = Timestamp();
	show_palette = true;
	scroll_dy = y / 5;
	scroll_y += scroll_dy;
	if (scroll_y < 0) scroll_y = 0;
	if (scroll_y > (block_count - 1 - (uint)Block::water)) scroll_y = (block_count - 2);
	g_player.palette_block = Block(uint(std::round(scroll_y)) + (uint)Block::water);
}

bool last_cursor_init = false;
double last_cursor_x, last_cursor_y;

void model_init(GLFWwindow* window)
{
	if (!g_player.load())
	{
		fprintf(stderr, "Failed to load player.json\n");
		exit(1);
	}
	glfwSetKeyCallback(window, on_key);
	glfwSetMouseButtonCallback(window, on_mouse_button);
	glfwSetScrollCallback(window, on_scroll);
}

int NeighborBit(int dx, int dy, int dz)
{
	return 1 << ((dx + 1) + (dy + 1) * 3 + (dz + 1) * 9);
}

bool IsVertexFree(uint neighbors, int dx, int dy, int dz)
{
	assert(abs(dx) + abs(dy) + abs(dz) == 3);
	uint mask = NeighborBit(dx, dy, dz);
	mask |= NeighborBit(0, dy, dz) | NeighborBit(dx, 0, dz) | NeighborBit(dx, dy, 0);
	mask |= NeighborBit(0, 0, dz) | NeighborBit(dx, 0, 0) | NeighborBit(0, dy, 0);
	return (neighbors & mask) == 0;
}

bool IsEdgeFree(uint neighbors, int dx, int dy, int dz)
{
	assert(abs(dx) + abs(dy) + abs(dz) == 2);
	uint mask = NeighborBit(dx, dy, dz);
	mask |= NeighborBit(0, dy, dz) | NeighborBit(dx, 0, dz) | NeighborBit(dx, dy, 0);
	return (neighbors & mask) == 0;
}

int Resolve(glm::vec3& delta, float k, float x, float y, float z)
{
	glm::vec3 d(x, y, z);
	float ds = sqr(d);
	if (ds < 1e-6) return -1;
	delta = d * (k / sqrtf(ds) - 1);
	return 1;
}

// Move sphere so that it is not coliding with cube
//
// Neighbors is 3x3x3 matrix of 27 bits representing nearby cubes.
// Used to turn off vertices and edges that are not exposed.
int sphere_vs_cube(glm::vec3& center, float radius, glm::ivec3 cube, int neighbors)
{
	float e = 0.5;
	float x = center.x - cube.x - e;
	float y = center.y - cube.y - e;
	float z = center.z - cube.z - e;
	float k = e + radius;

	if (x >= k || x <= -k || y >= k || y <= -k || z >= k || z <= -k) return 0;

	float s2 = sqr(radius + sqrtf(2) / 2);
	if (x*x + y*y >= s2 || x*x + z*z >= s2 || y*y + z*z >= s2) return 0;

	if (x*x + y*y + z*z >= sqr(radius + sqrt(3) / 2)) return 0;

	// if center outside cube
	if (x > +e)
	{
		if (y > +e)
		{
			if (z > +e) return IsVertexFree(neighbors, 1, 1, +1) ? Resolve(center, radius, x - e, y - e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, 1, 1, -1) ? Resolve(center, radius, x - e, y - e, z + e) : -1;
			return IsEdgeFree(neighbors, 1, 1, 0) ? Resolve(center, radius, x - e, y - e, 0) : -1;
		}
		if (y < -e)
		{
			if (z > +e) return IsVertexFree(neighbors, 1, -1, +1) ? Resolve(center, radius, x - e, y + e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, 1, -1, -1) ? Resolve(center, radius, x - e, y + e, z + e) : -1;
			return IsEdgeFree(neighbors, 1, -1, 0) ? Resolve(center, radius, x - e, y + e, 0) : -1;
		}

		if (z > +e) return IsEdgeFree(neighbors, 1, 0, +1) ? Resolve(center, radius, x - e, 0, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, 1, 0, -1) ? Resolve(center, radius, x - e, 0, z + e) : -1;
		center.x += radius - x + e;
		return 1;
	}

	if (x < -e)
	{
		if (y > +e)
		{
			if (z > +e) return IsVertexFree(neighbors, -1, 1, +1) ? Resolve(center, radius, x + e, y - e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, -1, 1, -1) ? Resolve(center, radius, x + e, y - e, z + e) : -1;
			return IsEdgeFree(neighbors, -1, 1, 0) ? Resolve(center, radius, x + e, y - e, 0) : -1;
		}
		if (y < -e)
		{
			if (z > +e) return IsVertexFree(neighbors, -1, -1, +1) ? Resolve(center, radius, x + e, y + e, z - e) : -1;
			if (z < -e) return IsVertexFree(neighbors, -1, -1, -1) ? Resolve(center, radius, x + e, y + e, z + e) : -1;
			return IsEdgeFree(neighbors, -1, -1, 0) ? Resolve(center, radius, x + e, y + e, 0) : -1;
		}

		if (z > +e) return IsEdgeFree(neighbors, -1, 0, +1) ? Resolve(center, radius, x + e, 0, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, -1, 0, -1) ? Resolve(center, radius, x + e, 0, z + e) : -1;
		center.x += -radius - x - e;
		return 1;
	}

	if (y > +e)
	{
		if (z > +e) return IsEdgeFree(neighbors, 0, 1, +1) ? Resolve(center, radius, 0, y - e, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, 0, 1, -1) ? Resolve(center, radius, 0, y - e, z + e) : -1;
		center.y += radius - y + e;
		return 1;
	}
	if (y < -e)
	{
		if (z > +e) return IsEdgeFree(neighbors, 0, -1, +1) ? Resolve(center, radius, 0, y + e, z - e) : -1;
		if (z < -e) return IsEdgeFree(neighbors, 0, -1, -1) ? Resolve(center, radius, 0, y + e, z + e) : -1;
		center.y += -radius - y - e;
		return 1;
	}

	if (z > +e)
	{
		center.z +=  radius - z + e;
		return 1;
	}
	if (z < -e)
	{
		center.z += -radius - z - e;
		return 1;
	}

	// center inside cube
	float ax = fabs(x), ay = fabs(y);
	if (ax > ay)
	{
		if (ax > fabs(z)) { center.x += (x > 0 ? k : -k) - x; return 1; }
	}
	else
	{
		if (ay > fabs(z)) { center.y += (y > 0 ? k : -k) - y; return 1; }
	}
	center.z += (z > 0 ? k : -k) - z;
	return 1;
}

int cuboid_vs_cube(glm::vec3 center, glm::vec3 radius, glm::ivec3 cube, glm::vec3& delta)
{
	float e = 0.5;
	float x = center.x - cube.x - e;
	float y = center.y - cube.y - e;
	float z = center.z - cube.z - e;

	glm::vec3 k = radius + e;

	if (x >= k.x || x <= -k.x || y >= k.y || y <= -k.y || z >= k.z || z <= -k.z) return 0;

	float dx = (x > 0 ? k.x : -k.x) - x;
	float dy = (y > 0 ? k.y : -k.y) - y;
	float dz = (z > 0 ? k.z : -k.z) - z;

	if (std::abs(dx) < std::abs(dy))
	{
		if (std::abs(dx) < std::abs(dz))
		{
			delta = glm::vec3(dx, 0, 0);
		}
		else
		{
			delta = glm::vec3(0, 0, dz);
		}
	}
	else
	{
		if (std::abs(dy) < std::abs(dz))
		{
			delta = glm::vec3(0, dy, 0);
		}
		else
		{
			delta = glm::vec3(0, 0, dz);
		}
	}
	return 1;
}

// Move cylinder so that it is not coliding with cube.
// Cylinder is always facing up.
//
// Neighbors is 3x3x3 (only 4 corners are used) matrix of bits representing nearby cubes.
// Used to turn off edges that are not exposed.
int cylinder_vs_cube(glm::vec3 center, float radius, float /*half*/height, glm::ivec3 cube, glm::vec3& delta, int neighbors)
{
	float e = 0.5;
	float x = center.x - cube.x - e;
	float y = center.y - cube.y - e;
	float z = center.z - cube.z - e;
	float kr = e + radius;
	float kh = e + height;

	if (x >= kr || x <= -kr || y >= kr || y <= -kr || z >= kh || z <= -kh) return 0;

	float s2 = sqr(radius + sqrtf(2) / 2);
	if (x*x + y*y >= s2) return 0;

	// Compute delta XY
	float dxy = 0;
	if (x > +e)
	{
		if (y > +e)
		{
			if (!IsEdgeFree(neighbors, 1, 1, 0) || !Resolve(delta, radius, x - e, y - e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else if (y < -e)
		{
			if (!IsEdgeFree(neighbors, 1, -1, 0) || !Resolve(delta, radius, x - e, y + e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else
		{
			delta = glm::vec3(kr - x, 0, 0);
			dxy = delta.x;
		}
	}
	else if (x < -e)
	{
		if (y > +e)
		{
			if (!IsEdgeFree(neighbors, -1, 1, 0) || !Resolve(delta, radius, x + e, y - e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else if (y < -e)
		{
			if (!IsEdgeFree(neighbors, -1, -1, 0) || !Resolve(delta, radius, x + e, y + e, 0)) return -1;
			dxy = sqrt(delta.x * delta.x + delta.y * delta.y);
		}
		else
		{
			delta = glm::vec3(-kr - x, 0, 0);
			dxy = -delta.x;
		}
	}
	else
	{
		if (y > +e)
		{
			delta = glm::vec3(0, kr - y, 0);
			dxy = delta.y;
		}
		else if (y < -e)
		{
			delta = glm::vec3(0, -kr - y, 0);
			dxy = -delta.y;
		}
		else
		{
			// move either by X or Y, depending which one is less
			float dx = (x > 0 ? kr : -kr) - x;
			float dy = (y > 0 ? kr : -kr) - y;
			if (std::abs(dx) < std::abs(dy))
			{
				delta = glm::vec3(dx, 0, 0);
				dxy = std::abs(dx);
			}
			else
			{
				delta = glm::vec3(0, dy, 0);
				dxy = std::abs(dy);
			}
		}
	}

	// Compute delta Z
	float dz = (z > 0 ? kh : -kh) - z;
	if (std::abs(dz) < dxy) delta = glm::vec3(0, 0, dz);
	return 1;
}

int cube_neighbors(glm::ivec3 cube)
{
	int neighbors = 0;
	FOR2(dx, -1, 1) FOR2(dy, -1, 1) FOR2(dz, -1, 1)
	{
		glm::ivec3 pos(cube.x + dx, cube.y + dy, cube.z + dz);
		if (!g_chunks.can_move_through(pos)) neighbors |= NeighborBit(dx, dy, dz);
	}
	return neighbors;
}

// TODO: simplify
int cube_neighbors2(glm::ivec3 cube)
{
	int neighbors = 0;
	FOR2(dx, -1, 1) FOR2(dy, -1, 1) if (dx != 0 && dy != 0)
	{
		glm::ivec3 pos(cube.x + dx, cube.y + dy, cube.z);
		if (!g_chunks.can_move_through(pos)) neighbors |= NeighborBit(dx, dy, 0);
	}
	return neighbors;
}

glm::vec3 gravity(glm::vec3 pos)
{
	return glm::vec3(0, 0, -30);

	glm::vec3 dir = glm::vec3(MoonCenter) - pos;
	double dist2 = glm::dot(dir, dir);
	double a = 10000000;

	if (dist2 > MoonRadius * MoonRadius)
	{
		return dir * (float)(a / (sqrt(dist2) * dist2));
	}
	else
	{
		return dir * (float)(a / (MoonRadius * MoonRadius * MoonRadius));
	}
}

const float player_acc = 35;
const float player_jump_dv = 15;
const float player_max_vel = 10;

bool on_the_ground = false;

void simulate(float dt, glm::vec3 dir, bool jump)
{
	if (g_player.creative_mode)
	{
		g_player.position += dir * (dt * 20);
	}
	else
	{
		glm::ivec3 p = glm::ivec3(glm::floor(g_player.position));
		bool underwater = g_chunks.get_block(p) == Block::water;

		if ((on_the_ground || underwater) && !jump)
		{
			if (dir.x != 0 || dir.y != 0 || dir.z != 0)
			{
				float d = glm::dot(dir, g_player.velocity);
				glm::vec3 a = dir * d;
				glm::vec3 b = g_player.velocity - a;
				float p = dt * player_acc;

				// Brake on side-velocity
				float b_len = glm::length(b);
				if (b_len <= p)
				{
					g_player.velocity -= b;
					p -= b_len;

					if (glm::length(a) > player_max_vel)
					{
						// Brake on velocity along running direction
						g_player.velocity -= glm::normalize(a) * p;
					}
					else
					{
						glm::vec3 v = g_player.velocity + dir * p;
						float v2 = glm::length2(v);
						if (v2 < glm::length2(g_player.velocity) || v2 < player_max_vel * player_max_vel) g_player.velocity = v;
					}
				}
				else
				{
					g_player.velocity -= glm::normalize(b) * p;
				}
			}
			else
			{
				float v = glm::length(g_player.velocity);
				if (v <= dt * player_acc)
				{
					g_player.velocity = glm::vec3(0, 0, 0);
				}
				else
				{
					g_player.velocity -= glm::normalize(g_player.velocity) * (dt * player_acc);
				}
			}
		}
		if (!underwater) g_player.velocity += gravity(g_player.position) * dt;
		g_player.position += g_player.velocity * dt;
	}

	on_the_ground = false;
	if (!g_collision && g_player.creative_mode) return;

	FOR(i, 2)
	{
		// Resolve all collisions simultaneously
		glm::ivec3 p = glm::ivec3(glm::floor(g_player.position));
		glm::vec3 sum(0, 0, 0);
		int c = 0;
		FOR2(x, p.x - 1, p.x + 1) FOR2(y, p.y - 1, p.y + 1) FOR2(z, p.z - 1, p.z + 1)
		{
			glm::ivec3 cube(x, y, z);
			if (g_chunks.can_move_through(cube)) continue;
			glm::vec3 delta;
			const float radius = 0.48;
			glm::vec3 q = g_player.position;
			if (true)
			{
				// q.z -= radius; BUG: unstable?
				if (1 == cylinder_vs_cube(q, radius, radius * 2, cube, /*out*/delta, cube_neighbors(cube)))
				{
					sum += delta;
					c += 1;
				}
			}
			else if (false)
			{
				if (1 == sphere_vs_cube(q, radius, cube, cube_neighbors(cube)))
				{
					sum += q - g_player.position;
					c += 1;
				}
			}
			else
			{
				// q.z -= radius; BUG: unstable?
				if (1 == cuboid_vs_cube(q, glm::vec3(radius, radius, radius * 2), cube, /*out*/delta))
				{
					sum += delta;
					c += 1;
				}
			}
		}
		if (c == 0)	break;
		float sum2 = glm::length2(sum);
		if (sum2 > 0)
		{
			glm::vec3 normal = sum * glm::inversesqrt(sum2);
			float d = glm::dot(normal, g_player.velocity);
			if (d < 0) g_player.velocity -= d * normal;
			if (glm::dot(normal, glm::normalize(gravity(g_player.position))) < -0.9) on_the_ground = true;
		}
		g_player.position += sum / (float)c;
	}
}

void model_move_player(GLFWwindow* window, float dt)
{
	glm::vec3 dir(0, 0, 0);

	float* m = glm::value_ptr(g_player.orientation);
	glm::vec3 forward(m[4], m[5], m[6]);
	glm::vec3 right(m[0], m[1], m[2]);

	if (!g_player.creative_mode)
	{
		forward.z = 0;
		forward = glm::normalize(forward);
		right.z = 0;
		right = glm::normalize(right);
	}

	if (glfwGetKey(window, 'A')) dir -= right;
	if (glfwGetKey(window, 'D')) dir += right;
	if (glfwGetKey(window, 'W')) dir += forward;
	if (glfwGetKey(window, 'S')) dir -= forward;
	if (glfwGetKey(window, 'Q')) dir[2] += 1;
	if (glfwGetKey(window, 'E')) dir[2] -= 1;

	bool jump = false;
	if (!g_player.creative_mode && on_the_ground && glfwGetKey(window, GLFW_KEY_SPACE))
	{
		g_player.velocity += glm::normalize(-gravity(g_player.position)) * player_jump_dv;
		jump = true;
	}

	glm::vec3 p = g_player.position;
	if (dir.x != 0 || dir.y != 0 || dir.z != 0)
	{
		show_palette = false;
		dir = glm::normalize(dir);
	}
	while (dt > 0)
	{
		if (dt <= 0.008)
		{
			simulate(dt, dir, jump);
			break;
		}
		simulate(0.008, dir, jump);
		dt -= 0.008;
	}
	if (p != g_player.position)
	{
		rays_remaining = directions.size();
		float* ma = glm::value_ptr(g_player.orientation);
		glm::vec3 direction(ma[4], ma[5], ma[6]);
		glm::ivec3 cplayer = glm::ivec3(glm::floor(g_player.position)) >> ChunkSizeBits;
		visible_chunks.cleanup(cplayer, direction);
	}
}

float intersect_line_plane(glm::vec3 orig, glm::vec3 dir, glm::vec4 plane)
{
	assert(is_unit_length(dir));
	assert(is_unit_length(plane.xyz()));
	return (-plane.w - glm::dot(orig, plane.xyz())) / glm::dot(dir, plane.xyz());
}

// Find cube face that contains closest point of intersection with the ray.
// No results if ray's origin is inside the cube
int intersects_ray_cube(glm::vec3 orig, glm::vec3 dir, glm::ivec3 cube)
{
	assert(is_unit_length(dir));
	orig -= glm::dvec3(cube);
	glm::vec3 cube_center(0.5f, 0.5f, 0.5f);
	float pos = glm::dot(cube_center - orig, dir);
	if (pos + BlockRadius < 0) return -1;

	// optional, check if cube is far from ray
	float dist2 = glm::distance2(cube_center, orig + dir * pos);
	if (dist2 >= 0.75) return -1;

	if (orig.x < 0 && dir.x > 0)
	{
		float d = -orig.x / dir.x;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.y && k.y <= 1 && 0 <= k.z && k.z <= 1) return 0;
	}
	if (orig.x > 1 && dir.x < 0)
	{
		float d = (1 - orig.x) / dir.x;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.y && k.y <= 1 && 0 <= k.z && k.z <= 1) return 1;
	}
	if (orig.y < 0 && dir.y > 0)
	{
		float d = -orig.y / dir.y;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.z && k.z <= 1) return 2;
	}
	if (orig.y > 1 && dir.y < 0)
	{
		float d = (1 - orig.y) / dir.y;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.z && k.z <= 1) return 3;
	}
	if (orig.z < 0 && dir.z > 0)
	{
		float d = -orig.z / dir.z;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.y && k.y <= 1) return 4;
	}
	if (orig.z > 1 && dir.z < 0)
	{
		float d = (1 - orig.z) / dir.z;
		glm::vec3 k = orig + dir * d;
		if (0 <= k.x && k.x <= 1 && 0 <= k.y && k.y <= 1) return 5;
	}
	return -1;
}

Sphere select_sphere(10);

bool select_cube(glm::ivec3& sel_cube, int& sel_face)
{
	glm::ivec3 p = glm::ivec3(glm::floor(g_player.position));
	float* ma = glm::value_ptr(g_player.orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);

	for (glm::ivec3 d : select_sphere)
	{
		glm::ivec3 cube = p + d;
		if (g_chunks.selectable_block(cube) && (sel_face = intersects_ray_cube(g_player.position, dir, cube)) != -1)
		{
			sel_cube = cube;
			return true;
		}
	}
	return false;
}

void model_orientation(GLFWwindow* window)
{
	double cursor_x, cursor_y;
	glfwGetCursorPos(window, &cursor_x, &cursor_y);
	if (!last_cursor_init)
	{
		last_cursor_init = true;
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
	}
	if (cursor_x != last_cursor_x || cursor_y != last_cursor_y)
	{
		show_palette = false;
		g_player.turn((cursor_x - last_cursor_x) / 150, (cursor_y - last_cursor_y) / 150);
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
		perspective_rotation = glm::rotate(perspective, g_player.pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, g_player.yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
		rays_remaining = directions.size();
	}
}

void model_digging(GLFWwindow* window)
{
	if (g_player.digging_on)
	{
		if (!selection)
		{
			g_player.digging_on = false;
			return;
		}

		if (sel_cube != g_player.digging_block)
		{
			g_player.start_digging(sel_cube);
		}
		else if (g_player.digging_start.elapsed_ms() > 2000)
		{
			edit_block(sel_cube, Block::none);
			g_player.digging_on = false;
			selection = select_cube(/*out*/sel_cube, /*out*/sel_face);
			if (selection) g_player.start_digging(sel_cube);
		}
	}
	else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS && selection)
	{
		g_player.start_digging(sel_cube);
	}
}

struct BlockRef
{
	Chunk* chunk;
	glm::i8vec3 ipos;
	Block block;

	operator Block() { return block; }
	BlockRef() { }
	explicit BlockRef(glm::ivec3 p) : chunk(g_chunks.get_opt(p >> ChunkSizeBits)), ipos(p & ChunkSizeMask), block(chunk ? chunk->get(glm::ivec3(ipos)) : Block::none) { }
};

static const int SimulationDistance = 6; // in chunks

static const int MaxActiveChunks = 50;
std::vector<glm::ivec3> sim_active_chunks;

void mark_remesh_chunk(glm::ivec3 pos)
{
	Chunk* chunk = g_chunks.get_opt(pos >> ChunkSizeBits);
	if (chunk && chunk->get(pos & ChunkSizeMask) != Block::none) chunk->m_remesh = true;
}

void mark_remesh_chunk(Chunk& chunk, glm::ivec3 p, glm::ivec3 q)
{
	chunk.m_remesh = true;
	if (q.x == CMin) mark_remesh_chunk(p - ix);
	if (q.x == CMax) mark_remesh_chunk(p + ix);
	if (q.y == CMin) mark_remesh_chunk(p - iy);
	if (q.y == CMax) mark_remesh_chunk(p + iy);
	if (q.z == CMin) mark_remesh_chunk(p - iz);
	if (q.z == CMax) mark_remesh_chunk(p + iz);
}

void update_block(BlockRef& ref, Block b)
{
	ref.block = b;
	glm::ivec3 q(ref.ipos);
	glm::ivec3 p = ref.chunk->get_cpos();
	ref.chunk->set(glm::ivec3(ref.ipos), b);
	mark_remesh_chunk(*ref.chunk, p, q);
	activate_block(q + (p << ChunkSizeBits));
}

void update_block(glm::ivec3 pos, Block b)
{
	glm::ivec3 p = pos >> ChunkSizeBits;
	glm::ivec3 q = pos & ChunkSizeMask;
	Chunk& chunk = g_chunks.get(p);
	assert(chunk.get_cpos() == p);
	chunk.set(q, b);
	mark_remesh_chunk(chunk, p, q);
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
	BlockRef m((b.chunk->get_cpos() << ChunkSizeBits) + glm::ivec3(b.ipos) - iz);
	return m.chunk && m.block == Block::water;
}

void model_simulate_water(BlockRef b, glm::ivec3 bpos)
{
	int w = water_level(b);

	// Flow down
	BlockRef m(bpos - iz);
	if (m.chunk && accepts_water(m.block))
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
			if (q.chunk && (q == Block::none || (q >= Block::water1 && q < b))) side.push_back(q);
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
			if (px.chunk && accepts_water(px) && py.chunk && accepts_water(py) && q.chunk && (q == Block::none || (q >= Block::water1 && q < b))) side.push_back(q);
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
			if (p.chunk && p == Block::none && q.chunk && accepts_water(q)) side.push_back(q);
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
			if (p.chunk && p == Block::none && px.chunk && px == Block::none && py.chunk && py == Block::none && q.chunk && accepts_water(q)) side.push_back(q);
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
			g_chunks.get(bpos >> ChunkSizeBits).m_active = true;
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
			g_chunks.get(bpos >> ChunkSizeBits).m_active = true;
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
		if (m.chunk && (m == Block::none || is_water(m)))
		{
			Block e = m.block;
			update_block(m, b.block);
			update_block(b, e);
		}
	}
	else if (b == Block::water_source)
	{
		BlockRef m(pos - iz);
		if (!m.chunk || !enable_f6) return;
		if (m == Block::none || is_water_partial(m))
		{
			int w = water_level(m);
			update_block(m, Block(int(Block::water1) + w));
		}
	}
	else if (b == Block::water_drain)
	{
		BlockRef m(pos + iz);
		if (m.chunk && is_water(m))
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
			if (q.chunk && is_water(q)) { update_block(q, Block::soul_sand); active = true; }
		}
		if (!active && rand() % 10 == 0)
		{
			update_block(b, Block::none);
		}
		else
		{
			g_chunks.get(pos >> ChunkSizeBits).m_active = true;
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
		Chunk& chunk = g_chunks.get(cpos);
		FOR(z, ChunkSize) FOR(y, ChunkSize) FOR(x, ChunkSize)
		{
			glm::ivec3 v(x, y, z);
			const Block b = chunk.get(v);
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

					const Chunk* c = g_chunks.get_opt((v + d) >> ChunkSizeBits);
					if (!c)
					{
						sim_visited_list.resize(e);
						break;
					}
					const Block b = c->get((v + d) & ChunkSizeMask);
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
			Block e = g_chunks.get_block(q + iz, Block::none);
			if (e != Block::sand && e != Block::red_sand && !is_water(e)) break;
			q.z += 1;
		}

		FOR2(i, a->z, q.z)
		{
			update_block(glm::ivec3(q.x, q.y, i-1), g_chunks.get_block(glm::ivec3(q.x, q.y, i)));
		}
		update_block(q, Block::none);

		a = b;
	}
}

void model_simulate_blocks()
{
	if (g_player.creative_mode) return;

	Timestamp tb;
	glm::ivec3 cplayer = glm::ivec3(glm::floor(g_player.position)) >> ChunkSizeBits;

	sim_active_chunks.clear();
	for (glm::ivec3 d : simulation_sphere)
	{
		glm::ivec3 cpos = cplayer + d;
		Chunk& chunk = g_chunks.get(cpos);
		if (chunk.get_cpos() != cpos || !chunk.m_active) continue;
		chunk.m_active = false;
		if (chunk.empty()) continue;
		sim_active_chunks.push_back(cpos);
		if (sim_active_chunks.size() == MaxActiveChunks) break;
	}

	// shuffle sim_order
	if (sim_active_chunks.size() > 0) FOR(i, ChunkSize2 / 4)
	{
		std::swap(sim_order[rand() % ChunkSize2], sim_order[rand() % ChunkSize2]);
	}

	for (glm::ivec3 cpos : sim_active_chunks)
	{
		FOR(z, ChunkSize) for (glm::i8vec2 xy : sim_order)
		{
			model_simulate_block(glm::ivec3(xy.x, xy.y, z) + (cpos << ChunkSizeBits));
		}
	}

	model_simulate_gravity();

	for (glm::ivec3 cpos : sim_active_chunks)
	{
		Chunk& chunk = g_chunks.get(cpos);
		if (!chunk.m_active) chunk.update_empty();
	}

	Timestamp tc;
	stats::simulate_time_ms = glm::mix<float>(stats::simulate_time_ms, tb.elapsed_ms(tc), 0.15f);
}

void model_frame(GLFWwindow* window, double delta_ms)
{
	model_orientation(window);
	Timestamp ta;
	if (!console.IsVisible()) model_move_player(window, delta_ms * 1e-3);
	Timestamp tb;
	selection = select_cube(/*out*/sel_cube, /*out*/sel_face);
	Timestamp tc;
	model_digging(window);
	model_simulate_blocks();

	stats::collide_time_ms = glm::mix<float>(stats::collide_time_ms, ta.elapsed_ms(tb), 0.15f);
	stats::select_time_ms = glm::mix<float>(stats::select_time_ms, tb.elapsed_ms(tc), 0.15f);
}

// Render

GLuint block_texture;
Text* text = nullptr;

int line_program;
GLuint line_matrix_loc;
GLuint line_position_loc;

int block_program;
GLuint block_matrix_loc;
GLuint block_sampler_loc;
GLuint block_pos_loc;
GLuint block_tick_loc;
GLuint block_foglimit2_loc;
GLuint block_eye_loc;

GLuint block_pos0_loc;
GLuint block_pos1_loc;
GLuint block_pos2_loc;
GLuint block_pos3_loc;
GLuint block_texture_loc;
GLuint block_light_loc;
GLuint block_plane_loc;

GLuint block_buffer;
GLuint line_buffer;

int g_tick;

struct BlockTextureLoader
{
	BlockTextureLoader();
	void load(BlockTexture block_texture);
	void load_textures();

private:
	uint m_width, m_height, m_size;
	std::vector<uint8_t> m_pixels;
};

BlockTextureLoader::BlockTextureLoader()
{
	uint8_t* image;
	char filename[1024];
	snprintf(filename, sizeof(filename), "bdc256/%s.png", block_texture_name[0]);
	uint error = lodepng_decode32_file(&image, &m_width, &m_height, filename);
	if (error)
	{
		fprintf(stderr, "lodepgn_decode32_file(%s) error %u: %s\n", filename, error, lodepng_error_text(error));
		exit(1);
	}
	free(image);
	m_size = m_width * m_height * 4;
}

void BlockTextureLoader::load(BlockTexture tex)
{
	unsigned char* image;
	unsigned iwidth, iheight;
	char filename[1024];
	if (tex > BlockTexture::lava_still && tex <= BlockTexture::lava_still_31) return;
	if (tex > BlockTexture::lava_flow && tex <= BlockTexture::lava_flow_31) return;
	if (tex > BlockTexture::water_still && tex <= BlockTexture::water_still_63) return;
	if (tex > BlockTexture::water_flow && tex <= BlockTexture::water_flow_31) return;
	if (tex > BlockTexture::furnace_front_on && tex <= BlockTexture::ff_15) return;
	if (tex > BlockTexture::sea_lantern && tex <= BlockTexture::sl_4) return;
	snprintf(filename, sizeof(filename), "bdc256/%s.png", block_texture_name[uint(tex)]);
	unsigned error = lodepng_decode32_file(&image, &iwidth, &iheight, filename);
	if (error)
	{
		fprintf(stderr, "lodepgn_decode32_file(%s) error %u: %s\n", filename, error, lodepng_error_text(error));
		exit(1);
	}
	Auto(free(image));

	assertf(m_width == iwidth, "m_wdith=%u iwidth=%u", m_width, iwidth);
	assertf((iheight % m_height) == 0, "iheight=%u m_height=%u", iheight, m_height);

	glm::u8vec4* pixel = (glm::u8vec4*)image;
	if (is_leaves(tex) || tex == BlockTexture::grass_top)
	{
		FOR(j, m_width * m_height)
		{
			pixel[j].r = 0;
			pixel[j].b = 0;
		}
	}
	if (tex == BlockTexture::cloud)
	{
		FOR(j, m_width * m_height)
		{
			pixel[j].a = pixel[j].r;
		}
	}

	uint frames = 1;
	if (tex == BlockTexture::water_still) frames = 64;
	if (tex == BlockTexture::water_flow) frames = 32;
	if (tex == BlockTexture::lava_still || tex == BlockTexture::lava_flow) frames = 32;
	if (tex == BlockTexture::furnace_front_on) frames = 16;
	if (tex == BlockTexture::sea_lantern) frames = 5;
	memcpy(m_pixels.data() + uint(tex) * m_size, image, m_size * frames);
}

void BlockTextureLoader::load_textures()
{
	m_pixels.resize(m_width * m_height * 4 * block_texture_count);

	// Use 7 instead of 8 threads to avoid the same thread from having to load all animated textures
	std::thread threads[7];
	FOR(i, 7)
	{
		threads[i] = std::move(std::thread([](BlockTextureLoader* self, int k)
		{
			FOR(j, block_texture_count)
			{
				if (j % 7 == k) self->load((BlockTexture)j);
			}
		}, this, i));
	}
	FOR(i, 7) threads[i].join();

	glGenTextures(1, &block_texture);
	glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, m_width, m_height, block_texture_count, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
}

void render_init()
{
	printf("OpenGL version: [%s]\n", glGetString(GL_VERSION));
	glEnable(GL_CULL_FACE);

	{
		BlockTextureLoader loader;
		loader.load_textures();
	}

	perspective = glm::perspective<float>(M_PI / 180 * 90, width / (float)height, 0.03, 1000);
	perspective_rotation = glm::rotate(perspective, g_player.pitch, glm::vec3(1, 0, 0));
	perspective_rotation = glm::rotate(perspective_rotation, g_player.yaw, glm::vec3(0, 1, 0));
	perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
	rays_remaining = directions.size();
	last_cursor_init = false;
	text = new Text;

	line_program = load_program("line");
	line_matrix_loc = glGetUniformLocation(line_program, "matrix");
	line_position_loc = glGetAttribLocation(line_program, "position");

	block_program = load_program("block", true);
	block_matrix_loc = glGetUniformLocation(block_program, "matrix");
	block_sampler_loc = glGetUniformLocation(block_program, "sampler");
	block_pos_loc = glGetUniformLocation(block_program, "cpos");
	block_tick_loc = glGetUniformLocation(block_program, "tick");
	block_foglimit2_loc = glGetUniformLocation(block_program, "foglimit2");
	block_eye_loc = glGetUniformLocation(block_program, "eye");

	// TODO: assert these aren't -1
	block_pos0_loc = glGetAttribLocation(block_program, "vertex0");
	block_pos1_loc = glGetAttribLocation(block_program, "vertex1");
	block_pos2_loc = glGetAttribLocation(block_program, "vertex2");
	block_pos3_loc = glGetAttribLocation(block_program, "vertex3");
	block_texture_loc = glGetAttribLocation(block_program, "block_texture_with_flag");
	block_light_loc = glGetAttribLocation(block_program, "light");
	block_plane_loc = glGetAttribLocation(block_program, "plane");

	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &line_buffer);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glClearColor(0.2, 0.4, 1, 1.0);
	glViewport(0, 0, width, height);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void BlockRenderer::draw_quad(int face, bool reverse, bool underwater_overlay)
{
	Quad q;
	q.texture = get_block_texture(m_block, face);
	if (underwater_overlay) q.texture = BlockTexture((int)q.texture | (1 << 15));
	const int* f = Cube::faces[face];
	q.light = face_light2(face); // TODO reverse?
	glm::ivec3 w = m_pos & ChunkSizeMask;
	if (reverse)
	{
		q.pos[0] = glm::u8vec3((w + Cube::corner[f[0]]) * 15);
		q.pos[1] = glm::u8vec3((w + Cube::corner[f[3]]) * 15);
		q.pos[2] = glm::u8vec3((w + Cube::corner[f[2]]) * 15);
		q.pos[3] = glm::u8vec3((w + Cube::corner[f[1]]) * 15);
		q.plane = ((face ^ 1) << 8) | q.pos[0][face / 2];
	}
	else
	{
		q.pos[0] = glm::u8vec3((w + Cube::corner[f[0]]) * 15);
		q.pos[1] = glm::u8vec3((w + Cube::corner[f[1]]) * 15);
		q.pos[2] = glm::u8vec3((w + Cube::corner[f[2]]) * 15);
		q.pos[3] = glm::u8vec3((w + Cube::corner[f[3]]) * 15);
		q.plane = (face << 8) | q.pos[0][face / 2];
	}
	m_quads->push_back(q);
}

static glm::ivec3 adjust(glm::ivec3 v, int zmin, int zmax)
{
	return glm::ivec3(v.x * 15, v.y * 15, (v.z == 0) ? zmin : zmax);
}

void BlockRenderer::draw_quad(int face, int zmin, int zmax, bool reverse, bool underwater_overlay)
{
	Quad q;
	q.texture = get_block_texture(m_block, face);
	if (underwater_overlay) q.texture = BlockTexture((int)q.texture | (1 << 15));
	const int* f = Cube::faces[face];
	q.light = face_light2(face); // TODO zmin/zmax? // TODO reverse?
	glm::ivec3 w = m_pos & ChunkSizeMask;
	if (reverse)
	{
		q.pos[0] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[0]], zmin, zmax));
		q.pos[1] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[3]], zmin, zmax));
		q.pos[2] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[2]], zmin, zmax));
		q.pos[3] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[1]], zmin, zmax));
		q.plane = ((face ^ 1) << 8) | q.pos[0][face / 2];
	}
	else
	{
		q.pos[0] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[0]], zmin, zmax));
		q.pos[1] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[1]], zmin, zmax));
		q.pos[2] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[2]], zmin, zmax));
		q.pos[3] = glm::u8vec3(w * 15 + adjust(Cube::corner[f[3]], zmin, zmax));
		q.plane = (face << 8) | q.pos[0][face / 2];
	}
	m_quads->push_back(q);
}

const float foglimit2 = sqr(0.8 * RenderDistance * ChunkSize);

void render_world_blocks(const glm::mat4& matrix, const Frustum& frustum)
{
	glEnable(GL_DEPTH_TEST);
	Auto(glDisable(GL_DEPTH_TEST));

	glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
	if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	Auto(if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));

	glUseProgram(block_program);
	glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform3fv(block_eye_loc, 1, glm::value_ptr(g_player.position));
	if (!g_player.creative_mode) g_tick += 1;
	glUniform1i(block_tick_loc, g_tick);
	glUniform1f(block_foglimit2_loc, foglimit2);

	glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
	glEnableVertexAttribArray(block_pos0_loc);
	glEnableVertexAttribArray(block_pos1_loc);
	glEnableVertexAttribArray(block_pos2_loc);
	glEnableVertexAttribArray(block_pos3_loc);
	glEnableVertexAttribArray(block_texture_loc);
	glEnableVertexAttribArray(block_light_loc);
	glEnableVertexAttribArray(block_plane_loc);

	glVertexAttribIPointer(block_pos0_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[0]);
	glVertexAttribIPointer(block_pos1_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[1]);
	glVertexAttribIPointer(block_pos2_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[2]);
	glVertexAttribIPointer(block_pos3_loc,    3, GL_UNSIGNED_BYTE,  sizeof(Quad), &((Quad*)0)->pos[3]);
	glVertexAttribIPointer(block_texture_loc, 1, GL_UNSIGNED_SHORT, sizeof(Quad), &((Quad*)0)->texture);
	glVertexAttribIPointer(block_light_loc,   1, GL_UNSIGNED_SHORT, sizeof(Quad), &((Quad*)0)->light);
	glVertexAttribIPointer(block_plane_loc,   1, GL_UNSIGNED_SHORT, sizeof(Quad), &((Quad*)0)->plane);

	stats::chunk_count = 0;
	stats::quad_count = 0;

	glEnable(GL_BLEND);
	static BlockRenderer renderer;
	for (glm::ivec3 cpos : visible_chunks)
	{
		if (!frustum.is_sphere_outside(glm::vec3(cpos * ChunkSize + ChunkSize / 2), ChunkSize * BlockRadius))
		{
			Chunk& chunk = g_chunks.get(cpos);
			if (chunk.get_cpos() != cpos || !chunk.renderable()) continue;

			if (chunk.m_remesh) chunk.remesh(renderer);
			glm::ivec3 pos = cpos * ChunkSize;
			glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
			// TODO: avoid expensive sorting for far chunks
			chunk.sort(g_player.position);
			stats::quad_count += chunk.render();
			stats::chunk_count += 1;
		}
	}
	glDisable(GL_BLEND);
}

void server_main();
Socket g_client;
RingBuffer g_recv_buffer;
RingBuffer g_send_buffer;

void net_frame()
{
	CHECK2(g_recv_buffer.recv_any(g_client), exit(1));
	CHECK2(g_send_buffer.send_any(g_client), exit(1));

	if (g_recv_buffer.size == 0) return;
	uint8_t size = g_recv_buffer.buffer[g_recv_buffer.begin];
	if (g_recv_buffer.size >= size + 1)
	{
		if (g_recv_buffer.begin + 1 + size <= sizeof(RingBuffer::buffer))
		{
			char* msg = (char*)g_recv_buffer.buffer + g_recv_buffer.begin + 1;
			console.Print("Server: [%.*s]\n", size, msg);
		}
		else
		{
			char* msg1 = (char*)g_recv_buffer.buffer + g_recv_buffer.begin + 1;
			int size1 = sizeof(g_recv_buffer.buffer) - g_recv_buffer.begin - 1;
			char* msg2 = (char*)g_recv_buffer.buffer;
			int size2 = size - size1;
			console.Print("Server: [%.*s%.*s]\n", size1, msg1, size2, msg2);
		}
		g_recv_buffer.pop_front(size + 1);
	}
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);

	text->Reset(width, height, matrix);
	if (show_counters && !console.IsVisible())
	{
		int raytrace = std::round(100.0f * (directions.size() - rays_remaining) / directions.size());
		text->Print("[%.1f %.1f %.1f] C:%4d Q:%3dk frame:%2.0f model:%1.0f raytrace:%2.0f %d%% render %2.0f F%c%c%c recv:%d send:%d",
			g_player.position.x, g_player.position.y, g_player.position.z, stats::chunk_count, stats::quad_count / 1000,
			stats::frame_time_ms, stats::model_time_ms, stats::raytrace_time_ms, raytrace, stats::render_time_ms,
			enable_f4 ? '4' : '-', enable_f5 ? '5' : '-', enable_f6 ? '6' : '-', g_recv_buffer.size, g_send_buffer.size);

		text->Print("collide:%1.0f select:%1.0f simulate:%1.0f [%.1f %.1f %.1f] %.1f%s",
			stats::collide_time_ms, stats::select_time_ms, stats::simulate_time_ms,
			g_player.velocity.x, g_player.velocity.y, g_player.velocity.z, glm::length(g_player.velocity), on_the_ground ? " ground" : "");

		if (selection)
		{
			Block block = g_chunks.get_block(sel_cube);
			text->Print("selected: %s at [%d %d %d]", block_name[(uint)block], sel_cube.x, sel_cube.y, sel_cube.z);
		}

		if (g_player.digging_on)
		{
			text->Print("digging for %.1f seconds", g_player.digging_start.elapsed_ms() / 1000);
		}
	}

	console.Render(text, glfwGetTime());
	glUseProgram(0);

	if (show_palette)
	{
		if (scroll_dy <= 0.1 / 5) scroll_y += (std::round(scroll_y) - scroll_y) * 0.05;

		glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
		glUseProgram(block_program);
		glm::mat4 view;
		view = glm::rotate(view, float(M_PI * 1.75), glm::vec3(1,0,0));
		view = glm::rotate(view, float(M_PI * 0.25), glm::vec3(0,0,1));
		view = glm::translate(view, glm::vec3(-128,-128,-128));
		view = glm::ortho<float>(-8 * width / height, 8 * width / height, -8, 8, -64, 64) * view;
		view = glm::translate(view, glm::vec3(-scroll_y, scroll_y, -1));
		glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(view));

		glm::vec3 eye(8, 8, 8);
		glUniform3fv(block_eye_loc, 1, glm::value_ptr(eye));
		glUniform1i(block_tick_loc, g_tick);
		glUniform1f(block_foglimit2_loc, 1e30);

		glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
		glEnableVertexAttribArray(block_pos0_loc);
		glEnableVertexAttribArray(block_pos1_loc);
		glEnableVertexAttribArray(block_pos2_loc);
		glEnableVertexAttribArray(block_pos3_loc);
		glEnableVertexAttribArray(block_texture_loc);
		glEnableVertexAttribArray(block_light_loc);
		glEnableVertexAttribArray(block_plane_loc);

		glVertexAttribIPointer(block_pos0_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[0]);
		glVertexAttribIPointer(block_pos1_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[1]);
		glVertexAttribIPointer(block_pos2_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[2]);
		glVertexAttribIPointer(block_pos3_loc,    3, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->pos[3]);
		glVertexAttribIPointer(block_texture_loc, 1, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->texture);
		glVertexAttribIPointer(block_light_loc,   1, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->light);
		glVertexAttribIPointer(block_plane_loc,   1, GL_UNSIGNED_SHORT, sizeof(WQuad), &((WQuad*)0)->plane);

		const uint palette_blocks = block_count - (uint)Block::water;
		WQuad quads[3 * palette_blocks];
		WQuad* e = quads;
		FOR(i, palette_blocks) FOR(face, 6)
		{
			if (face != 0 && face != 2 && face != 5) continue;

			const int* f = Cube::faces[face];
			uint8_t light = 255;
			if (face == 2) light = 127;
			if (face == 5) light = 127 + 64;
			if (face == 0) light = 255;

			glm::ivec3 w(128+i, 128-i, 128);
			FOR(j, 4) e->pos[j] = glm::u16vec3((w + Cube::corner[f[j]]) * 15);
			e->plane = face << 8;
			e->light = 65535;
			e->texture = get_block_texture(Block(i + block_count - palette_blocks), face);
			e += 1;
		}

		glEnable(GL_BLEND);
		glm::ivec3 pos(0, 0, 0);
		glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
		glBufferData(GL_ARRAY_BUFFER, sizeof(WQuad) * 3 * palette_blocks, quads, GL_STREAM_DRAW);
		glDrawArrays(GL_POINTS, 0, 3 * palette_blocks);
		glUseProgram(0);
		glDisable(GL_BLEND);

		text->Reset(width, height, matrix);
		text->PrintAt(width / 2 - height / 40, height / 2 - height / 40, 0, block_name[(uint)g_player.palette_block], strlen(block_name[(uint)g_player.palette_block]));

		if (ts_palette.elapsed_ms() >= 1000) show_palette = false;
	}

	if (selection || show_palette)
	{
		glUseProgram(line_program);
		glUniformMatrix4fv(line_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));

		glBindBuffer(GL_ARRAY_BUFFER, line_buffer);
		glEnableVertexAttribArray(line_position_loc);
		Auto(glDisableVertexAttribArray(line_position_loc));

		glVertexAttribPointer(line_position_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);

		glm::vec2 vline[4];
		vline[0] = glm::vec2(width/2 - 15, height / 2);
		vline[1] = glm::vec2(width/2 + 15, height / 2);
		vline[2] = glm::vec2(width/2, height / 2 - 15);
		vline[3] = glm::vec2(width/2, height / 2 + 15);

		glBufferData(GL_ARRAY_BUFFER, sizeof(vline), vline, GL_STREAM_DRAW);
		glDrawArrays(GL_LINES, 0, 4);
	}
}

void OnError(int error, const char* message)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, message);
}

double Timestamp::milisec_per_tick = 0;

std::atomic<Timestamp> stall_ts;
std::atomic<bool> stall_enable(true);

void stall_alarm_thread()
{
	while (true)
	{
		usleep(1000);
		if (!stall_enable.load(std::memory_order_relaxed)) break;
		Timestamp a = stall_ts;
		if (a.elapsed_ms() > 10000)
		{
			fprintf(stderr, "main thread stalled :(\n");
			exit(1);
		}
	}
}

void wait_for_nearby_chunks(glm::ivec3 cpos)
{
	FOR2(x, -1, 1) FOR2(y, -1, 1) FOR2(z, -1, 1) while (true)
	{
		glm::ivec3 p(x, y, z);
		Chunk& chunk = g_chunks.get(p + cpos);
		if (chunk.get_cpos() == p + cpos) break;
		usleep(10000);
	}
}

bool equal(const char* a, std::pair<const char*, int> b)
{
	return strlen(a) == b.second && memcmp(a, b.first, b.second) == 0;
}

bool is_integer(std::pair<const char*, int> a)
{
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
	}
	if (a.second > 9) return false;
	FOR(i, a.second) if (!isdigit(a.first[i])) return false;
	return true;
}

int parse_int(std::pair<const char*, int> a)
{
	bool negative = false;
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
		negative = true;
	}
	int v = 0;
	FOR(i, a.second) v = v * 10 + a.first[i] - '0';
	return negative ? -v : v;
}

bool is_real(std::pair<const char*, int> a)
{
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
	}
	FOR(i, a.second) if (!isdigit(a.first[i]) && a.first[i] != '.') return false;
	int dots = 0;
	FOR(i, a.second) if (a.first[i] == '.') dots += 1;
	return dots <= 1;
}

double parse_real(std::pair<const char*, int> a)
{
	bool negative = false;
	if (a.second > 1 && a.first[0] == '-')
	{
		a.first += 1;
		a.second -= 1;
		negative = true;
	}
	double v = 0;
	double e = 0.1;
	bool dot = false;
	FOR(i, a.second)
	{
		if (a.first[i] == '.')
		{
			dot = true;
		}
		else if (!dot)
		{
			v = v * 10 + (a.first[i] - '0');
		}
		else
		{
			v += e * (a.first[i] - '0');
			e /= 10;
		}
	}
	return negative ? -v : v;
}

void command_import(char* filename, glm::ivec3 pos, float scale)
{
	std::thread([=]()
	{
		Auto(free(filename));
		console.Print("importing %s at (%d %d %d) scale %f ...\n", filename, pos.x, pos.y, pos.z, scale);

		Mesh mesh;
		if (!mesh.load(filename))
		{
			console.Print("loading file %s failed\n", filename);
			return;
		}

		rasterize_mesh(mesh, pos, scale, Block::grass);
		console.Print("import completed\n");
	}).detach();
}

typedef std::pair<const char*, int> Token;

void command_set()
{
	console.Print("collision = %s\n", g_collision ? "true" : "false");
}

void command_set(Token key, Token value)
{
	if (equal("collision", key))
	{
		if (equal("true", value)) { g_collision = true; return; }
		if (equal("false", value)) { g_collision = false; return; }
		console.Print("error in syntax: set collision (true | false)\n");
		return;
	}
	console.Print("unknown var %.*s. type 'set' for list of all vars.", key.second, key.first);
}

void MyConsole::Execute(const char* command, int length)
{
	if (command[0] == '/') // shout!
	{
		if (length == 1) return;
		command += 1;
		length -= 1;
		Print("You: %.*s\n", length, command);
		if (g_send_buffer.space() >= length + 1)
		{
			uint8_t a = length;
			g_send_buffer.write_back(&a, 1);
			g_send_buffer.write_back((const uint8_t*)command, length);
		}
		return;
	}

	std::vector<std::pair<const char*, int>> tokens;
	FOR(i, length)
	{
		if (command[i] == ' ') continue;
		if (i == 0 || command[i-1] == ' ') tokens.push_back({ command + i, 1 }); else tokens.back().second += 1;
	}

	if (equal("import", tokens[0]))
	{
		if (tokens.size() != 6 || !is_integer(tokens[2]) || !is_integer(tokens[3]) || !is_integer(tokens[4]) || !is_real(tokens[5]))
		{
			Print("error in syntax: import <filename> <x> <y> <z> <scale>\n");
			return;
		}
		char* filename = (char*)memcpy(malloc(tokens[1].second + 1), tokens[1].first, tokens[1].second);
		filename[tokens[1].second] = 0;
		command_import(filename, glm::ivec3(parse_int(tokens[2]), parse_int(tokens[3]), parse_int(tokens[4])), parse_real(tokens[5]));
		return;
	}

	if (equal("set", tokens[0]))
	{
		if (tokens.size() == 1) { command_set(); return; }
		if (tokens.size() == 3) { command_set(tokens[1], tokens[2]); return; }
		Print("error in syntax: set [<key> <value>]\n");
		return;
	}

	assert(tokens.size() > 0);
	Print("[%.*s] command not found\n", tokens[0].second, tokens[0].first);
}

GLFWwindow* create_window()
{
	glfwSetErrorCallback(OnError);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode->width*2, mode->height*2, "Arena", glfwGetPrimaryMonitor(), NULL);
	if (!window) return nullptr;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1/*VSYNC*/);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);
	return window;
}

int main(int, char**)
{
	void sigsegv_handler(int sig);
	signal(SIGSEGV, sigsegv_handler);
	CHECK(glfwInit());

	std::thread(server_main).detach();

	glm::dvec3 a;
	glm::i64vec3 b;
	a.x = glfwGetTime();
	b.x = rdtsc();
	usleep(100000);
	a.y = glfwGetTime();
	b.y = rdtsc();
	usleep(100000);
	a.z = glfwGetTime();
	b.z = rdtsc();

	GLFWwindow* window = create_window();
	CHECK(window);
	model_init(window);
	render_init();
	fprintf(stderr, "Loading chunks in background ...\n");
	g_chunks.fork_threads();

	glm::dvec3 c;
	glm::i64vec3 d;
	c.x = glfwGetTime();
	d.x = rdtsc();
	usleep(100000);
	c.y = glfwGetTime();
	d.y = rdtsc();
	usleep(100000);
	c.z = glfwGetTime();
	d.z = rdtsc();
	Timestamp::init(a, b, c, d);

	Timestamp t0;
	stall_ts = t0;
	std::thread stall_alarm(stall_alarm_thread);

	glm::ivec3 cplayer = glm::ivec3(glm::floor(g_player.position)) >> ChunkSizeBits;
	wait_for_nearby_chunks(cplayer);

	while (!g_client.connect("localhost", "7000"))
	{
		if (errno != ECONNREFUSED) CHECK2(false, exit(1));
		usleep(100*1000);
		g_client.close();
	}

	fprintf(stderr, "Ready!\n");
	while (!glfwWindowShouldClose(window))
	{
		Timestamp ta;
		stall_ts = ta;
		double frame_ms = t0.elapsed_ms(ta);
		t0 = ta;
		glfwPollEvents();

		net_frame();

		model_frame(window, frame_ms);
		glm::mat4 matrix = glm::translate(perspective_rotation, -g_player.position);
		Frustum frustum(matrix);
		g_player.cpos = glm::ivec3(glm::floor(g_player.position)) >> ChunkSizeBits;
		g_player.atomic_cpos = compress_ivec3(g_player.cpos);

		Timestamp tb;
		update_render_list(cplayer, frustum);

		Timestamp tc;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		render_world_blocks(matrix, frustum);
		render_gui();

		if (show_counters)
		{
			Timestamp td;
			stats::frame_time_ms = glm::mix<float>(stats::frame_time_ms, frame_ms, 0.15f);
			stats::model_time_ms = glm::mix<float>(stats::model_time_ms, ta.elapsed_ms(tb), 0.15f);
			stats::raytrace_time_ms = glm::mix<float>(stats::raytrace_time_ms, tb.elapsed_ms(tc), 0.15f);
			stats::render_time_ms = glm::mix<float>(stats::render_time_ms, tc.elapsed_ms(td), 0.15f);
		}

		glfwSwapBuffers(window);
	}

	fprintf(stderr, "Waiting for threads ...\n");
	stall_enable.store(false);
	stall_alarm.join();
	g_chunks.join_threads();
	fprintf(stderr, "Saving ...\n");
	g_scm.save();
	g_player.save();
	fprintf(stderr, "Exiting ...\n");
	glfwTerminate();
	_exit(0); // exit(0) is not enough
	return 0;
}
