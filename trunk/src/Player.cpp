#include "Player.hpp"
#include "Framework.hpp"

#include <iostream>
#include <GL/gl.h>
#include <cmath>

Player::Player(Framework* control, const std::string& nick, Side where, double z)
 : framework(control), name(nick), side(where), position(0, 0, (where == FRONT ? 1.0 : -1.0) * z), width(1.0f), height(1.0f),
	speed(0, 0), maxspeed(0.01f), thickness(0.05), deceltimer(-1), attachedBall(NULL), displist(-1), lastmove(0)
{}

Player::~Player()
{
	if (deceltimer != -1) framework->removeTimer(deceltimer);
	if (displist != -1) glDeleteLists(displist, 1);
}

void Player::run()
{
	deceltimer = framework->addTimer(25, DECELERATE, this);
}

void Player::setSize(double w, double h)
{
	width = w;
	height = h;
	if (displist != -1)
	{
		glDeleteLists(displist, 1);
		displist = -1;
	}
}

void Player::setPosition(double x, double y)
{
	position.x = x;
	position.y = y;
}

void Player::move(double x, double y, unsigned int time)
{
	double timediff = (double)(time - lastmove);
	if (timediff < 1.0) return;
	lastmove = time;

	double speedx = std::min(maxspeed, fabs(x) / timediff) * (x > 0 ? 1 : -1);
	double speedy = std::min(maxspeed, fabs(y) / timediff) * (y > 0 ? 1 : -1);

	speed.x = std::max(fabs(speed.x), fabs(speedx)) * (speedx > 0 ? 1 : -1);
	speed.y = std::max(fabs(speed.y), fabs(speedy)) * (speedy > 0 ? 1 : -1);

	x = speedx * timediff;
	y = speedy * timediff;

	if (x < 0)
		position.x = framework->detectBarrier(position.x + x - width/2.0f, LEFT, side) + width/2.0f;
	else	position.x = framework->detectBarrier(position.x + x + width/2.0f, RIGHT, side) - width/2.0f;
	if (y < 0)
		position.y = framework->detectBarrier(position.y + y - height/2.0f, TOP, side) + height/2.0f;
	else	position.y = framework->detectBarrier(position.y + y + height/2.0f, BOTTOM, side) - height/2.0f;

	if (attachedBall != NULL)
		attachedBall->setPosition(Vec3f(position.x, position.y,
					position.z + (side == FRONT? -1.0 : 1.0) * (thickness * 5.0 + attachedBall->getRadius())));
}

void Player::draw()
{
	if (displist == -1)
	{
		displist = glGenLists(1);
		glNewList(displist, GL_COMPILE);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_COLOR, GL_ONE);
		if (side == FRONT)
		{
			GLfloat mat_ambient[] = {0.0, 0.5, 0.0};
			GLfloat mat_diffuse[] = {0.0, 0.5, 0.0};
			GLfloat mat_specular[] = {0.0, 0.0, 0.0};
			GLfloat mat_emission[] = {0.0, 0.0, 0.0};
			GLfloat mat_shininess = 0.07812619;
			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
			glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_emission);
			glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
		} else {
			GLfloat mat_ambient[] = {0.8, 0.0, 0.0};
			GLfloat mat_diffuse[] = {0.8, 0.0, 0.0};
			GLfloat mat_specular[] = {0.0, 0.0, 0.0};
			GLfloat mat_emission[] = {0.0, 0.0, 0.0};
			GLfloat mat_shininess = 0.07812619;
			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
			glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_emission);
			glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
		}

		Vec2f diff = Vec2f(width / 2.0f / 5.0f,	height / 2.0f / 5.0f);

		// this descibes the Z axis bending
		double zmod[5][5][2][2] =
		{
			4, 5, 2, 3,	2, 3, 1, 2,	1, 2, 1, 2,	1, 2, 2, 3,	2, 3, 4, 5,
			3, 4, 1, 2,	1, 2, 0, 1,	0, 1, 0, 1,	0, 1, 1, 2,	1, 2, 3, 4,
			3, 3, 1, 1,	1, 1, 0, 0,	0, 0, 0, 0,	0, 0, 1, 1,	1, 1, 3, 3,
			4, 3, 2, 1,	2, 1, 1, 0,	1, 0, 1, 0,	1, 0, 2, 1,	2, 1, 4, 3,
			5, 4, 3, 2,	3, 2, 2, 1,	2, 1, 2, 1,	2, 1, 3, 2,	3, 2, 5, 4,
		};
		double z = thickness * (side == FRONT ? 1.0 : -1.0);
		glTranslatef(0.0, 0.0, - 5.0 * z);
		glBegin(GL_QUADS);
			for (int x = -2; x < 3; x++)
			{
				for (int y = -2; y < 3; y++)
				{
					glVertex3f(x * diff.x*2.0 + diff.x, y * diff.y*2.0 - diff.y, zmod[x+2][y+2][0][0] * z);
					glVertex3f(x * diff.x*2.0 - diff.x, y * diff.y*2.0 - diff.y, zmod[x+2][y+2][0][1] * z);
					glVertex3f(x * diff.x*2.0 - diff.x, y * diff.y*2.0 + diff.y, zmod[x+2][y+2][1][1] * z);
					glVertex3f(x * diff.x*2.0 + diff.x, y * diff.y*2.0 + diff.y, zmod[x+2][y+2][1][0] * z);
				}
			}
		glEnd();
		glDisable(GL_BLEND);

		glEndList();
	}
	glPushMatrix();
	glTranslatef(position.x, position.y, position.z);
	glCallList(displist);
	glPopMatrix();
}

