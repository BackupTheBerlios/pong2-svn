#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include <iostream>
#include <string>
#include <vector>
#include <map>

#include "SDL.h"
#include <GL/gl.h>
#include "grapple/grapple.h"

#include "stuff.hpp"
#include "Interface.hpp"
#include "Field.hpp"
#include "Ball.hpp"
#include "Player.hpp"
#include "Camera.hpp"
#include "Buffer.hpp"

class Ball;

class Player;

//! Framework provides the functionality of underlying libraries
/*! Both Server and Client are derived from Framework to have the necessary functionality handy
    and also to be called through pure virtual functions by the Framework code.
    Whenever the user presses a button, the Server has to do other stuff with it than the Client.
*/
class Framework {

public:
	//! holds a timer call
	struct TimerData {
		//! the event descriptor
		EventReceiver::Event event;
		//! the receiving object who demanded the timer
		EventReceiver* receiver;
		//! the timer itself provided by the SDL
		SDL_TimerID timer;
	};

	struct Peer {
		std::string name;
		Player* player;
		bool ready;

		inline Peer(std::string id)
		: name(id), player(NULL), ready(false) {}
		// needed by std::map, do not use
		inline Peer() {}
	};

	//! holds the actual networking state
	enum Networkstate {
		//! initialization isn't finished yet
		UNINITIALIZED,
		//! the server is waiting for a client to arrive
		WAITING,
		//! the client tries to connect to a server
		CONNECTING,
		//! server & client are exchanging data
		TRANSMITTING_DATA,
		//! a networked game is running
		RUNNING
	} state;

	//! shutdown the game
	/*! This will push a quit event and is called whenever we want to exit cleanly.
	   We need to exit that way because using exit() wouldn't call destructors,
	   therefore the opposite player wouldn't get told about quitting. */
	void shutdown();

	//! the constructor, called by Server or Client
	/*!	\param surf pointer to the SDL video surface created in main.cpp
		\param conf a Configuration structure created in main.cpp and filled with configuration from the commandline
		\param initial the desired initial networking state, actually WAITING from the Server, CONNECTING from the Client
	*/
	Framework(void *surf, const Configuration& conf, Networkstate initial);

	//! the destructor only frees left SDL timers and the TimerData structures
	~Framework();

	//! load a texture into OpenGL
	/*!	\param filename the file containing the texture (has to be readable by SDL_image)
		\result the number given to the texture for further binding
	*/
	GLuint loadTexture(const std::string& filename);

	//! test for collisions of a ball against the world
	/*!	\param position the position to test for
		\param speed the speed of the ball flying
		\param radius the ball's radius
		\result pointer to a Collision structure which provides the according data
	*/
	Collision* detectCol(const Vec3f& position, const Vec3f& speed, double radius);

	//! test for something in the way, paddle movement
	/*! only tests for one direction (x or y axis) at once
		\param dest where the paddle wants to go to
		\param direction the according direction
		\param side wether the paddle is in the FRONT or in the BACK
		\result how far the paddle can go without crossing a barriere
	*/
	double detectBarrier(double dest, int direction, Side side);

	//! add a timer (in fact SDL timer)
	/*!	\param intervall the intervall in ticks (ms) of the wanted timer
		\param event the event the timer should trigger
		\param receiver pointer to the receiving object
		\result the timer's number needed later to remove the timer
	*/
	int addTimer(unsigned int intervall, EventReceiver::Event event, EventReceiver* receiver);

	//! remove a previously added timer by it's index
	/*!	\param index the timer's number which was given by addTimer()
	*/
	void removeTimer(int index);

	inline const std::vector<Player*>& getPlayers() { return player; }
protected:
	//! enter the event loop, which indefinitely runs and processes events
	void loop();

	//! set/unset pause state
	/*! In pause mode, the mouse & keyboard input isn't grabbed and the game can't go on.
	    Pause mode is shared between Server and Client.
		\param pause wether to pause (true) or resume (false) - ignored if external is false
		\param external wether this was triggered by network or locally
	*/
	void togglePause(bool pause, bool external);

	virtual void ping()=0;

	//! process message queue, send periodical packages, called by loop()
	virtual void doNetworking()=0;

	virtual void sendPacket(Buffer& data, bool reliable)=0;

	void sendSimplePacket(PacketType t);

	//! our Camera object setting up the viewport
	Camera camera;

	//! our Interface object responsible for UI/HUD output
	Interface output;

	//! the playing field, 4 walls
	Field field;

	//! vector holding all involved balls.
	/*! Up till now we have only one ball.
	*/
	std::vector<Ball> ball;

	//! vector holding all involved players (with their paddles).
	/*! Up till now we have only two players.
		If noone is connected as client, the server provides the famous "Mr. Wand" called opponent.
		Better not try to beat him!
	*/
	std::vector<Player*> player;

	std::map<grapple_user, Peer> peer;
	grapple_user localid;

	//! pause state
	/*! Is 0 if not paused and otherwise holds the ticks (ms) which need to be processed after the pause. */
	unsigned int paused;
private:
	//! initialize GL
	void resetGL();

	//! draw the whole scene; called by loop() on every frame
	void drawScene();

	//! processe a pressed key, called by loop()
	void handleKeyPress(SDL_keysym *keysym);

	//! processe mouse movement, called by loop()
	/*! the button's state determines which action to take
		\param x relative movement, X axis
		\param y relative movement, Y axis
		\param buttons bitfield containing the buttons actually pressed
	*/
	void handleMouseMove(int x, int y, unsigned char buttons);

	//! move the paddle, called by handleMouseMove()
	virtual void movePaddle(double x, double y, unsigned int time)=0;
	//! update the game per frame, therefore called by loop()
	virtual void updateGame(int ticks)=0;
	//! process an occured score (the ball went out of the field), called by detectCol()
	virtual void doScore(Side side)=0;
	//! serve the ball, i.e. the player wants to kick it off, called by loop()
	virtual void serveBall()=0;

	/*! the SDL video surface */
	SDL_Surface *surface;

	//! vector holding the TimerData structures; they have to be held to remove timers and free them
	std::vector<TimerData*> timerdata;

	//! the minimum stepping in ticks (ms) - can be used to achieve a fps maximum setting
	unsigned int timeunit;
	//! time in ticks (ms) of the latest frame
	unsigned int lasttime;
	//! frames we did since the last fps calculation
	unsigned int frames;
	//! ticks (ms) since the last fps calculation
	unsigned int xdiff;
};

//! function given to every created timer to process it
/*! SDL needs a C function to call when the timer has finished one intervall.
    This one runs in another thread. so it pushes an event providing the necessary data to the SDL event queue.
    This event will be processed by loop(), inside of the Framework :)
*/
unsigned int processTimer(unsigned int intervall, void* data);

#endif
