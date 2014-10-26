// UNDONE: main thread should unload chunks that are too far (to make room for new ones)
// BUG: stray blue pixels on quad seams!
// BUG: model_frame() is too slow! -> PROFILE!
// - chunk loading/buffering too slow! -> PROFILE!
// - raytracer too slow! -> PROFILE! new counter: rays / second
// - compare perf with past changes, to avoid regressions!

// TODO:
// - MacBook Air support: detect number of cores, non-retina resolution, auto-reduce render distance based on frame time

// - Revert back to simpler raytracing model with flood fill of all can-see-through blocks

// # more proceduraly generated stuff!
// - nicer generated trees! palms!

// # gravity mode: planar (2 degrees of freedom orientation), spherical (2 degrees of freedom orientation), zero-g (3 degrees of freedom orientation)

// # PERF level-of-detail rendering
// - geometry shader to generate 6 vertices from Quad (try to reduce memory bandwidth when sending vertices)
// - triangle reduction for far away chunks:
//	- remove interior quads not visible from far outside (ie. cave close to the surface OR back side of far away wall)
//    - remove small lone blocks, fill small lone holes
//    - two concave quads at 90 degrees can be converted into slope (even if not far!)
// - chunk reduction:
//    - combine 2x2x2 chunks that are far to be able to optimize their triangles better and to reduce the total number of chunks as render distances are increasing
//		- should help with ray-tracing as well, as ray will hit (and make visible) 2x2x2 chunk as single unit

// Textures:
// - re-sort triangles inside chunk buffer (for axis chunks only) as player moves
// - nicer transparency for blocks with holes like leaves (render all backfaces)
// - nicer transpaarency for translucent blocks without holes (only render backfaces at the end of group of same translucent blocks)
// - BUGS: render inside faces for leaves (use interior opaque leaf textures to optimize rendering of dense trees?)

// # client / server:
// - terrain generation on server
// - server sends: chunks, block updates and player position
// - client sends: edits, player commands

// multi-player:
// - render simple avatars
// - login users
// - chat

// # database:
// - save player position on exit
// - compress block files (to save space and to reduce write amplification)

// # rendering:
// - static models: enchanting table, torch, cactus, water (different amounts)
// - texture overlays: redstone powder, ladder, railroad
// - 2d sprites: fire, plants, grass, flowers, web, ...
// - directional blocks: crafting table, furnace, pumpkin

// # survival mode:
// - tool rendering
// - digging
// - inventory with 2x2 crafting
// - inventory items with icons
// - dropping items on the ground (ie. by hand or when digging)
// - 3x3 crafting table UI
// - furnace UI

// sky:
// - day/night cycle
// - sun with bloom effect (shaders)

// # advanced editor:
// # - free mouse (but fixed camera) mode for editing
// # - orbit camera mode for editing
// # - flood fill tool
// # - selection tool
// # - cut/copy/paste/move tool
// # - drawing shapes (line / wall)
// # - undo / redo
// # - wiki / user change tracking

// # water:
// # - animated / reflective surface (shaders)

// # ambient lighting and shadows

// # portals! inside world and between worlds

// # bots / critters
// # python scripting integration (sandbox)

#include "util.hh"
#include "callstack.hh"
#include "rendering.hh"
#include "auto.hh"
#include "unistd.h"

#define LODEPNG_COMPILE_CPP
#include "lodepng/lodepng.h"

void sigsegv_handler(int sig)
{
	fprintf(stderr, "Error: signal %d:\n", sig);
	void* array[20];
	backtrace_symbols_fd(array, backtrace(array, 20), STDERR_FILENO);
	exit(1);
}

void __assert_rtn(const char* func, const char* file, int line, const char* cond)
{
	fprintf(stderr, "Assertion: (%s), function %s, file %s, line %d.\n", cond, func, file, line);
	void* array[20];
	backtrace_symbols_fd(array, backtrace(array, 20), STDERR_FILENO);
	exit(1);
}

void __assert_rtn_format(const char* func, const char* file, int line, const char* cond, const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	fprintf(stderr, "Assertion: (%s), ", cond);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, ", function %s, file %s, line %d.\n", func, file, line);
	va_end(va);
	void* array[20];
	backtrace_symbols_fd(array, backtrace(array, 20), STDERR_FILENO);
	exit(1);
}

#define assertf(C, fmt, ...) do { if (!(C)) __assert_rtn_format(__func__, __FILE__, __LINE__, #C, fmt, __VA_ARGS__); } while(0)

Initialize { signal(SIGSEGV, sigsegv_handler); }

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

const int RenderDistance = 32;
static_assert(RenderDistance < MapSize / 2, "");

Sphere render_sphere(RenderDistance);

// ========================

#define FuncStr(E) #E,
#define FuncCount(E) +1
#define FuncList(E) E,

#define Blocks(A, F, B) A \
	F(none) \
	F(leaves_acacia) \
	F(leaves_big_oak) \
	F(leaves_birch) \
	F(leaves_jungle) \
	F(leaves_oak) \
	F(leaves_spruce) \
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
	F(glass_white) \
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
	F(ice) \
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
	F(water15) \
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
	B

const char* block_texture_name[] = BlockTextures({, FuncStr, });
static const uint block_texture_count = BlockTextures(0, FuncCount, +0);
static_assert(block_texture_count <= 65535, "");
enum class BlockTexture : uint16_t BlockTextures({, FuncList, });

bool is_leaves(BlockTexture a) { return a >= BlockTexture::leaves_acacia && a <= BlockTexture::leaves_spruce; }
bool is_leaves(Block a) { return a >= Block::leaves_acacia && a <= Block::leaves_spruce; }
bool is_sand(Block a) { return a == Block::sand || a == Block::red_sand; }
bool is_water(Block a) { return a >= Block::water1 && a <= Block::water15; }
bool is_water_partial(Block a) { return a >= Block::water1 && a < Block::water15; }
bool can_move_through(Block block) { return block == Block::none || is_water(block); }
bool can_see_through_non_water(Block block) { return block == Block::none || is_leaves(block) || block == Block::ice || block == Block::glass_white; }
bool can_see_through(Block block) { return can_see_through_non_water(block) || is_water(block); }
bool accepts_water(Block b) { return is_water_partial(b) || b == Block::none; }

