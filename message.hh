#pragma once

#include "block.hh"

enum class MessageType : uint8_t
{
	Text = 0,
	AvatarState = 1,
	ChunkState = 2,
	ServerStatus = 3
};

struct MessageText
{
	MessageType type;
	uint16_t size;
	char text[0]; // <size> bytes follow!
} __attribute__((packed));

struct MessageAvatarState
{
	MessageType type;
	uint8_t id;
	glm::vec3 position;
	float yaw, pitch;
} __attribute__((packed));

struct MessageChunkState
{
	MessageType type;
	glm::ivec3 cpos;
	Block blocks[ChunkSize3];
} __attribute__((packed));

struct MessageServerStatus
{
	MessageType type;
	uint8_t dummy;
	// All times in units of 100us
	uint16_t exchange_time;
	uint16_t inbox_time;
	uint16_t simulation_time;
	uint16_t chunk_time;
	uint16_t avatar_time;
	uint32_t frame;
} __attribute__((packed));

struct SocketBuffer;
MessageText* read_text_message(SocketBuffer& recv);
void write_text_message(SocketBuffer& send, const char* fmt, ...);
