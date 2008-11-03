#include "Buffer.hpp"
#include <cstdlib>
#include <cstring>

Buffer::Buffer() : size(sizeof(PacketType)), freemem(true)
{
	data = (char*)malloc(size);
}

Buffer::Buffer(PacketType t) : size(sizeof(PacketType)), freemem(true)
{
	data = (char*)malloc(size);
	setType(t);
}

Buffer::Buffer(char* content, int bytes) : data(content), size(bytes), pos(sizeof(PacketType)), freemem(false)
{
	memcpy(&type, data, sizeof(PacketType));
}

Buffer::~Buffer()
{
	if (freemem) free(data);
}

void Buffer::setType(PacketType t)
{
	type = t;
	memcpy(data, &type, sizeof(PacketType));
}

void Buffer::pushInt(int value)
{
	data = (char*)realloc(data, size + sizeof(int));
	memcpy(data + size, &value, sizeof(int));
	size += sizeof(int);
}

void Buffer::pushDouble(double value)
{
	data = (char*)realloc(data, size + sizeof(double));
	memcpy(data + size, &value, sizeof(double));
	size += sizeof(double);
}

void Buffer::pushId(grapple_user value)
{
	data = (char*)realloc(data, size + sizeof(grapple_user));
	memcpy(data + size, &value, sizeof(grapple_user));
	size += sizeof(Side);
}

void Buffer::pushSide(Side value)
{
	data = (char*)realloc(data, size + sizeof(Side));
	memcpy(data + size, &value, sizeof(Side));
	size += sizeof(Side);
}

void Buffer::pushString(const std::string& str)
{
	data = (char*)realloc(data, size + str.size() + 1);
	memcpy(data + size, str.c_str(), str.size() + 1);
	size += str.size() + 1;
}

int Buffer::popInt()
{
	int value;
	memcpy(&value, data + pos, sizeof(int));
	pos += sizeof(int);
	return value;
}

double Buffer::popDouble()
{
	double value;
	memcpy(&value, data + pos, sizeof(double));
	pos += sizeof(double);
	return value;
}

grapple_user Buffer::popId()
{
	grapple_user value;
	memcpy(&value, data + pos, sizeof(grapple_user));
	pos += sizeof(grapple_user);
	return value;
}

Side Buffer::popSide()
{
	Side value;
	memcpy(&value, data + pos, sizeof(Side));
	pos += sizeof(Side);
	return value;
}

std::string Buffer::popString()
{
	char str[size - pos];
	memcpy(str, data + pos, size - pos);
	pos = size;
	return std::string(str);
}
