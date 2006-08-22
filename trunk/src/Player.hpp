#ifndef PLAYER_H
#define PLAYER_H

#include <string>
#include "stuff.hpp"
#include "Ball.hpp"

class Framework;

//! Player with paddle
/* Describes a player with name and score in the game, who controls a paddle,
   which is also fully included into this class. */
class Player : EventReceiver {
public:
	//! The constructor
	/*!	\param control the game's Framework (ie Server or Client) object
		\param nick the player's name
		\param where wether the paddle is in the FRONT or in the BACK
		\param z the absolute Z coordinate of the paddle; normally half of the field width
	*/
	Player(Framework* control, const std::string& nick, Side where, double z);
	//! The destructor
	~Player();

	//! create necessary timer(s)
	/*! does what can't be done while the object is on the stack */
	void run();

	//! returns the player name
	inline const std::string& getName() { return name; }
	//! returns the actual score
	inline int getScore() { return score; }
	//! returns FRONT or BACK
	inline Side getSide() { return side; }
	//! returns the actual position on the X and Y axis
	inline Vec2f getPosition() { return Vec2f(position.x, position.y); }

	//! set a new name
	inline void setName(const std::string& nick) { name = nick; }
	//! set a new score
	inline void setScore(int points) { score = points; }
	//! increase the score
	inline void incScore() { score++; }

	//! set the paddle size
	/*! used to let Mr. Wand cheat
		\param w width of the paddle
		\param h height of the paddle
	*/
	void setSize(double w, double h);
	//! set the paddle position
	/*! On the Client side the paddle doesn't move itself.
		\param x position coordinate
		\param y position coordinate
	*/
	void setPosition(double x, double y);

	//! move as desired by the user
	/*! This will test for barrieres and also against the maximum allowed speed of the paddle
		\param x relative desired X movement
		\param y relative desired Y movement
		\param time actual timestamp in ticks (ms) for speed calculations
	*/
	void move(double x, double y, unsigned int time);
	//! draw the paddle using GL functions
	void draw();

	//! test for collisions of a ball against the paddle
	/*!	\param bposition the position to test for
		\param bspeed the speed of the ball flying
		\param radius the ball's radius
		\result pointer to a Collision structure which provides the according data, NULL if there was no collision
	*/
	Collision* detectCol(const Vec3f& bposition, const Vec3f& bspeed, double radius);

	//! process a timer triggered event
	/*!	\param event the event descriptor
	*/
	void action(Event event);

	//! attach a Ball to the paddle
	/*! The ball is attached and moves along with the paddle until the player decides to release it.
	    The paddle will tell the Ball to set it's position whenever it moves.
	    The Ball's speed vector is set to 0, 0, 0 to prevent it from moving itself.
		\param ball pointer to the ball to attach
	*/
	void attachBall(Ball* ball);
	//! release any attached Ball
	/*! The ball will be released giving it new speed values determined by the paddle's own speed.
		\param zSpeed the Z axis speed to give to the ball on detaching
	*/
	void detachBall(double zSpeed);
private:
	//! "slow down" the paddle
	/*! As the paddle isn't really moved with a speed (so the paddle doesn't have to slow down first
	    when the user changes the movement direction) there is a speed value calculated from the last move.
	    This value is the maximum of the stored speed and the new speed. Using a timer which cales decelerate(),
	    the stored speed falls down continuously.
	*/
	void decelerate();

	//! the maximum possible paddle movement speed
	double maxspeed;

	//! pointer to the game's Framework (ie Server or Client) object
	Framework* framework;

	//! player nickname
	std::string name;
	//! actual score
	int score;
	//! wether the paddle is in the FRONT or in the BACK
	Side side;
	//! the position; saving both the dynamic X, Y coords and the predefined Z coord
	Vec3f position;
	//! speed, reasonably only X and Y
	Vec2f speed;
	//! width and height of the paddle
	double width, height;
	//! how thick the paddle is (bending)
	double thickness;

	//! timer used to decelerate
	int deceltimer;

	//! GL displaylist descriptor
	int displist;

	//! pointer to a Ball object ready to serve, if there is none, NULL
	Ball* attachedBall;

	//! timestamp in ticks of the last movement
	unsigned int lastmove;
};

#endif
