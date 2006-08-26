#ifndef BALL_H
#define BALL_H

#include <GL/gl.h>
#include <GL/glu.h>
#include "stuff.hpp"

class Framework;

//! The ball is the most important piece of our game..
class Ball : public EventReceiver
{
public:
	//! The constructor
	/*!	\param control the game's Framework (ie Server or Client) object
	*/
	Ball(Framework *control);
	//! The destructor
	~Ball();

	//! returns the radius
	inline double getRadius() { return radius; }
	//! returns a reference to the position
	inline const Vec3f& getPosition() { return position; }
	//! returns a reference to the speed vector
	inline const Vec3f& getSpeed() { return speed; }

	//! set a new position
	/*! used by the Client
		\param pos reference to the new position
	*/
	void setPosition(const Vec3f& pos);
	//! set new speed values
	/*! \param spd reference to the new speed vector
	*/
	void setSpeed(const Vec3f& spd);

	//! process a timer triggered event
	/*!	\param event the event descriptor
	*/
	void action(Event event);

	//! move on
	/*! Let's the ball move for some time. It will ask for collisions itself.
		\param ticks the time evolved in ms
	*/
	void move(int ticks);
	//! draw the ball using GL functions
	void draw();

	//! shrink the ball
	/*! start the process of shrinking
		\param ticks duration of the scaling process in ms
	*/
	void shrink(int ticks);

	//! grow the ball
	/*! start the process of growing
		\param ticks duration of the scaling process in ms
	*/
	void grow(int ticks);
private:
	//! pointer to the game's Framework (ie Server or Client) object
	Framework *framework;

	//! radius of the ball
	double radius;
	//! the actual position
	Vec3f position;
	//! speed values for every axis
	Vec3f speed;
	//! minimum z speed the ball constantly accelerates to
	double zSpeed;
	//! pointer to our quadrik which builds the sphere
	GLUquadricObj *quad;
	//! descriptor of the utilized display list
	int displist;
	//! actual scaling (shrink/grow)
	double scale;
	//! describing the scaling progress
	int spawntodo, spawndone;
	//! reference to the timer used for scaling
	int spawntimer;
	//! reference to the timer used for accelerating
	int acceltimer;
};

#endif
