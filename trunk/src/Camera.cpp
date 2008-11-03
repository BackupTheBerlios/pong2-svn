#include "Camera.hpp"
#include <GL/gl.h>
#include <GL/glu.h>
#include <cmath>

Camera::Camera(unsigned int w, unsigned int h) : distance(7.75), angle(0.0, 0.0), mode(FOLLOW_PADDLE_REVERSE), width(w), height(h)
{
}

void Camera::init()
{
	/* Height / width ratio */
	GLfloat ratio;

	/* Protect against a divide by zero */
	if (height == 0) height = 1;

	ratio = (GLfloat)width / (GLfloat)height;

	/* Setup our viewport. */
	glViewport(0, 0, (GLsizei)width, (GLsizei)height);

	/* change to the projection matrix and set our viewing volume. */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	/* Set our perspective with a FOV of 60 */
	gluPerspective(60.0f, ratio, 0.01f, 100.0f);

	/* Make sure we'll operate on the model view and not the projection */
	glMatrixMode(GL_MODELVIEW);
}

void Camera::translate()
{
	glLoadIdentity();
	glTranslatef(0.0, 0.0, -distance);
	glRotatef(angle.y, 1.0, 0.0, 0.0);
	glRotatef(angle.x, 0.0, 1.0, 0.0);
}

void Camera::adjustAngle(double x, double y)
{
	angle.x += x;
	angle.y += y;
	angle.x = std::min(fabs(angle.x), 10.0) * (angle.x > 0 ? 1.0 : -1.0);
	angle.y = std::min(fabs(angle.y), 10.0) * (angle.y > 0 ? 1.0 : -1.0);
}

void Camera::adjustDistance(double dist)
{
	distance += dist;
	if (distance < 0.0) distance = 0.0;
	if (distance > 11.0) distance = 11.0;
}
