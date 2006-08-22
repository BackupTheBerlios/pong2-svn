#include "Ball.hpp"
#include "Framework.hpp"
#include <math.h>
#include <stdlib.h>
#include <iostream>

Ball::Ball(Framework* control)
: framework(control), radius(0.2), quad(NULL), displist(-1), spawntimer(-1), acceltimer(-1), scale(0.0),
  position(0.0, 0.0, 0.0), speed(0.0, 0.0, 0.0), zSpeed(0.0)
{
}

Ball::~Ball()
{
	if (quad != NULL) gluDeleteQuadric(quad);
	if (displist != -1) glDeleteLists(displist, 1);
}

void Ball::setPosition(const Vec3f& pos)
{
	position.x = pos.x;
	position.y = pos.y;
	position.z = pos.z;
}

void Ball::setSpeed(const Vec3f& spd)
{
	speed.x = spd.x;
	speed.y = spd.y;
	speed.z = spd.z;

	/* we have a new speed we want to achieve
		actually, it is yet achieved. but after bounces, it could get lower
	*/
	zSpeed = speed.z;
}

void Ball::action(Event event)
{
	if (event == GROW)
	{
		scale = double(spawndone) / (double)(spawndone + spawntodo);
		if (spawntodo == 0)
		{
			framework->removeTimer(spawntimer);
			spawntimer = -1;
		} else {
			spawndone++;
			spawntodo--;
		}
	} else if (event == SHRINK)
	{
		scale = double(spawntodo) / (double)(spawndone + spawntodo);
		if (spawntodo == 0)
		{
			framework->removeTimer(spawntimer);
			spawntimer = -1;
		} else {
			spawndone++;
			spawntodo--;
		}
	} else if (event == ACCELERATE)
	{
	}
}

void Ball::move(int ticks)
{
	Vec3f destination;

	// determine the minimum desired stepping
	double time = 1000.0 * 0.05f / std::max(fabs(speed.x), std::max(fabs(speed.y), fabs(speed.z)));

	// perhaps our system is fast enough to provide more than our minimum desired stepping
	if (ticks < time) time = ticks;
	for (int steps = 0; steps < ticks / (int)round(time); steps++)
	{
		destination.x = position.x + speed.x * time / 1000.0;
		destination.y = position.y + speed.y * time / 1000.0;
		destination.z = position.z + speed.z * time / 1000.0;

		Collision* collide = framework->detectCol(destination, speed, radius);

		if (collide == NULL) {
			position.x = destination.x;
			position.y = destination.y;
			position.z = destination.z;
		} else {
			position.x = collide->position.x;
			position.y = collide->position.y;
			position.z = collide->position.z;
			speed.x = collide->speed.x;
			speed.y = collide->speed.y;
			speed.z = collide->speed.z;

			delete collide;
		}
	}
}

void Ball::draw()
{
	if (displist == -1)
	{
		displist = glGenLists(1);
		glNewList(displist, GL_COMPILE);

		GLfloat mat_ambient[] = {0.22610818, 0.10305943, 0.0};
		GLfloat mat_diffuse[] = {0.90440222, 0.41225299, 0.0};
		GLfloat mat_specular[] = {0.09559777, 0.09559777, 0.09559777};
		GLfloat mat_emission[] = {0.0, 0.0, 0.0};
		GLfloat mat_shininess = 0.07812619;
		glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
		glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
		glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
		glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_emission);
		glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);

		if (quad == NULL) {
			quad = gluNewQuadric();
			gluQuadricNormals(quad, GLU_SMOOTH);
			//gluQuadricTexture(quad, GL_TRUE);
		}
		gluSphere(quad, radius, 32, 32);

		glEndList();
	}

	glPushMatrix();
	/* Move to position */
	glTranslatef(position.x, position.y, position.z);
	/* shrink/grow */
	glScalef(scale, scale, scale);

	glCallList(displist);
	glPopMatrix();
}

void Ball::shrink(int ticks)
{
	spawntodo = (int)round(ticks / 25);
	spawndone = 0;
	if (spawntimer != -1) { framework->removeTimer(spawntimer);
	}
		spawntimer = framework->addTimer(25, SHRINK, this);
}

void Ball::grow(int ticks)
{

	spawntodo = (int)round(ticks / 25);
	spawndone = 0;
	if (spawntimer != -1) {framework->removeTimer(spawntimer);
	}
		spawntimer = framework->addTimer(25, GROW, this);
}
