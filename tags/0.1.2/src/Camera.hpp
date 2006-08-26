#ifndef CAMERA_H
#define CAMERA_H

#include "stuff.hpp"

//! Handles the "camera", the viewing of the game
/*! Yet simple this one could be extended for better glory */
class Camera
{
public:
	//! viewing/following mode selectable by the user
	enum View {
		//! no automatic transformations are done
		FREE = 1,
		/*! the camera follows the paddle in reverse order to give
		    a better view of the paddle's surrounding area */
		FOLLOW_PADDLE_REVERSE = 2,
		//! the camera follows the paddle to resemble it's position
		FOLLOW_PADDLE = 3,
	};

	//! default constructor
	/*!	\param w width of the screen in pixels
		\param h height of the screen in pixels
	*/
	Camera(unsigned int w, unsigned int h);

	//! set up the viewport
	void init();

	//! returns the camera's distance to the field center
	inline double getDistance() { return distance; }
	//! returns the actual View mode
	inline View getMode() { return mode; }

	//! set the View mode
	inline void setMode(View selected) { mode = selected; }

	//! call the GL routines needed to setup the camera per frame
	void translate();
	//! alter the camera's viewing angle
	/*!	\param x relative adjustment on the X axis
		\param y relative adjustment on the Y axis
	*/
	void adjustAngle(double x, double y);
	//! alter the camera's distance to the field center
	/*! \param dist relative adjustment of the distance
	*/
	void adjustDistance(double dist);

private:
	//! the actual viewing mode
	View mode;
	//! the actual distance to the field center
	double distance;
	//! the angle of the camera
	Vec2f angle;

	//! screen width in pixels
	unsigned int width;
	//! screen height in pixels
	unsigned int height;
};

#endif
