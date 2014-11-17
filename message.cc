#include "socket.hh"
#include "message.hh"

MessageText* read_text_message(SocketBuffer& recv)
{
	if (recv.size() < sizeof(MessageText)) return nullptr;
	MessageText* message = reinterpret_cast<MessageText*>(recv.data());
	assert(message->type == MessageType::Text);
	if (recv.size() < sizeof(MessageText) + (uint)message->size) return nullptr;
	recv.read_message(sizeof(MessageText) + (uint)message->size);
	return message;
}

void write_text_message(SocketBuffer& send, const char* fmt, ...)
{
	char buffer[1024];
	va_list va;
	va_start(va, fmt);
	int length = vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);
	assert(length < sizeof(buffer));

	MessageText message;
	message.type = MessageType::Text;
	message.size = length;
	send.ensure_space(sizeof(MessageText) + length);
	send.write(&message, sizeof(MessageText));
	send.write(&buffer, length);
}
