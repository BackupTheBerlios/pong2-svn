#ifndef CLIENT_H
#define CLIENT_H

#include "Framework.hpp"
#include "Buffer.hpp"

//! network game client
/*! The client can't do anything by its own. It's intended to give away all the work to the Server
    on the other end of the line.
*/
class Client : Framework
{
public:
	//! constructor which initiates the connecting process
	/*!	\param surf pointer to the SDL video surface created in main.cpp
		\param conf a Configuration structure created in main.cpp and filled with configuration from the commandline
	*/
	Client(void *surf, const Configuration& conf);

	//! deconstructor, which tells the perhaps connected server that we give up
	/* To prevent any concurrancy to break up stuff, this hardly kills the game with exit().
	   Note that due to the atexit() registration this will lead to a proper deinitialization of SDL & others.
	*/
	~Client();

private:
	//! process the player's desire to move on
	/*! Tells the server that the player wants to move
		\param x desired movement on the X axis
		\param y desired movement on the Y axis
		\param time the actual timestamp (ticks), ignored
	*/
	void movePaddle(double x, double y, unsigned int time);

	//! ping the server or whatever
	/*! As we don't have any work we use this regular call as an opportunity to manage our connection.
	    We could ping the server here to test the connection. Actually we only use it to send regular HELO packages
	    until we finally get connected. Therefore, the server can be started even if the client already is waiting.
		\param ticks elapsed time since the last call
	*/
	void updateGame(int ticks);

	//! unused, see Server
	void score(Side side);

	//! let the player kickoff the ball (notifies the Server)
	void serveBall();

	//! process an incoming network packet
	/*! parses the packet and takes the appropriate actions
		\param sender the packet's originator (here it should always be the server)
		\param data not NULL terminated char array with the data
		\param size the size of the data
	*/
	void receivePacket(Peer* sender, char* data, int size);

	//! helping pointer to the Server Peer object
	Peer* server;
	//! temporary placeholder for the player's name until the Player object is created (while data is transmitted)
	std::string playername;

	//! count of ticks (ms) since the last ping / sent HELO packet
	unsigned int ping;
};

#endif
