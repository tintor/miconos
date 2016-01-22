#include "block.hh"
#include <cstdio>

const char* block_name[block_count] = EnumBlocks({, FuncStr, });
const char* block_texture_name[block_texture_count] = BlockTextures({, FuncStr, });

#define SC(A) case Block::A: return BlockTexture::A
#define S1(A, XYZ) case Block::A: return BlockTexture::XYZ
#define S2(A, XY, Z) case Block::A: return (face < 4) ? BlockTexture::XY : BlockTexture::Z;
#define S3(A, XY, ZMIN, ZMAX) case Block::A: return (face < 4) ? BlockTexture::XY : ((face == 4) ? BlockTexture::ZMIN : BlockTexture::ZMAX);

BlockTexture get_block_texture(Block block, int face)
{
	switch (block)
	{
	case Block::none: fprintf(stderr, "Block: %d Face: %d\n", static_cast<int>(block), face); FAIL;
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
		fprintf(stderr, "Block: %d Face: %d\n", static_cast<int>(block), face);
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
		fprintf(stderr, "Block: %d Face: %d\n", static_cast<int>(block), face);
		FAIL;
	case Block::pumpkin:
		if (face == 0) return BlockTexture::pumpkin_face_off;
		if (face == 1) return BlockTexture::pumpkin_side;
		if (face == 2) return BlockTexture::pumpkin_side;
		if (face == 3) return BlockTexture::pumpkin_side;
		if (face == 4) return BlockTexture::pumpkin_top;
		if (face == 5) return BlockTexture::pumpkin_top;
		fprintf(stderr, "Block: %d Face: %d\n", static_cast<int>(block), face);
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
	fprintf(stderr, "Block: %d Face: %d\n", static_cast<int>(block), face);
	FAIL;
}
#undef SC
#undef S2
#undef S3