int water_level_strict(Block b) { assert(b >= Block::water1 && b <= Block::water15); return int(b) - int(Block::water1) + 1; }
int water_level(Block b) { return (b == Block::none) ? 0 : water_level_strict(b); }

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
	S1(water15, water_still);
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

namespace Cube
{
	glm::ivec3 corner[8];
	const int faces[6][4] = { { 0, 4, 6, 2 }/*xmin*/, { 1, 3, 7, 5 }/*xmax*/, { 0, 1, 5, 4 }/*ymin*/, { 2, 6, 7, 3 }/*ymax*/, { 0, 2, 3, 1 }/*zmin*/, { 4, 5, 7, 6 }/*zmax*/ };
	Initialize { FOR(i, 8) corner[i] = glm::ivec3(i&1, (i>>1)&1, (i>>2)&1); }
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
		if (q < -0.35) return Block::ice_packed;
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
	void set(glm::ivec3 a, Block b) { m_block[z_order<ChunkSizeBits>(a)] = b; }
private:
	Block* m_block;
};

struct MapChunk
{
	MapChunk() : m_block(nullptr), m_count(0) { }

	void reset(Block* block)
	{
		m_block = block;
		m_count = 0;
		FOR(i, ChunkSize3) if (m_block[i] != Block::none) m_count += 1;
	}

	Block operator[](glm::ivec3 a) { return m_block[index(a)]; }

	bool valid() { return m_block != 0; }
	bool empty() { return m_count == 0; }

	void set(glm::ivec3 a, Block b)
	{
		int i = index(a);
		Block q = m_block[i];
		if (b != Block::none)
		{
			if (q == Block::none) m_count += 1;
		}
		else
		{
			if (q != Block::none) m_count -= 1;
		}
		m_block[i] = b;
	}

private:
	static int index(glm::ivec3 a) { return z_order<ChunkSizeBits>(a); }

protected:
	int m_count; // number of non-empty blocks
	Block* m_block;
};

void generate_chunk(MapChunkLight& mc, glm::ivec3 cpos)
{
	heightmap->Populate(cpos.x, cpos.y);
	FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
	{
		glm::ivec3 v(x, y, z);
		mc.set(v, generate_block(cpos * ChunkSize + v));
	}
}

// =======================

struct SuperChunk
{
	static const uint BlockCubeFileSize = (1 << (3 * (ChunkSizeBits + SuperChunkSizeBits))) * sizeof(Block);
	typedef BitCube<(1 << SuperChunkSizeBits)> BitCubeExplored;

	glm::ivec3 scpos;
	int refs;
	ArrayFile<BlockCubeFileSize + sizeof(BitCubeExplored)> file;

	BitCubeExplored& explored() { return *reinterpret_cast<BitCubeExplored*>(reinterpret_cast<uint8_t*>(file.data()) + BlockCubeFileSize); }
	Block* chunk(glm::ivec3 cpos) { return reinterpret_cast<Block*>(file.data()) + ChunkSize3 * z_order<SuperChunkSizeBits>(cpos); }
};

#include <unordered_map>

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
	Block* acquire_chunk(glm::ivec3 cpos, bool generate)
	{
		AutoLock(m_lock);
		glm::ivec3 scpos = cpos >> SuperChunkSizeBits;
		SuperChunk* sc = m_map[scpos];
		if (sc == nullptr)
		{
			sc = new SuperChunk;
			sc->scpos = scpos;
			sc->refs = 0;
			if (!sc->file.open("world", scpos, "sc")) exit(1);
			m_map[scpos] = sc;
		}
		sc->refs += 1;

		Block* blocks = sc->chunk(cpos & SuperChunkSizeMask);
		m_lock.unlock();
		AutoVecLock _(cpos, chunk_locks);
		m_lock.lock();

		if (!sc->explored()[cpos & SuperChunkSizeMask])
		{
			if (!generate) return nullptr;
			m_lock.unlock();
			MapChunkLight mc(blocks);
			generate_chunk(mc, cpos);
			m_lock.lock();
			sc->explored().set(cpos & SuperChunkSizeMask);
		}

		return blocks;
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
			sc->file.save();
			sc->file.close();
			delete sc;
			m_map.erase(m_map.find(scpos));
		}
	}

	void save()
	{
		AutoLock(m_lock);
		for (auto it : m_map)
		{
			it.second->file.save();
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
	glm::u8vec3 pos[4];
	BlockTexture texture;
	uint8_t light;
	uint16_t plane;
};

bool operator<(const Quad& a, const Quad& b)
{
	if (a.texture != b.texture) return a.texture < b.texture;
	return a.plane < b.plane;
}

struct Vertex
{
	BlockTexture texture;
	uint8_t light;
	glm::u8vec2 uv;
	glm::u8vec3 pos;
};

struct WVertex
{
	BlockTexture texture;
	uint8_t light;
	glm::u16vec2 uv;
	glm::u16vec3 pos;
};

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

	void sort(glm::ivec3 center)
	{
		auto cmp = [center](glm::ivec3 a, glm::ivec3 b) { return glm::distance2(a, center) > glm::distance2(b, center); };
		std::sort(array.begin(), array.end(), cmp);
	}

	glm::ivec3* begin() { return &array[0]; }
	glm::ivec3* end() { return begin() + array.size(); }

private:
	BitCube<MapSize> set;
	std::vector<glm::ivec3> array;
};

struct Chunk;
static const glm::ivec3 face_dir[6] = { -ix, ix, -iy, iy, -iz, iz };

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

struct BlockRenderer
{
	std::vector<Quad> m_quads;
	glm::ivec3 m_pos;
	glm::ivec3 m_fpos;
	Block m_block;

	std::vector<Quad> m_xy, m_yx;

	MapChunk* mc;
	CachedChunk* cc;

	void draw_quad(int face, bool reverse, bool underwater_overlay);
	void draw_quad(int face, int zmin, int zmax, bool reverse, bool underwater_overlay = false);

