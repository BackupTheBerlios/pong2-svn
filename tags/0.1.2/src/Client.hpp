#ifndef CLIENT_H
#define CLIENT_H

#include "Framework.hpp"

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

	static grapple_client initNetwork(const std::string& version, const std::string& server, const unsigned short port, const std::string& playername);

private:
	//! process the player's desire to move on
	/*! Tells the server that the player wants to move
		\param x desired movement on the X axis
		\param y desired movement on the Y axis
		\param time the actual timestamp (ticks), ignored
	*/
	void movePaddle(double x, double y, unsigned int time);

	void updateGame(int ticks);

	//! unused, see Server
	inline void doScore(Side side) {};

	//! let the player kickoff the ball (notifies the Server)
	void serveBall();

	void ping();

	//! process messaging queue and send periodical (ping) packages
	void doNetworking();

	void sendPacket(Buffer& data, bool reliable);

	//! temporary placeholder for the player's name until the Player object is created (while data is transmitted)
	std::string playername;

	//! libgrapple client object, used for network communication
	grapple_client client;
};

#endif
