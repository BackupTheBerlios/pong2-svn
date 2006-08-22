#ifndef SERVER_H
#define SERVER_H

#include "Framework.hpp"

//! The Server is not only a network listening server but actually master of the gameflow.
/*! Even if this were a single player game (well, it's kind of hard against Mr. Wand)
    we would need the server as the instance caring about everything running right.
*/
class Server : Framework, EventReceiver
{
public:
	//! constructor which initiates the game against Mr.Wand
	/*!	\param surf pointer to the SDL video surface created in main.cpp
		\param conf a Configuration structure created in main.cpp and filled with configuration from the commandline
	*/
	Server(void *surf, const Configuration& conf);

	//! deconstructor, which tells perhaps connected clients about our suicide
	/* To prevent any concurrancy to break up stuff, this hardly kills the game with exit().
	   Note that due to the atexit() registration this will lead to a proper deinitialization of SDL & others.
	*/
	~Server();

private:
	//! process the player's desire to move on
	/*! The according Paddle is called to move itself and every Client is notified of the new position
		\param x desired movement on the X axis
		\param y desired movement on the Y axis
		\param time the actual timestamp (ticks) to determine the elapsed time value since the last move
	*/
	void movePaddle(double x, double y, unsigned int time);

	//! update the game state by moving the ball
	/*!  this will let the ball move and report the new position to every Client afterwards
		\param ticks elapsed time since the last call
	*/
	void updateGame(int ticks);

	//! process a timer triggered event
	/*!	\param event the event descriptor
	*/
	void action(Event event);

	//! value a score (ball went outside the field)
	/*! the score will be calculated, told to the Interface and every Client
		\param side where the ball went out - this means the player on the opposite will get honourated
	*/
	void score(Side side);

	//! let the player kickoff the ball
	void serveBall();

	//! process an incoming network packet
	/*! parses the packet and takes the appropriate actions
		\param sender the packet's originator
		\param data not NULL terminated char array with the data
		\param size the size of the data
	*/
	void receivePacket(Peer* sender, char* data, int size);

	//! descriptor (index) of the timer used when the ball flies out after a score
	int ballouttimer;
	//! the ball's initial Z axis speed when it get's served
	double ballspeed;

	//! the actual gaming round
	/*! Not only shown by Interface but also used to calculate who is the one to serve the ball next
	    Every player gets 5 serves at a time. */
	int round;
};

#endif