	void draw_face(bool cond, int face)
	{
		Block q = cond ? (*mc)[m_pos + face_dir[face]] : cc[face][m_fpos + face_dir[face]];
		if (q == Block::water15)
		{
			draw_quad(face, false, true);
		}
		else if (is_water(q) && face < 4)
		{
			if (q != m_block)
			{
				int w = water_level_strict(q);
				draw_quad(face, 0, w, false, true);
				draw_quad(face, w, 15, false, false);
			}
		}
		else
		{
			if (can_see_through(q) && q != m_block)
			{
				draw_quad(face, false, false);
			}
		}
	}

	void draw_water_side(bool cond, int face, int w)
	{
		Block q = cond ? (*mc)[m_pos + face_dir[face]] : cc[face][m_fpos + face_dir[face]];
		if (is_water_partial(q))
		{
			int w2 = water_level_strict(q);
			if (w > w2)
			{
				draw_quad(face, w2, w, false);
				draw_quad(face, w2, w, true);
			}
		}
		else if (can_see_through_non_water(q))
		{
			draw_quad(face, 0, w, false);
			draw_quad(face, 0, w, true);
		}
	}

	void draw_water_bottom()
	{
		Block q = (m_pos.z != CMin) ? (*mc)[m_pos - iz] : cc[4][m_fpos - iz];
		if (q != Block::water15 && can_see_through(q)) draw_quad(4, false, false);
		if (q == Block::none) draw_quad(4, true, false);
	}

