#include <stdlib.h>
#include <iostream>
#include "Framework.hpp"
#include "Interface.hpp"
#include <math.h>
#include "SDL_image.h"


Framework::Framework(void *surf, const Configuration& conf, Networkstate initial)
 : field(this), output(this), surface((SDL_Surface*)surf),
   paused(1), timeunit(7), lasttime(SDL_GetTicks()), frames(0), state(initial), xdiff(0), camera(conf.width, conf.height)
{
	/* initialize OpenGL */
	resetGL();

	/* set up the window */
	camera.init();

	if (conf.fullscreen) SDL_WM_ToggleFullScreen(surface);

	/* set unpaused state (grab input) */
	togglePause(false, true);
}

Framework::~Framework()
{
	for (int i = 0; i < timerdata.size(); i++)
	{
		if (timerdata[i]->timer != NULL)
			SDL_RemoveTimer(timerdata[i]->timer);
		delete timerdata[i];
	}
}

void Framework::loop()
{
	/* main loop variable */
	bool done = false;
	/* used to collect events */
	SDL_Event event;

	/* wait for events */
	while (!done)
	{
		/* handle the events in the queue */
		while (SDL_PollEvent(&event))
		{
			switch(event.type)
			{
			case SDL_MOUSEMOTION:
				/* give away mouse movement with buttons pressed */
				handleMouseMove(event.motion.xrel, event.motion.yrel, event.motion.state);
				break;
			case SDL_MOUSEBUTTONUP:
				/* handle mouse button release for serving */
				if (event.button.button == SDL_BUTTON_LEFT)
					if (!paused) serveBall();
			case SDL_KEYDOWN:
				/* handle key presses */
				handleKeyPress(&event.key.keysym);
				break;
			case SDL_QUIT:
				/* handle quit requests */
				done = true;
				break;
			case SDL_USEREVENT:
				if (((TimerData*)event.user.data1)->timer == NULL)
				{
					/* this means our timer has gone inactive and we are pleased to stop our work! */
				} else {
					((TimerData*)event.user.data1)->receiver->action(((TimerData*)event.user.data1)->event);
				}
				break;
			}
		}
		doNetworking();

		int tdiff = SDL_GetTicks() - lasttime;
		if (tdiff > timeunit) {
			frames++;
			xdiff += tdiff; // always greater 0 because we decided to let tdiff be greater than timeunit
			if ((xdiff >= 100)&&(xdiff >= timeunit * 20)) {
				output.updateFPS(frames * 1000.0 / xdiff); // There are 1000 ticks / second
				frames = 0;
				xdiff = 0;
			}
			lasttime += tdiff;

			// Game status code
			updateGame(tdiff);

			// Rendering code
			drawScene();
		}
	}
}

/* function to handle key press events */
void Framework::handleKeyPress(SDL_keysym *keysym)
{
	switch (keysym->sym)
	{
	case SDLK_q:
		shutdown();
		break;
	case SDLK_f:
		SDL_WM_ToggleFullScreen(surface);
		break;
	case SDLK_ESCAPE:
	case SDLK_p:
		togglePause(false, false);
		break;
	case SDLK_F1:
		camera.setMode((Camera::View)1);
		break;
	case SDLK_F2:
		camera.setMode((Camera::View)2);
		break;
	case SDLK_F3:
		camera.setMode((Camera::View)3);
		break;
	default:
		break;
	}
}

void Framework::handleMouseMove(int x, int y, unsigned char buttons)
{
	if (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT))
	{
		camera.adjustAngle((double)x / 10.0, (double)y / 10.0);
	} else if (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE))
	{
		camera.adjustDistance((double)y / 10.0);
		resetGL();
	} else {
		if (!paused) movePaddle((double)x / 50.0, (double)-y / 50.0, SDL_GetTicks());
		if (camera.getMode() == Camera::FOLLOW_PADDLE)
			camera.adjustAngle((double)-x / 20.0, (double)-y / 20.0);
		else if (camera.getMode() == Camera::FOLLOW_PADDLE_REVERSE)
			camera.adjustAngle((double)x / 20.0, (double)y / 20.0);
	}
}

void Framework::togglePause(bool pause, bool external)
{
	Buffer sbuf;
	if ((((external)&&(pause))||((!external)&&(paused == 0)))&&(state != CONNECTING))
	{
		paused = SDL_GetTicks() - lasttime;
		SDL_ShowCursor(1);
		SDL_WM_GrabInput(SDL_GRAB_OFF);
		output.togglePaused(true);
		sbuf.setType(PAUSE_REQUEST);
	} else {
		lasttime = SDL_GetTicks() + paused;
		paused = 0;
		if (state != CONNECTING) {
			SDL_ShowCursor(0);
			SDL_WM_GrabInput(SDL_GRAB_ON);
		}
		output.togglePaused(false);
		sbuf.setType(RESUME_REQUEST);
	}
	if (!external)
		sendPacket(sbuf);
}

