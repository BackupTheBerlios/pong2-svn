#ifndef BUFFER_H
#define BUFFER_H

#include "stuff.hpp"
#include <string>

//! the type of a network packet
enum Type {
	//! client asking for a server with version
	HELO,
	//! server responding it is full
	SERVER_FULL,
	//! server responding wrong version
	VERSION_MISMATCH,
	//! server responding ready for the game
	READY,
	//! telling a player's name
	PLAYERNAME,
	//! requesting paused state
	PAUSE_REQUEST,
	//! requesting non-paused state
	RESUME_REQUEST,
	//! server telling the ball's position
	BALLPOSITION,
	//! server telling a paddle's position
	PADDLEPOSITION,
	//! client requesting paddle movement
	PADDLEMOVE,
	//! server reporting a score
	SCORE,
	//! server telling the actual round
	ROUND,
	//! demanding and reporting the ball to be served
	SERVE_BALL,
	//! reporting the client that the ball is served by the server's player
	OPPOSITE_SERVE,
	//! telling one left the game
	QUIT,
};

//! Used to manage any incoming packet or create an outgoing packet
class Buffer
{
public:
	//! default constructor for an outgoing packet
	Buffer();
	//! constructor with type for an outgoing packet
	/*! \param t the type of the packet we want to send */
	Buffer(Type t);
	//! constructor for an incoming packet
	/*! store the packet's data in the Buffer to have easy access
		\param content the data itself
		\param bytes the data's size
	*/
	Buffer(char* content, int bytes);
	//! destructor cleaning up if the Buffer holds it's own memory field
	~Buffer();

	//! set the packet's type
	/*! \param t the type of the packet we want to send */
	void setType(Type t);
	//! add an integer value to the packet
	/*! \param value the value itself */
	void pushInt(int value);
	//! add a double value to the packet
	/*! \param value the value itself */
	void pushDouble(double value);
	//! add a Side value to the packet
	/*! \param value the value itself */
	void pushSide(Side value);
	//! add a variable sized string to the packet
	/*! This is only possible at the end of a packet.
	\param str the string */
	void pushString(const std::string& str);

	//! collect an integer value from the packet
	/*! \result the value itself */
	int popInt();
	//! collect a double value from the packet
	/*! \result the value itself */
	double popDouble();

	//! collect a Side value from the packet
	/*! \result the value itself */
	Side popSide();

	//! collect a string from the packet
	/*! The length is determined by the remaining data size
	\result the value itself */
	std::string popString();

	//! return the actually stored type, used for incoming packages
	inline Type getType() { return type; }
	//! return the data, used for outgoing packages
	inline char* getData() { return data; }
	//! return the data size, used for outgoing packages
	inline int getSize() { return size; }
private:
	//! wether we need to free the memory or it was allocated outside
	bool freemem;
	//! pointer to the data
	char* data;
	//! the data field size
	int size;
	//! actual position in the data field
	int pos;
	//! the packet type
	Type type;
};
#endif