	void draw_water_top(int w)
	{
		if (m_block == Block::water15)
		{
			Block q = (m_pos.z != CMax) ? (*mc)[m_pos + iz] : cc[5][m_fpos + iz];
			if (can_see_through_non_water(q)) draw_quad(5, false, false);
			if (q == Block::none) draw_quad(5, true, false);
		}
		else
		{
			draw_quad(5, w, w, false);
			draw_quad(5, w, w, true);
		}
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

	void merge_quads(std::vector<Vertex>& vertices)
	{
		std::sort(m_quads.begin(), m_quads.end());
		auto a = m_quads.begin(), w = a;
		while (a != m_quads.end())
		{
			auto b = a + 1;
			while (b != m_quads.end() && a->texture == b->texture && a->plane == b->plane) b += 1;

			if (is_leaves(a->texture))
			{
				while (a < b) *w++ = *a++;
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
			w = std::copy(m_xy.begin(), m_xy.end(), w);
		}
		m_quads.resize(w - m_quads.begin());

		vertices.resize(6 * m_quads.size());
		Vertex* e = vertices.data();
		for (Quad q : m_quads)
		{
			glm::u8vec3 d = q.pos[2] - q.pos[0];
			uint u, v;
			uint face = q.plane >> 8;
			if (face < 2) { u = d.y; v = d.z; }
			else if (face < 4) { u = d.x; v = d.z; }
			else { u = d.y; v = d.x; }

			// How much to rotate each face?
			int a = (face == 0 || face == 3 || face == 5) ? 0 : 1;
			*e++ = { q.texture, q.light, glm::u8vec2(u, 0), q.pos[(1+a)%4] };
			*e++ = { q.texture, q.light, glm::u8vec2(0, 0), q.pos[(2+a)%4] };
			*e++ = { q.texture, q.light, glm::u8vec2(0, v), q.pos[(3+a)%4] };
			*e++ = { q.texture, q.light, glm::u8vec2(u, 0), q.pos[(1+a)%4] };
			*e++ = { q.texture, q.light, glm::u8vec2(0, v), q.pos[(3+a)%4] };
			*e++ = { q.texture, q.light, glm::u8vec2(u, v), q.pos[(0+a)%4] };
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

struct Chunk : public MapChunk
{
	Chunk() : m_cpos(NullChunk) { }

	void buffer(BlockRenderer& renderer)
	{
		AutoLock(m_buffer_mutex);
		release(m_vertices);
		renderer.m_quads.clear();
		if (!empty())
		{
			glm::ivec3 cpos = get_cpos();
			CachedChunk cc[6];
			renderer.cc = cc;
			renderer.mc = this;
			FOR(x, ChunkSize) FOR(y, ChunkSize) FOR(z, ChunkSize)
			{
				glm::ivec3 p(x, y, z);
				Block block = operator[](p);
				if (block == Block::none) continue;
				renderer.m_pos = p;
				renderer.m_fpos = p + (cpos << ChunkSizeBits);
				renderer.m_block = block;

				if (block >= Block::water1 && block <= Block::water15)
				{
					int w = uint(block) - uint(Block::water1) + 1;
					renderer.draw_water_side(x != CMin, 0, w);
					renderer.draw_water_side(x != CMax, 1, w);
					renderer.draw_water_side(y != CMin, 2, w);
					renderer.draw_water_side(y != CMax, 3, w);
					renderer.draw_water_bottom();
					renderer.draw_water_top(w);
				}
				else
				{
					renderer.draw_face(x != CMin, 0);
					renderer.draw_face(x != CMax, 1);
					renderer.draw_face(y != CMin, 2);
					renderer.draw_face(y != CMax, 3);
					renderer.draw_face(z != CMin, 4);
					renderer.draw_face(z != CMax, 5);
				}
			}
		}
		renderer.merge_quads(m_vertices);
		m_render_size = m_vertices.size();
	}

	int render()
	{
		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * m_vertices.size(), &m_vertices[0], GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, m_render_size);
		return m_render_size;
	}

	void init(glm::ivec3 cpos, BlockRenderer& renderer)
	{
		assert(unloaded());

		assert(!valid());
		// TODO if (m_buffer) g_scm.release_chunk();
		reset(g_scm.acquire_chunk(cpos, true));
		m_cpos = compress_ivec3(cpos);
		buffer(renderer);
		m_renderable = true;
		m_active = true;
	}

	void unload() { m_cpos = NullChunk; m_renderable = false; }
	bool unloaded() { return m_cpos == NullChunk; }
	bool renderable() { return m_renderable; }

	glm::ivec3 get_cpos() { return decompress_ivec3(m_cpos); }

	bool m_active;
	friend class Chunks;
private:
	std::atomic<CompressedIVec3> m_cpos;
	std::atomic<bool> m_renderable;

	std::mutex m_buffer_mutex; // Protects m_vertices and m_render_size
	std::vector<Vertex> m_vertices;
	int m_render_size;
};

Console console;

namespace player
{
//	glm::vec3 position = glm::vec3(MoonCenter) + glm::vec3(0, 0, MoonRadius + 10);
	glm::vec3 position(0, 0, 20);
	std::atomic<CompressedIVec3> cpos(compress_ivec3(glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits));
	float yaw = 0;
	float pitch = 0;
	glm::mat4 orientation;
	//float* m = glm::value_ptr(player::orientation);
	//glm::vec3 forward(m[4], m[5], m[6]);
	//glm::vec3 right(m[0], m[1], m[2]);
	glm::vec3 velocity;
	bool flying = true;
}

class Chunks
{
public:
	static const int Threads = 7;

	Chunks(): m_map(new Chunk[MapSize * MapSize * MapSize]) { }

	void start_threads()
	{
		FOR(i, Threads)
		{
			std::thread t(loader_thread, this, i);
			t.detach();
		}
	}

	Block get_block(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		assert(chunk.get_cpos() == (pos >> ChunkSizeBits));
		return chunk[pos & ChunkSizeMask];
	}

	bool selectable_block(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		return chunk.get_cpos() == (pos >> ChunkSizeBits) && chunk[pos & ChunkSizeMask] != Block::none;
	}

	bool can_move_through(glm::ivec3 pos)
	{
		Chunk& chunk = get(pos >> ChunkSizeBits);
		return chunk.get_cpos() == (pos >> ChunkSizeBits) && ::can_move_through(chunk[pos & ChunkSizeMask]);
	}

	void reset(glm::ivec3 cpos, BlockRenderer& renderer)
	{
		Chunk& c = get(cpos);
		if (c.get_cpos() != cpos) return;
		c.m_renderable = false;
		c.buffer(renderer);
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
		while (true)
		{
			glm::ivec3 cpos = decompress_ivec3(player::cpos);
			if (cpos == last_cpos)
			{
				usleep(200);
				continue;
			}

			int q = 0;
			for (int i = 0; i < render_sphere.size(); i++)
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
};

// TODO: main thread should be marking chunks for unload and Chunks should release references back to SuperChunkManager!

Chunks g_chunks;

void CachedChunk::init(glm::ivec3 cpos)
{
	initialized = true;
	Block* blocks = g_scm.acquire_chunk(cpos, false);
	if (blocks)
	{
		release_cpos = cpos;
		mc.reset(blocks);
	}
}

Block CachedChunk::operator[](glm::ivec3 pos)
{
	if (!initialized) init(pos >> ChunkSizeBits);
	if (mc.valid()) return mc[pos & ChunkSizeMask];
	return generate_block(pos);
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
		Block block = (*chunk)[voxel & ChunkSizeMask];
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
	glm::ivec3 origin = glm::ivec3(glm::floor(player::position * k));

	int64_t budget = 40 / Timestamp::milisec_per_tick;
	Timestamp ta;

	int q = 0;
	while (rays_remaining > 0)
	{
		glm::ivec3 dir = directions[ray_it++];
		if (ray_it == directions.size()) ray_it = 0;
		rays_remaining -= 1;
		if (frustum.contains_point(player::position + glm::vec3(dir))) raytrace(chunk, origin, dir);
		if ((q++ % 2048) == 0 && ta.elapsed() > budget) break;
	}
	visible_chunks.sort(cpos);
}

namespace stats
{
	int vertex_count = 0;
	int chunk_count = 0;

	float frame_time_ms = 0;
	float render_time_ms = 0;
	float model_time_ms = 0;
	float raytrace_time_ms = 0;

	float collide_time_ms = 0;
	float select_time_ms = 0;
	float simulate_time_ms = 0;
	float rebuild_time_ms = 0;
}

// Model

glm::mat4 perspective;
glm::mat4 perspective_rotation;

glm::ivec3 sel_cube;
int sel_face;
bool selection = false;
bool wireframe = false;
bool show_counters = true;

Block carousel_block = Block(1);
double scroll_y = 0, scroll_dy = 0;
Timestamp ts_carousel;
bool show_carousel = false;

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
	c.buffer(renderer);
	activate_block(pos);

	if (p.x == 0) g_chunks.reset(cpos - ix, renderer);
	if (p.y == 0) g_chunks.reset(cpos - iy, renderer);
	if (p.z == 0) g_chunks.reset(cpos - iz, renderer);
	if (p.x == ChunkSizeMask) g_chunks.reset(cpos + ix, renderer);
	if (p.y == ChunkSizeMask) g_chunks.reset(cpos + iy, renderer);
	if (p.z == ChunkSizeMask) g_chunks.reset(cpos + iz, renderer);
}

bool digging_on = false;
Timestamp digging_start;
glm::ivec3 digging_block;

void on_key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	show_carousel = false;
	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		g_scm.save();
		glfwSetWindowShouldClose(window, GL_TRUE);
		return;
	}

	if (console.IsVisible())
	{
		if (action == GLFW_PRESS || action == GLFW_REPEAT)
		{
			if (console.OnKey(key, mods))
			{
				// handled by console
			}
			else if (key == GLFW_KEY_F2)
			{
				console.Hide();
			}
		}
	}
	else if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_F2)
		{
			console.Show();
		}
		else if (key == GLFW_KEY_F3)
		{
			wireframe = !wireframe;
		}
		else if (key == GLFW_KEY_F6)
		{
			show_counters = !show_counters;
		}
		else if (key == GLFW_KEY_TAB)
		{
			player::flying = !player::flying;
			player::velocity = glm::vec3(0, 0, 0);
			digging_on = false;
		}
	}
}

void on_mouse_button(GLFWwindow* window, int button, int action, int mods)
{
	show_carousel = false;

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && selection)
	{
		if (player::flying)
		{
			edit_block(sel_cube, Block::none);
		}
		else
		{
			digging_on = true;
			digging_start = Timestamp();
			digging_block = sel_cube;
		}
	}