Collision* Player::detectCol(const Vec3f& bposition, const Vec3f& bspeed, double radius)
{
	// this function indeed is "work in progress"

	// simple tests:
	// wrong side
	if ((side == BACK) == (bposition.z > 0)) return NULL;

	//std::cout << fabs(bspeed.x) + fabs(bspeed.y) + fabs(bspeed.z) << std::endl;

	// cache the sign of our position (this is ! the sign of the ball speed)
	double zSign = (side == FRONT ? 1.0 : -1.0);

	// not in range (ball is completely in front of the paddle or yet half beyond the paddle)
	if ((bposition.z*zSign + radius < position.z*zSign - thickness*5.0)
	  ||(bposition.z*zSign > position.z*zSign))
	return NULL;

	// determine X,Y quad

	struct vec2i {
		int x, y;
	} quadnum = {
		(int)floor(fabs(bposition.x - position.x) / (width / 5.0f)),
		(int)floor(fabs(bposition.y - position.y) / (height / 5.0f))
	};

	// test if a quad is hit
	if ((quadnum.x > 3)||(quadnum.y > 3)) return NULL;
	// test if it could be with radius is missing   THIS HAS TO BE IMPROVED A LOT
	if ((quadnum.x == 3)&&(fabs(bposition.x-radius - position.x) > width/2.0)) return NULL;
	if ((quadnum.x == 3)&&(fabs(bposition.y-radius - position.y) > height/2.0)) return NULL;

	// a little hack:
	// if the ball already flies in the right direction, it get's ignored
	if ((side == BACK) == (bspeed.z > 0)) return NULL;

	double absz = position.z*zSign - thickness*(5.0 - (quadnum.x + quadnum.y));
	if (bposition.z*zSign + radius < absz)
		return NULL;

	// we call it a hit
	Collision* col = new Collision;
	// we should use a better way to provide new positions
	col->position.x = bposition.x;
	col->position.y = bposition.y;
	col->position.z = bposition.z;//absz * zSign;
	col->speed.x = bspeed.x + speed.x * 200.0 + quadnum.x * 0.2 * (bposition.x >= position.x ? 1.0 : -1.0);
	col->speed.y = bspeed.y + speed.y * 200.0 + quadnum.y * 0.2 * (bposition.y >= position.y ? 1.0 : -1.0);

	double zSpeed = bspeed.z * zSign;
	// we substract here what we added as quadnum values on x and y
	//zSpeed -= (quadnum.x + quadnum.y) * 0.25;
	//zSpeed = std::max(zSpeed, 0.1);
	col->speed.z = zSpeed * -zSign;
	std::cout << "*** HIT ***" << std::endl;

	return col;
}

void Player::attachBall(Ball* ball)
{
	attachedBall = ball;
	attachedBall->setSpeed(Vec3f(0.0, 0.0, 0.0));
	attachedBall->setPosition(Vec3f(position.x, position.y, position.z + (side == FRONT? -1.0 : 1.0) * (thickness * 5.0 + attachedBall->getRadius())));
	attachedBall->grow(500);
}

void Player::detachBall(double zSpeed)
{
	if (attachedBall != NULL)
	{
		attachedBall->setSpeed(Vec3f(speed.x * 500.0, speed.y * 500.0, (side == FRONT? -1.0 : 1.0) * zSpeed));
		attachedBall = NULL;
	}
}

void Player::decelerate()
{
	speed.x = (speed.x > 0.0 ? 1 : -1) * std::max(0.0, fabs(speed.x) - 0.001f);
	speed.y = (speed.x > 0.0 ? 1 : -1) * std::max(0.0, fabs(speed.y) - 0.001f);
	//std::cout << std::endl << name << ":\t " << speed.x << "\t " << speed.y << std::endl;
}

void Player::action(Event event)
{
	if (event == DECELERATE) {
		decelerate();
	}
}
