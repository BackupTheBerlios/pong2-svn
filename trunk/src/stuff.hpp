#ifndef STUFF_H
#define STUFF_H
#include <string>
#include <sstream>

class Player;

//! the game configuration, which can mostly be altered by command line settings
struct Configuration {
	//! the constructor preinitializing default values
	inline Configuration() : version("9"),
		width(1024), height(768), bpp(32), fullscreen(false),
		playername("Hans"), mode(SERVER), servername(""), port(6642) {}
	//! the game's network protocol version (libgrapple wants a string here)
	std::string version;
	//! the screen size in pixels
	int width, height;
	//! the desired bits per pixel value (only used if available)
	int bpp;
	//! wether to start up in fullscreen mode or not
	bool fullscreen;
	//! what role to play (used on startup)
	enum Netmode {
		//! be a game controlling server
		SERVER,
		//! be a client to connect to a specific host
		CLIENT,
		//! be a client searching for servers (not implemented yet)
		BROADCAST
	} mode;
	//! the local player's name
	std::string playername;
	//! if in Client mode, the host to connect to
	std::string servername;
	//! the UDP networking port
	unsigned short port;
};

//! A side of the field
enum Side {
	//! just none
	NONE,
	//! in the front, where the local player acts
	FRONT,
	//! in the back, where the remote player acts
	BACK,
	//! wall of the field
	LEFT,
	//! wall of the field
	RIGHT,
	//! wall of the field
	TOP,
	//! wall of the field
	BOTTOM
};

//! a two dimensional double (not float) vector
struct Vec2f {
	//! default constructor
	inline Vec2f() {}
	//! constructor giving the values
	inline Vec2f(double xx, double xy)
	 : x(xx), y(xy) {}
	//! the two values itself
	double x, y;
};

//! a three dimensional double (not float) vector
struct Vec3f {
	//! default constructor
	inline Vec3f() {}
	//! constructor giving the values
	inline Vec3f(double xx, double xy, double xz)
	 : x(xx), y(xy), z(xz) {}
	//! the three values itself
	double x, y, z;
};

//! an occured collision of a ball against stuff
struct Collision {
	//! the ball's new position, where it got bounced
	Vec3f position;
	//! the ball's new speed values
	Vec3f speed;
	//! the opponent the ball bounced again, if there are any further policy questions
	void *opponent;
};

//! Any object which can receive timer triggered events
struct EventReceiver {
	//! the events descriptor
	enum Event {
		//! accelerate stuff! (used by Ball)
		ACCELERATE,
		//! decelerate stuff! (used by Player)
		DECELERATE,
		//! the ball got out of the field and needs to be reinserted
		BALLOUT,
		//! shrink the ball!
		SHRINK,
		//! grow the ball!
		GROW,
		//! decrease the alpha value of a flashing message
		DEFLASH
	};

	//! responsible for actions to be taken after the timer intervall is over
	virtual void action(Event event)=0;
};

#endif
