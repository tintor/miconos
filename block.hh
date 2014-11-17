#pragma once
#include "util.hh"
#include "algorithm.hh"

const int ChunkSizeBits = 4, SuperChunkSizeBits = 4, MapSizeBits = 7;
const int ChunkSize = 1 << ChunkSizeBits, SuperChunkSize = 1 << SuperChunkSizeBits, MapSize = 1 << MapSizeBits;
const int CMin = 0, CMax = ChunkSize - 1;
const int ChunkSizeMask = ChunkSize - 1, SuperChunkSizeMask = SuperChunkSize - 1, MapSizeMask = MapSize - 1;

const int ChunkSize2 = ChunkSize * ChunkSize;
const int ChunkSize3 = ChunkSize * ChunkSize * ChunkSize;

#define FuncStr(E) #E,
#define FuncCount(E) +1
#define FuncList(E) E,

#define EnumBlocks(A, F, B) A \
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

static const uint block_count = EnumBlocks(0, FuncCount, +0);
extern const char* block_name[block_count];
static_assert(block_count <= 256, "");
enum class Block : uint8_t EnumBlocks({, FuncList, });

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

static const uint block_texture_count = BlockTextures(0, FuncCount, +0);
extern const char* block_texture_name[block_texture_count];
static_assert(block_texture_count <= 65535, "");
enum class BlockTexture : uint16_t BlockTextures({, FuncList, });

inline bool is_leaves(BlockTexture a) { return a >= BlockTexture::leaves_acacia && a <= BlockTexture::leaves_spruce; }
inline bool is_leaves(Block a) { return a >= Block::leaves_acacia && a <= Block::leaves_spruce; }
inline bool is_log(Block a) { return a >= Block::log_acacia && a <= Block::log_spruce; }
inline bool is_sand(Block a) { return a == Block::sand || a == Block::red_sand; }
inline bool is_water(Block a) { return a <= Block::water && a >= Block::water1; }
inline bool is_water_partial(Block a) { return a >= Block::water1 && a < Block::water; }
inline bool can_move_through(Block block) { return block <= Block::cloud; }

inline bool can_see_through_non_water(Block block) { return block == Block::none || is_leaves(block) || block == Block::ice || block == Block::glass_white; }
inline bool can_see_through(Block block) { return block <= Block::glass_white; }

inline bool is_blended(BlockTexture a) { return a == BlockTexture::ice || a == BlockTexture::glass_white || a == BlockTexture::water_still || a == BlockTexture::slime; }

static_assert(Block::water1 == (Block)1, "");
inline bool accepts_water(Block b) { return b < Block::water; }
inline int water_level(Block b) { return int(b); }

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

BlockTexture get_block_texture(Block block, int face);

typedef XCube<ChunkSize, Block> Blocks;