	if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT)
	{
		digging_on = false;
	}

	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT && selection)
	{
		glm::ivec3 dir[] = { -ix, ix, -iy, iy, -iz, iz };
		edit_block(sel_cube + dir[sel_face], carousel_block);
	}
}

void on_scroll(GLFWwindow* window, double x, double y)
{
	ts_carousel = Timestamp();
	show_carousel = true;
	scroll_dy = y / 5;
	scroll_y += scroll_dy;
	if (scroll_y < 0) scroll_y = 0;
	if (scroll_y > (block_count - 2)) scroll_y = (block_count - 2);
	carousel_block = Block(uint(std::round(scroll_y)) + 1);
}

void model_init(GLFWwindow* window)
{
	glfwSetKeyCallback(window, on_key);
	glfwSetMouseButtonCallback(window, on_mouse_button);
	glfwSetScrollCallback(window, on_scroll);
	glfwSetCursorPos(window, 0, 0);
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

int Resolve(glm::vec3& center, float k, float x, float y, float z)
{
	glm::vec3 d(x, y, z);
	float ds = sqr(d);
	if (ds < 1e-6) return -1;
	center += d * (k / sqrtf(ds) - 1);
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

glm::vec3 gravity(glm::vec3 pos)
{
	return glm::vec3(0, 0, -15);

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

void simulate(float dt, glm::vec3 dir)
{
	if (!player::flying)
	{
		player::velocity += gravity(player::position) * dt;
	}
	player::position += dir * ((player::flying ? 20 : 10) * dt) + player::velocity * dt;

	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	FOR(i, 2)
	{
		// Resolve all collisions simultaneously
		glm::vec3 sum(0, 0, 0);
		int c = 0;
		FOR2(x, p.x - 3, p.x + 3) FOR2(y, p.y - 3, p.y + 3) FOR2(z, p.z - 3, p.z + 3)
		{
			glm::ivec3 cube(x, y, z);
			if (!g_chunks.can_move_through(cube) && 1 == sphere_vs_cube(player::position, 0.9, cube, cube_neighbors(cube)))
			{
				sum += player::position;
				c += 1;
			}
		}
		if (c == 0) break;
		player::velocity = glm::vec3(0, 0, 0);
		player::position = sum / (float)c;
	}
}

void model_move_player(GLFWwindow* window, float dt)
{
	glm::vec3 dir(0, 0, 0);

	float* m = glm::value_ptr(player::orientation);
	glm::vec3 forward(m[4], m[5], m[6]);
	glm::vec3 right(m[0], m[1], m[2]);

	if (!player::flying)
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

	if (!player::flying && glfwGetKey(window, GLFW_KEY_SPACE))
	{
		player::velocity = gravity(player::position) * -0.5f;
	}

	glm::vec3 p = player::position;
	if (dir.x != 0 || dir.y != 0 || dir.z != 0)
	{
		show_carousel = false;
		dir = glm::normalize(dir);
	}
	while (dt > 0)
	{
		if (dt <= 0.008)
		{
			simulate(dt, dir);
			break;
		}
		simulate(0.008, dir);
		dt -= 0.008;
	}
	if (p != player::position)
	{
		rays_remaining = directions.size();
		float* ma = glm::value_ptr(player::orientation);
		glm::vec3 direction(ma[4], ma[5], ma[6]);
		glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
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
	glm::ivec3 p = glm::ivec3(glm::floor(player::position));
	float* ma = glm::value_ptr(player::orientation);
	glm::vec3 dir(ma[4], ma[5], ma[6]);

	for (glm::ivec3 d : select_sphere)
	{
		glm::ivec3 cube = p + d;
		if (g_chunks.selectable_block(cube) && (sel_face = intersects_ray_cube(player::position, dir, cube)) != -1)
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
	static double last_cursor_x = 0;
	static double last_cursor_y = 0;
	if (cursor_x != last_cursor_x || cursor_y != last_cursor_y)
	{
		show_carousel = false;
		player::yaw += (cursor_x - last_cursor_x) / 150;
		player::pitch += (cursor_y - last_cursor_y) / 150;
		if (player::pitch > M_PI / 2 * 0.999) player::pitch = M_PI / 2 * 0.999;
		if (player::pitch < -M_PI / 2 * 0.999) player::pitch = -M_PI / 2 * 0.999;
		last_cursor_x = cursor_x;
		last_cursor_y = cursor_y;
		player::orientation = glm::rotate(glm::rotate(glm::mat4(), -player::yaw, glm::vec3(0, 0, 1)), -player::pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective, player::pitch, glm::vec3(1, 0, 0));
		perspective_rotation = glm::rotate(perspective_rotation, player::yaw, glm::vec3(0, 1, 0));
		perspective_rotation = glm::rotate(perspective_rotation, float(M_PI / 2), glm::vec3(-1, 0, 0));
		rays_remaining = directions.size();
	}
}

void model_digging(GLFWwindow* window)
{
	if (digging_on)
	{
		if (!selection)
		{
			digging_on = false;
			return;
		}

		if (sel_cube != digging_block)
		{
			digging_on = false;
			digging_block = sel_cube;
			digging_start = Timestamp();
		}
		else if (digging_start.elapsed_ms() > 3500)
		{
			edit_block(sel_cube, Block::none);
			digging_on = false;
			selection = select_cube(/*out*/sel_cube, /*out*/sel_face);
			if (selection)
			{
				digging_on = true;
				digging_block = sel_cube;
				digging_start = Timestamp();
			}
		}
	}
	else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS && selection)
	{
		digging_on = true;
		digging_block = sel_cube;
		digging_start = Timestamp();
	}
}

struct BlockRef
{
	Chunk* chunk;
	glm::i8vec3 ipos;
	Block block;

	operator Block() { return block; }
	BlockRef() { }
	explicit BlockRef(glm::ivec3 p) : chunk(g_chunks.get_opt(p >> ChunkSizeBits)), ipos(p & ChunkSizeMask), block(chunk ? (*chunk)[glm::ivec3(ipos)] : Block::none) { }
};

static const int SimulationDistance = 6; // in chunks

struct DirtyChunks
{
	enum { N = (SimulationDistance + 1/*neighbours*/) * 2 + 1 };
	glm::ivec3 origin;
	BitCube<N> set;
	std::vector<Chunk*> array;

	void add(glm::ivec3 a)
	{
		if (set.xset(a - origin)) array.push_back(&g_chunks.get(a));
	}
};

static const int MaxActiveChunks = 10;
std::vector<glm::ivec3> sim_active_chunks;
DirtyChunks sim_dirty_chunks;

void update_block(BlockRef& ref, Block b)
{
	ref.chunk->set(glm::ivec3(ref.ipos), b);
	ref.block = b;
	glm::ivec3 q(ref.ipos);
	glm::ivec3 p = ref.chunk->get_cpos();
	sim_dirty_chunks.add(p);
	// TODO: only if there is a block in neighbor chunk => mark neighbor as dirty
	if (q.x == CMin) sim_dirty_chunks.add(p - ix);
	if (q.x == CMax) sim_dirty_chunks.add(p + ix);
	if (q.y == CMin) sim_dirty_chunks.add(p - iy);
	if (q.y == CMax) sim_dirty_chunks.add(p + iy);
	if (q.z == CMin) sim_dirty_chunks.add(p - iz);
	if (q.z == CMax) sim_dirty_chunks.add(p + iz);
	activate_block(q + (p << ChunkSizeBits));
}

const char* block_name_safe(Block block)
{
	return ((int)block < block_count) ? block_name[(int)block] : "";
}

// returns true when src becomes empty
bool water_flow(BlockRef& src, BlockRef& dest, int w)
{
	assert(water_level(dest) + w <= 15);
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
	return m.chunk && m.block == Block::water15;
}

void model_simulate_water(BlockRef b, glm::ivec3 bpos)
{
	int w = water_level(b);

	// Flow down
	BlockRef m(bpos - iz);
	if (m.chunk && accepts_water(m.block))
	{
		int flow = std::min(w, 15 - water_level(m));
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
			int wb = water_level(m);
			if ((wb < w) && (w > 1 || water_under(m)))
			{
				int flow = (w - wb + 1) / 2;
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
			int wb = water_level(m);
			if ((wb < w) && (w > 1 || water_under(m)))
			{
				int flow = (w - wb + 1) / 2;
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
			update_block(b, Block::water15);
		}
		else
		{
			g_chunks.get(bpos >> ChunkSizeBits).m_active = true;
		}
	}
}

void model_simulate_blocks()
{
	if (player::flying) return;

	// TODO: fix SelectCube for non-cube models (like partial water)

	glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;

	sim_dirty_chunks.set.clear();
	sim_dirty_chunks.array.clear();
	sim_dirty_chunks.origin = cplayer - ii * (DirtyChunks::N / 2);

	sim_active_chunks.clear();
	for (glm::ivec3 d : simulation_sphere)
	{
		glm::ivec3 cpos = cplayer + d;
		Chunk& chunk = g_chunks.get(cpos);
		if (chunk.get_cpos() == cpos && chunk.m_active)
		{
			sim_active_chunks.push_back(cpos);
			chunk.m_active = false;
			if (sim_active_chunks.size() == MaxActiveChunks) break;
		}
	}

	// shuffle sim_order
	if (sim_active_chunks.size() > 0) FOR(i, ChunkSize2 / 4)
	{
		std::swap(sim_order[rand() % ChunkSize2], sim_order[rand() % ChunkSize2]);
	}

	Timestamp tb;
	for (glm::ivec3 cpos : sim_active_chunks)
	{
		FOR(z, ChunkSize) for (glm::i8vec2 xy : sim_order)
		{
			glm::ivec3 bpos = glm::ivec3(xy.x, xy.y, z) + (cpos << ChunkSizeBits);
			BlockRef b(bpos);
			if (is_water(b)) model_simulate_water(b, bpos);
			else if (is_sand(b))
			{
				// Flow down swapping with water
				BlockRef m(bpos - iz);
				if (m.chunk && (m == Block::none || is_water(m)))
				{
					Block e = m.block;
					update_block(m, b.block);
					update_block(b, e);
				}
			}
			else if (b == Block::water_source)
			{
				BlockRef m(bpos - iz);
				if (!m.chunk) continue;
				if (m == Block::none || is_water_partial(m))
				{
					int w = water_level(m);
					update_block(m, Block(int(Block::water1) + w));
				}
			}
			else if (b == Block::water_drain)
			{
				BlockRef m(bpos + iz);
				if (m.chunk && is_water(m))
				{
					int w = water_level(m);
					update_block(m, (w == 1) ? Block::none : Block(int(Block::water1) + w - 2));
				}
			}
			else if (b == Block::soul_sand)
			{
				// Morth water around it
				bool active = false;
				for (auto v : { -ix, ix, -iy, iy, -iz, iz })
				{
					BlockRef q(bpos + v);
					if (q.chunk && is_water(q)) { update_block(q, Block::soul_sand); active = true; }
				}
				if (!active && rand() % 10 == 0)
				{
					update_block(b, Block::none);
				}
				else
				{
					g_chunks.get(bpos >> ChunkSizeBits).m_active = true;
				}
			}
		}
	}

	Timestamp tc;
	static BlockRenderer renderer;
	for (Chunk* chunk : sim_dirty_chunks.array) chunk->buffer(renderer);
	Timestamp td;

	stats::simulate_time_ms = glm::mix<float>(stats::simulate_time_ms, tb.elapsed_ms(tc), 0.15f);
	stats::rebuild_time_ms = glm::mix<float>(stats::rebuild_time_ms, tc.elapsed_ms(td), 0.15f);
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
glm::vec3 light_direction(0.5, 1, -1);

GLubyte light_cache[8][8][8];

Initialize
{
	FOR(a, 8) FOR(b, 8) FOR(c, 8)
	{
		glm::vec3 normal = glm::normalize(glm::cross(glm::vec3(Cube::corner[b] - Cube::corner[a]), glm::vec3(Cube::corner[c] - Cube::corner[a])));
		float cos_angle = std::max<float>(0, -glm::dot(normal, light_direction));
		float light = glm::clamp<float>(0.4f + cos_angle * 0.6f, 0, 1);
		light_cache[a][b][c] = (uint) floorf(glm::clamp<float>(light * 256, 0, 255));
	}
}

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
GLuint block_position_loc;
GLuint block_block_texture_loc;
GLuint block_light_loc;
GLuint block_uv_loc;

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
	fprintf(stderr, "Loaded %s, %u x %u\n", filename, iwidth, iheight);

	assertf(m_width == iwidth, "m_wdith=%u iwidth=%u", m_width, iwidth);
	assertf((iheight % m_height) == 0, "iheight=%u m_height=%u", iheight, m_height);

	if (is_leaves(tex) || tex == BlockTexture::grass_top)
	{
		glm::u8vec4* pixel = (glm::u8vec4*)image;
		FOR(j, m_width * m_height)
		{
			pixel[j].r = 0;
			pixel[j].b = 0;
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
	float ta = glfwGetTime();
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

	light_direction = glm::normalize(light_direction);

	perspective = glm::perspective<float>(M_PI / 180 * 90, width / (float)height, 0.03, 1000);
	text = new Text;

	line_program = load_program("line");
	line_matrix_loc = glGetUniformLocation(line_program, "matrix");
	line_position_loc = glGetAttribLocation(line_program, "position");

	block_program = load_program("block");
	block_matrix_loc = glGetUniformLocation(block_program, "matrix");
	block_sampler_loc = glGetUniformLocation(block_program, "sampler");
	block_pos_loc = glGetUniformLocation(block_program, "pos");
	block_tick_loc = glGetUniformLocation(block_program, "tick");
	block_foglimit2_loc = glGetUniformLocation(block_program, "foglimit2");
	block_eye_loc = glGetUniformLocation(block_program, "eye");
	block_position_loc = glGetAttribLocation(block_program, "position");
	block_block_texture_loc = glGetAttribLocation(block_program, "block_texture_with_flag");
	block_light_loc = glGetAttribLocation(block_program, "light");
	block_uv_loc = glGetAttribLocation(block_program, "uv");

	glGenBuffers(1, &block_buffer);
	glGenBuffers(1, &line_buffer);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glClearColor(0.2, 0.4, 1, 1.0);
	glViewport(0, 0, width, height);

	g_chunks.start_threads();
}

void BlockRenderer::draw_quad(int face, bool reverse, bool underwater_overlay)
{
	Quad q;
	q.texture = get_block_texture(m_block, face);
	if (underwater_overlay) q.texture = BlockTexture((int)q.texture | (1 << 15));
	const int* f = Cube::faces[face];
	q.light = light_cache[f[0]][f[1]][f[2]];
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
	m_quads.push_back(q);
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
	q.light = light_cache[f[0]][f[1]][f[2]];
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
	m_quads.push_back(q);
}

const float foglimit2 = sqr(0.8 * RenderDistance * ChunkSize);

void render_world_blocks(const glm::mat4& matrix, const Frustum& frustum)
{
	glBindTexture(GL_TEXTURE_2D_ARRAY, block_texture);
	if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	Auto(if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));

	glUseProgram(block_program);
	glUniformMatrix4fv(block_matrix_loc, 1, GL_FALSE, glm::value_ptr(matrix));
	glUniform3fv(block_eye_loc, 1, glm::value_ptr(player::position));
	if (!player::flying) g_tick += 1;
	glUniform1i(block_tick_loc, g_tick);
	glUniform1f(block_foglimit2_loc, foglimit2);

	glBindBuffer(GL_ARRAY_BUFFER, block_buffer);
	glEnableVertexAttribArray(block_position_loc);
	glEnableVertexAttribArray(block_block_texture_loc);
	glEnableVertexAttribArray(block_light_loc);
	glEnableVertexAttribArray(block_uv_loc);

	glVertexAttribIPointer(block_position_loc, 3, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->pos);
	glVertexAttribIPointer(block_block_texture_loc, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), &((Vertex*)0)->texture);
	glVertexAttribIPointer(block_light_loc, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->light);
	glVertexAttribIPointer(block_uv_loc, 2, GL_UNSIGNED_BYTE, sizeof(Vertex), &((Vertex*)0)->uv);

	stats::chunk_count = 0;
	stats::vertex_count = 0;

	for (glm::ivec3 cpos : visible_chunks)
	{
		if (!frustum.is_sphere_outside(glm::vec3(cpos * ChunkSize + ChunkSize / 2), ChunkSize * BlockRadius))
		{
			Chunk& chunk = g_chunks.get(cpos);
			if (chunk.get_cpos() != cpos || !chunk.renderable()) continue;
			glm::ivec3 pos = cpos * ChunkSize;
			glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
			stats::vertex_count += chunk.render();
			stats::chunk_count += 1;
		}
	}
}

void render_gui()
{
	glm::mat4 matrix = glm::ortho<float>(0, width, 0, height, -1, 1);

	text->Reset(width, height, matrix);
	if (show_counters)
	{
		int raytrace = std::round(100.0f * (directions.size() - rays_remaining) / directions.size());
		text->Printf("[%.1f %.1f %.1f] C:%4d T:%3dk frame:%2.0f model:%1.0f [collide:%1.0f select:%1.0f simulate:%1.0f rebuild:%1.0f] raytrace:%2.0f %d%% render %2.0f",
			 player::position.x, player::position.y, player::position.z, stats::chunk_count, stats::vertex_count / 3000,
			 stats::frame_time_ms, stats::model_time_ms, stats::collide_time_ms, stats::select_time_ms, stats::simulate_time_ms, stats::rebuild_time_ms,
			 stats::raytrace_time_ms, raytrace, stats::render_time_ms);

		if (selection)
		{
			Block block = g_chunks.get_block(sel_cube);
			text->Printf("selected: %s at [%d %d %d]", block_name[(uint)block], sel_cube.x, sel_cube.y, sel_cube.z);
		}
		else
		{
			text->Print("", 0);
		}

		if (digging_on)
		{
			text->Printf("digging for %.1f seconds", digging_start.elapsed_ms() / 1000);
		}
	}

	console.Render(text, glfwGetTime());
	glUseProgram(0);

	if (show_carousel)
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
		glEnableVertexAttribArray(block_position_loc);
		glEnableVertexAttribArray(block_block_texture_loc);
		glEnableVertexAttribArray(block_light_loc);
		glEnableVertexAttribArray(block_uv_loc);

		glVertexAttribIPointer(block_position_loc, 3, GL_UNSIGNED_SHORT, sizeof(WVertex), &((WVertex*)0)->pos);
		glVertexAttribIPointer(block_block_texture_loc, 1, GL_UNSIGNED_SHORT, sizeof(WVertex), &((WVertex*)0)->texture);
		glVertexAttribIPointer(block_light_loc, 1, GL_UNSIGNED_BYTE, sizeof(WVertex), &((WVertex*)0)->light);
		glVertexAttribIPointer(block_uv_loc, 2, GL_UNSIGNED_SHORT, sizeof(WVertex), &((WVertex*)0)->uv);

		WVertex vertices[18 * (block_count - 1)];
		WVertex* e = vertices;
		FOR(i, block_count-1) FOR(face, 6)
		{
			if (face != 0 && face != 2 && face != 5) continue;

			const int* f = Cube::faces[face];
			uint8_t light = 255;
			if (face == 2) light = 127;
			if (face == 5) light = 127 + 64;
			if (face == 0) light = 255;

			glm::ivec3 w(128+i, 128-i, 128);
			glm::u16vec3 pos[4];
			FOR(j, 4) pos[j] = glm::u16vec3((w + Cube::corner[f[j]]) * 15);

			glm::u16vec3 d = pos[2] - pos[0];
			uint u, v;
			if (face < 2) { u = d.y; v = d.z; }
			else if (face < 4) { u = d.x; v = d.z; }
			else { u = d.y; v = d.x; }

			// How much to rotate each face?
			int a = (face == 0 || face == 3 || face == 5) ? 0 : 1;
			BlockTexture texture = get_block_texture(Block(i+1), face);
			*e++ = { texture, light, glm::u16vec2(u, 0), pos[(1+a)%4] };
			*e++ = { texture, light, glm::u16vec2(0, 0), pos[(2+a)%4] };
			*e++ = { texture, light, glm::u16vec2(0, v), pos[(3+a)%4] };
			*e++ = { texture, light, glm::u16vec2(u, 0), pos[(1+a)%4] };
			*e++ = { texture, light, glm::u16vec2(0, v), pos[(3+a)%4] };
			*e++ = { texture, light, glm::u16vec2(u, v), pos[(0+a)%4] };
		}

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);

		glm::ivec3 pos(0, 0, 0);
		glUniform3iv(block_pos_loc, 1, glm::value_ptr(pos));
		glBufferData(GL_ARRAY_BUFFER, sizeof(WVertex) * 18 * (block_count - 1), vertices, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, 18 * (block_count - 1));
		glUseProgram(0);

		glDisable(GL_BLEND);

		text->Reset(width, height, matrix);
		text->PrintAt(width / 2 - height / 40, height / 2 - height / 40, 0, block_name[(uint)carousel_block], strlen(block_name[(uint)carousel_block]));

		if (ts_carousel.elapsed_ms() >= 1000) show_carousel = false;
	}

	if (selection || show_carousel)
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

void stall_alarm_thread()
{
	while (true)
	{
		usleep(1000);
		Timestamp a = stall_ts;
		if (a.elapsed_ms() > 5000)
		{
			fprintf(stderr, "main thread stalled :(\n");
			// TODO print callstack of main thread!
			exit(1);
		}
	}
}

int main(int, char**)
{
	if (!glfwInit()) return 0;

	glm::dvec3 a;
	glm::i64vec3 b;
	a.x = glfwGetTime();
	b.x = rdtsc();
	usleep(200000);
	a.y = glfwGetTime();
	b.y = rdtsc();
	usleep(200000);
	a.z = glfwGetTime();
	b.z = rdtsc();

	glfwSetErrorCallback(OnError);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode->width*2, mode->height*2, "Arena", glfwGetPrimaryMonitor(), NULL);
	if (!window) { glfwTerminate(); return 0; }
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1/*VSYNC*/);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);

	model_init(window);
	render_init();

	glm::dvec3 c;
	glm::i64vec3 d;
	c.x = glfwGetTime();
	d.x = rdtsc();
	usleep(200000);
	c.y = glfwGetTime();
	d.y = rdtsc();
	usleep(200000);
	c.z = glfwGetTime();
	d.z = rdtsc();
	Timestamp::init(a, b, c, d);

	// Wait for 3x3 chunks around player's starting position to load
	glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
	Timestamp tq;
	FOR2(x, -1, 1) FOR2(y, -1, 1) FOR2(z, -1, 1)
	{
		while (true)
		{
			glm::ivec3 p(x, y, z);
			Chunk& chunk = g_chunks.get(p + cplayer);
			if (chunk.get_cpos() == p + cplayer) break;
			usleep(10000);
			glfwPollEvents();
			assert(tq.elapsed_ms() < 5000);
		}
	}

	Timestamp t0;
	stall_ts = t0;
	std::thread stall_alarm(stall_alarm_thread);

	while (!glfwWindowShouldClose(window))
	{
		Timestamp ta;
		stall_ts = ta;
		double frame_ms = t0.elapsed_ms(ta);
		t0 = ta;
		glfwPollEvents();
		model_frame(window, frame_ms);
		glm::mat4 matrix = glm::translate(perspective_rotation, -player::position);
		Frustum frustum(matrix);
		glm::ivec3 cplayer = glm::ivec3(glm::floor(player::position)) >> ChunkSizeBits;
		player::cpos = compress_ivec3(cplayer);

		Timestamp tb;
		update_render_list(cplayer, frustum);

		Timestamp tc;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		render_world_blocks(matrix, frustum);
		glDisable(GL_DEPTH_TEST);
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
	glfwTerminate();
	return 0;
}
