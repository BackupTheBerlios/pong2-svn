/*
 * This code was created by Jeff Molofee '99
 * (ported to Linux/SDL by Ti Leggett '01)
 *
 * If you've found this code useful, please let me know.
 *
 * Visit Jeff at http://nehe.gamedev.net/
 *
 * or for port-specific comments, questions, bugreports etc.
 * email to leggett@eecs.tulane.edu
 */

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <getopt.h>

#include "Server.hpp"
#include "Client.hpp"

//! usage declaration printed if the user gives in a malformed argument, like -h
#define USAGE \
"[-n <name>] [-c <server>] [-p port] [-w <width> -h <height>]\
\n[-b bitsperpixel] [-f]\
\n\
\n -n \t set your name (default: Hans)\
\n -c \t connect to already running server (default: act as server)\
\n -p \t set alternative udp networking port (default: 6642)\
\n -w \t set x resolution in pixels (default: 1024)\
\n -h \t set y resolution in pixels (default: 768)\
\n -b \t set individual bitsperpixel (default: 32)\
\n -f \t operate in fullscreen mode (default: windowed, toggle with 'f' key)\
\n"

//! function to release used resources and hopefully restore the desktop
/* This is registered with atexit() as the function to be called on every exit */
void Quit(void)
{
	/* shutdown net2 */
	NET2_Quit();
	// FE_Quit();

	/* shutdown sdlnet which was used by net2 */
	SDLNet_Quit();
	/* clean up the window */
	SDL_Quit();
}

//! parses the command line options, initializes libraries, hardware and finally gives over to a server or client object
int main (int argc, char **argv)
{
	Configuration conf;
	int c;
	while ((c = getopt(argc, argv, "c:p:w:h:b:fn:")) != EOF) {
		std::stringstream hlp;
		switch (c) {
		case 'c':
			conf.mode = Configuration::CLIENT;
			conf.servername = optarg;
			break;
		case 'p':
			hlp << optarg;
			hlp >> conf.port;
			break;
		case 'w':
			hlp << optarg;
			hlp >> conf.width;
			break;
		case 'h':
			hlp << optarg;
			hlp >> conf.height;
			break;
		case 'b':
			hlp << optarg;
			hlp >> conf.bpp;
			break;
		case 'f':
			conf.fullscreen = true;
			break;
		case 'n':
			conf.playername = optarg;
			break;
		default:
			std::cerr << "Usage: " << argv[0] << " " << USAGE;
			exit(1);
		}
	}

	/* register cleanup function */
	atexit(Quit);

	/* initialize SDL */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
	{
		std::cerr << "can't initialize SDL video: " << SDL_GetError() << std::endl;
		exit(EXIT_FAILURE);
	}

	/* SDL_SetVideoMode flags */
	int videoFlags = SDL_OPENGL;
	videoFlags |= SDL_GL_DOUBLEBUFFER;
	videoFlags |= SDL_HWPALETTE;

	/* Fetch the video info */
	const SDL_VideoInfo *videoInfo = SDL_GetVideoInfo();
	if (!videoInfo)
	{
		std::cerr << "Can't collect video info: " << SDL_GetError() << std::endl;
		exit(EXIT_FAILURE);
	}

	/* This checks to see if surfaces can be stored in memory */
	if (videoInfo->hw_available)
		videoFlags |= SDL_HWSURFACE;
	else	videoFlags |= SDL_SWSURFACE;

	/* This checks if hardware blits can be done */
	if (videoInfo->blit_hw) videoFlags |= SDL_HWACCEL;

	/* Sets up OpenGL double buffering */
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	/* Get an SDL surface */
	SDL_Surface *surface = SDL_SetVideoMode(conf.width, conf.height, conf.bpp, videoFlags);
	if (!surface)
	{
		std::cerr << "Can't create SDL surface: " << SDL_GetError() << std::endl;
		exit(EXIT_FAILURE);
	}

	SDL_WM_SetCaption("Pong²", NULL);

	/* Initialize the networking - net2 is based on sdlnet */
	if (SDLNet_Init() == -1)
	{
		std::cerr << "Can't initialize networking (1): " << SDLNet_GetError() << std::endl;
		exit(EXIT_FAILURE);
	}

	if (NET2_Init() == -1)
	{
		std::cerr << "Can't initialize networking (1): " << NET2_GetError() << std::endl;
		exit(EXIT_FAILURE);
	}

	/* create the Server or Client object which controls the game */
	if (conf.mode == Configuration::SERVER)
		Server((void*)surface, conf);
	else	Client((void*)surface, conf);

	return EXIT_SUCCESS;
}