/* function to load in bitmap as a GL texture */
GLuint Framework::loadTexture(const std::string& filename)
{
	std::string fullname = std::string(PATH_PREFIX) + filename;
	GLuint texture = 0;
	/* Create temporal storage space for the texture */
	SDL_Surface *image;

	/* trying to load the bitmap */
	if ((image = IMG_Load(fullname.c_str())))
	{
		/* As the generated SDL surface has the format of the input image,
		   ie 24 bit or 8 bit, we have to convert it to the format
		   GL expects from us. In this case, 24 bpp.
		*/
		SDL_PixelFormat format;
		format.palette = NULL;
		format.BitsPerPixel= 24;
		format.Rshift= 16;	format.Gshift= 8;	format.Bshift= 0;
		format.Rmask= 0xff<<16;	format.Gmask= 0xff<<8;	format.Bmask= 0xff;

		SDL_Surface *teximage = SDL_ConvertSurface(image, &format, SDL_SWSURFACE);
		if (teximage) {
			/* Create the texture */
			glGenTextures(1, &texture);

			/* Load in texture */
			glBindTexture(GL_TEXTURE_2D, texture);

			/* Nearest filtering looks good on our wall texture */
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

			/* Generate the texture */
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, teximage->w, teximage->h, 0, GL_BGR,
					GL_UNSIGNED_BYTE, teximage->pixels);

			SDL_FreeSurface(teximage);
		} else {
			std::cerr << "Failed to convert texture format: " << fullname << std::endl;
			shutdown();
		}

		SDL_FreeSurface(image);
	} else {
		// we have no proper way to deal with missing textures, so we quit
		std::cerr << "Couldn't load texture: " << fullname << "\n\t" << IMG_GetError() << std::endl;
		shutdown();
	}

	return texture;
}


/* general OpenGL initialization function */
void Framework::resetGL()
{
	/* Enable smooth shading */
	glShadeModel(GL_SMOOTH);

	/* Set the background black */
	glClearColor(0.0f, 0.1f, 0.15f, 0.0f);

	/* Depth buffer setup */
	glClearDepth(1.0f);

	/* Enable normalizing of normals, actually not needed */
	//glEnable(GL_NORMALIZE);

	/* type of depth test to do */
	glDepthFunc(GL_LEQUAL);

	/* really nice perspective calculations */
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	glDisable(GL_COLOR_MATERIAL); // we have nice materials

	// Light parameters, used for all lights
	GLfloat l_ambient [] = { 0.1, 0.1, 0.1, 1.0 };
	GLfloat l_diffuse [] = { 0.4, 0.4, 0.4, 1.0 };
	GLfloat l_specular[] = { 1.0, 1.0, 1.0, 1.0 };
	// Set parameters but do not activate yet
	glLightfv(GL_LIGHT0, GL_AMBIENT, l_ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, l_diffuse);
	glLightfv(GL_LIGHT0, GL_SPECULAR, l_specular);
	glLightfv(GL_LIGHT1, GL_AMBIENT, l_ambient);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, l_diffuse);
	glLightfv(GL_LIGHT1, GL_SPECULAR, l_specular);
	glLightfv(GL_LIGHT2, GL_AMBIENT, l_ambient);
	glLightfv(GL_LIGHT2, GL_DIFFUSE, l_diffuse);
	glLightfv(GL_LIGHT2, GL_SPECULAR, l_specular);
	glLightfv(GL_LIGHT3, GL_AMBIENT, l_ambient);
	glLightfv(GL_LIGHT3, GL_DIFFUSE, l_diffuse);
	glLightfv(GL_LIGHT3, GL_SPECULAR, l_specular);

	// translate according actual camera position
	camera.translate();

	{
		GLfloat l_pos[] = { -field.getWidth(), -field.getHeight(), 0.0f, 1.0f };
		glLightfv(GL_LIGHT0, GL_POSITION, l_pos);
	}
	{
		GLfloat l_pos[] = { -field.getWidth(), field.getHeight(), 0.0f, 1.0f };
		glLightfv(GL_LIGHT1, GL_POSITION, l_pos);
	}
	{
		GLfloat l_pos[] = { field.getWidth(), -field.getHeight(), 0.0f, 1.0f };
		glLightfv(GL_LIGHT2, GL_POSITION, l_pos);
	}
	{
		GLfloat l_pos[] = { field.getWidth(), field.getHeight(), 0.0f, 1.0f };
		glLightfv(GL_LIGHT3, GL_POSITION, l_pos);
	}

	// now activate everything:
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHT1);
	glEnable(GL_LIGHT2);
	glEnable(GL_LIGHT3);

	// when clearing the stencil buffer, use 0
	glClearStencil(0);
}

