#ifndef FIELD_H
#define FIELD_H

#include "stuff.hpp"
#include <GL/gl.h>

class Framework;

//! The playing field.
/*! Holds the fields constraints, calculates Collisions and wether the ball is inside or not. Provides a function to render the walls.
*/
class Field
{
public:
	//! The constructor
	/*!	\param control the game's Framework (ie Server or Client) object
	*/
	Field(Framework *control);

	//! test for collisions of a ball against the field walls
	/*!	\param position the position to test for
		\param speed the speed of the ball flying
		\param radius the ball's radius
		\result pointer to a Collision structure which provides the according data, NULL if there was no collision
	*/
	Collision* detectCol(const Vec3f& position, const Vec3f& speed, double radius);

	//! test wether the ball is inside of the field
	/*! called by the global collision detector to become aware of a score
		\param z the ball's position on the Z axis
		\result FRONT or BACK if the ball is outside, otherwise NONE
	*/
	Side zOutside(double z);

	//! draw the field using GL
	void draw();

	//! return a field's constraint
	inline double getWidth()  { return width; }
	//! return a field's constraint
	inline double getHeight() { return height; }
	//! return a field's constraint
	inline double getLength() { return length; }
private:
	//! pointer to the game's Framework (ie Server or Client) object
	Framework *framework;
	//! descriptor of the walls' texture
	GLuint wallTexture;
	//! descriptor of the GL display list
	int displist;

	//! constraint of the field
	double width, height, length;
};

#endif
