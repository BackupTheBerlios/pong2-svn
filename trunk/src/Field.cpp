#include "Field.hpp"
#include <math.h>
#include <iostream>
#include "Framework.hpp"

Field::Field(Framework *control)
: framework(control), displist(-1), width(4.0), height(4.0), length(8.0)
{
	wallTexture = framework->loadTexture("wall.bmp");
}

Collision* Field::detectCol(const Vec3f& position, const Vec3f& speed, double radius)
{
	Collision *col = NULL;

	// first we make the "simple" z coord test
	if (fabs(position.z) - radius > fabs(length/2.0))
		return col;

	// second we test against the planes

	// LEFT / RIGHT
	double sign = (speed.x < 0 ? -1.0 : 1.0);

	if (fabs(position.x) + radius >= width/2.0)
	{
		// we could extend here with a test of the according circle against the rectangular

		col = new Collision;
		col->position.y = position.y;
		col->position.z = position.z;
		col->speed.y = speed.y;
		col->speed.z = speed.z;

		col->position.x = (width/2.0 - radius) * sign;
		col->speed.x = -speed.x;
	}

	// TOP / BOTTOM
	sign = (speed.y < 0 ? -1.0 : 1.0);

	if (fabs(position.y) + radius >= height/2.0)
	{
		// we could extend here with a test of the according circle against the rectangular

		if (col == NULL) {
			col = new Collision;
			col->position.x = position.x;
			col->position.z = position.z;
			col->speed.x = speed.x;
			col->speed.z = speed.z;
		}
		col->position.y = (height/2.0 - radius) * sign;
		col->speed.y = -speed.y;
	}
	return col;
}

Side Field::zOutside(double z)
{
	if (z < -length/2.0 - 1.0) return BACK;
	if (z >  length/2.0 + 1.0) return FRONT;
	return NONE;
}

void Field::draw()
{
/*	GLfloat mat_ambient[] = {1.0, 1.0, 1.0};
	GLfloat mat_diffuse[] = {0.68456549, 0.68319218, 0.68613717};
	GLfloat mat_specular[] = {0.33476005, 0.33355458, 0.39387350};
	GLfloat mat_emission[] = {0.0, 0.0, 0.0};
	GLfloat mat_shininess = 0.07142748;
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_emission);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);*/
	if (displist == -1)
	{
		displist = glGenLists(1);
		glNewList(displist, GL_COMPILE);

		glBindTexture(GL_TEXTURE_2D, wallTexture);
		glBegin(GL_QUADS);
			// bottom
			glNormal3f(0.0f, 1.0f, 0.0f);
			glTexCoord2f(0.0f, 0.0f);	glVertex3f( width/2.0f,  -height/2.0f, -length/2.0f);
			glTexCoord2f(0.0f, 1.0f);	glVertex3f(-width/2.0f,  -height/2.0f, -length/2.0f);
			glTexCoord2f(1.0f, 1.0f);	glVertex3f(-width/2.0f,  -height/2.0f,  length/2.0f);
			glTexCoord2f(1.0f, 0.0f);	glVertex3f( width/2.0f,  -height/2.0f,  length/2.0f);
			// top
			glNormal3f(0.0f,-1.0f, 0.0f);
			glTexCoord2f(0.0f, 0.0f);	glVertex3f( width/2.0f,   height/2.0f, -length/2.0f);
			glTexCoord2f(0.0f, 1.0f);	glVertex3f(-width/2.0f,   height/2.0f, -length/2.0f);
			glTexCoord2f(1.0f, 1.0f);	glVertex3f(-width/2.0f,   height/2.0f,  length/2.0f);
			glTexCoord2f(1.0f, 0.0f);	glVertex3f( width/2.0f,   height/2.0f,  length/2.0f);
			// left
			glNormal3f(1.0f, 0.0f, 0.0f);
			glTexCoord2f(0.0f, 0.0f);	glVertex3f(-width/2.0f,   height/2.0f, -length/2.0f);
			glTexCoord2f(0.0f, 1.0f);	glVertex3f(-width/2.0f,  -height/2.0f, -length/2.0f);
			glTexCoord2f(1.0f, 1.0f);	glVertex3f(-width/2.0f,  -height/2.0f,  length/2.0f);
			glTexCoord2f(1.0f, 0.0f);	glVertex3f(-width/2.0f,   height/2.0f,  length/2.0f);
			// right
			glNormal3f(-1.0f, 0.0f, 0.0f);
			glTexCoord2f(0.0f, 0.0f);	glVertex3f( width/2.0f,   height/2.0f, -length/2.0f);
			glTexCoord2f(0.0f, 1.0f);	glVertex3f( width/2.0f,  -height/2.0f, -length/2.0f);
			glTexCoord2f(1.0f, 1.0f);	glVertex3f( width/2.0f,  -height/2.0f,  length/2.0f);
			glTexCoord2f(1.0f, 0.0f);	glVertex3f( width/2.0f,   height/2.0f,  length/2.0f);
		glEnd();

		glEndList();
	}
	glCallList(displist);
}
