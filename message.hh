#pragma once

#include "block.hh"

// Network protocol
// first byte message type:
// - 0 general text message: 2 byte length followed by message string
// - 1 avatar state (struct)
// - 2 chunk state (struct)

enum class MessageType : uint8_t
{
	Text = 0,
	AvatarState = 1,
	ChunkState = 2,
	EditBlock = 3,
};

struct MessageAvatarState
{
	uint8_t id;
	glm::vec3 position;
	float yaw, pitch;
} __attribute__((packed));

struct MessageChunkState
{
	glm::ivec3 cpos;
	Block blocks[ChunkSize3];
} __attribute__((packed));

struct MessageEditBlock
{
	glm::ivec3 pos;
	Block block;
} __attribute__((packed));