/* Here goes our drawing code */
void Framework::drawScene()
{
	/* Clear the Buffers */
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	output.drawBackground((paused)||(state == CONNECTING));

	if (state != CONNECTING)
	{
		camera.translate();

		/* while we draw into the stencil buffer to store where the walls are to be overdrawn
		   with the ball reflections, we also draw the first "half" of the walls */

		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_ALWAYS, 1, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glDisable(GL_DEPTH_TEST);

		glDisable(GL_LIGHTING);
		glEnable(GL_TEXTURE_2D);
		/* we draw at half brightness, and later at half alpha. because we want to lighten
		   it a little bit up, we use 0.6 instead of 0.5 */
		glColor3f(0.6, 0.6, 0.6);
		field.draw();
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_LIGHTING);

		glEnable(GL_DEPTH_TEST);
		glStencilFunc(GL_EQUAL, 1, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

		// we will alter the modelview matrix by mirroring it
		glPushMatrix();

		// now it's time to draw 4 reflections for every ball

		/* Normally we would have to reposition the lights (according to the mirroring)
		   we have 4 lights with alike attributes which would replace themselves anyway
		   so we don't need to change them. For different light colors / other positions
		   we would need to add stuff here */

		glScalef(1.0, -1.0, 1.0);
		glTranslatef(0.0, field.getHeight(),0.0);
		for (int i = 0; i < ball.size(); i++)	ball[i].draw();

		camera.translate();
		glScalef(1.0, -1.0, 1.0);
		glTranslatef(0.0, -field.getHeight(),0.0);
		for (int i = 0; i < ball.size(); i++)	ball[i].draw();

		camera.translate();
		glScalef(-1.0, 1.0, 1.0);
		glTranslatef(field.getWidth(), 0.0,0.0);
		for (int i = 0; i < ball.size(); i++)	ball[i].draw();

		camera.translate();
		glScalef(-1.0, 1.0, 1.0);
		glTranslatef(-field.getWidth(), 0.0, 0.0);
		for (int i = 0; i < ball.size(); i++)	ball[i].draw();

		glDisable(GL_STENCIL_TEST);
		glPopMatrix();
		/* we now can overdraw the ball with the half translucent wall. this will give
		   a nice looking reflection effect */

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_LIGHTING);
		glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
		glEnable(GL_TEXTURE_2D);
		field.draw();
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_LIGHTING);
		glDisable(GL_BLEND);

		// finally we want to see the balls themselves, too

		for (int i = 0; i < ball.size(); i++)	ball[i].draw();

		// we conclude with translucent objects, first in the back:

		for (int i = 0; i < player.size(); i++)
			if (player[i].getSide() == BACK) player[i].draw();

		// next the ones in the front:

		for (int i = 0; i < player.size(); i++)
			if (player[i].getSide() == FRONT) player[i].draw();

		// these are overlays
		output.drawScore();
		output.drawRound();
	}
	output.drawFPS();
	output.drawMessages();

	/* Draw it to the screen */
	SDL_GL_SwapBuffers();
}

Collision* Framework::detectCol(const Vec3f& position, const Vec3f& speed, double radius)
{
	Collision* col;

	// test against walls
	col = field.detectCol(position, speed, radius);
	if (col != NULL) return col;

	// detection for paddles
	for (int i = 0; i < player.size(); i++)
	{
		col = player[i].detectCol(position, speed, radius);
		if (col != NULL) return col;
	}

	// wether the ball is still inside?
	Side side = field.zOutside(position.z);
	if (side != NONE)
		// score() detects itself if it's called multiple times
		score(side);
	return NULL;
}

double Framework::detectBarrier(double dest, int direction, Side side)
{
	switch (direction) {
	case LEFT:	return std::max(dest, -field.getWidth()/2.0f);
	case RIGHT:	return std::min(dest,  field.getWidth()/2.0f);
	case TOP:	return std::max(dest, -field.getHeight()/2.0f);
	case BOTTOM:	return std::min(dest,  field.getHeight()/2.0f);
	default:	return INFINITY;
	}
}

int Framework::addTimer(unsigned int intervall, EventReceiver::Event event, EventReceiver* receiver)
{
	TimerData* data = new TimerData;
	data->event = event;
	data->receiver = receiver;
	data->timer = SDL_AddTimer(intervall, processTimer, data);
	timerdata.push_back(data);
	return timerdata.size() - 1;
}

void Framework::removeTimer(int index)
{
	SDL_RemoveTimer(timerdata[index]->timer);
	timerdata[index]->timer = NULL;
	/* we are NOT allowed to delete the TimerData object, because it could be pointed to by
	   a currently running timer. it has to check wether it's SDL Timer still exists
	   and kill itself if not.
	*/
}

unsigned int processTimer(unsigned int intervall, void* data)
{
	SDL_Event event;
	event.type= SDL_USEREVENT;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return intervall;
}

void Framework::shutdown()
{
	SDL_Event killer;
	killer.type = SDL_QUIT;
	SDL_PushEvent(&killer);
}
