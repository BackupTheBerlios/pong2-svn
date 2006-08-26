#include <iostream>
#include <string>
#include <sstream>
#include <math.h>
#include "Interface.hpp"
#include "Framework.hpp"
#include "Player.hpp"

Interface::Interface(Framework* control)
 : fontlist(-1), framework(control), ping(""), fps("0 FPS"), roundnum("Round 1"), paused(false), flashtimer(-1)
{
	backTexture = framework->loadTexture("background.png");
	charTexture[0] = framework->loadTexture("q1.png");
	charTexture[1] = framework->loadTexture("q2.png");
	charTexture[2] = framework->loadTexture("q3.png");
	charTexture[3] = framework->loadTexture("q4.png");
	charTexture[4] = framework->loadTexture("q5.png");

	// we need to have the textures at hand first
	createFont();

	//for (int i = 32; i<128; i++) std::cout << (char)i << "\t";
}

Interface::~Interface()
{
	if (fontlist != -1)
		glDeleteLists(fontlist, 96);
}

void Interface::updateRound(int r)
{
	std::stringstream roundstr;
	roundstr << "Round " << r+1;
	roundnum = roundstr.str();
}

void Interface::updatePing(double pingtime)
{
	std::stringstream pingstr;
	pingstr << round(pingtime * 10.0)/10.0 << " ms";
	ping = pingstr.str();
}

void Interface::updateFPS(double frames)
{
	std::stringstream fpsstr;
	fpsstr << round(frames * 10.0)/10.0 << " FPS";
	fps = fpsstr.str();
}

void Interface::updateScore(Side side, int points)
{
	int idx = (side == FRONT ? 0 : 1);
	std::stringstream pts;
	pts << points;
	score[idx] = pts.str();
}

void Interface::addMessage(Message msg)
{
	message.push_back(msg);
	if ((msg == FLASH_GAME_STARTED)||(msg == FLASH_YOU_LOST)||(msg == FLASH_YOU_WIN))
	{
		if (flashtimer != -1) {
			message.remove(FLASH_GAME_STARTED);
			message.remove(FLASH_YOU_LOST);
			message.remove(FLASH_YOU_WIN);
			framework->removeTimer(flashtimer);
		}
		flashalpha = 1.0;
		flashtimer = framework->addTimer(25, DEFLASH, this);
	}
}

void Interface::removeMessage(Message msg)
{
	message.remove(msg);
}

void Interface::action(Event event)
{
	if (event == DEFLASH) {
		if (flashalpha > 0.05)
			flashalpha -= 0.01 + flashalpha*0.02;
		else {
			message.remove(FLASH_GAME_STARTED);
			message.remove(FLASH_YOU_LOST);
			message.remove(FLASH_YOU_WIN);
			framework->removeTimer(flashtimer);
			flashtimer = -1;
		}
	}
}

void Interface::createFont()
{
	char font_data[8][453] = {
	0,0,1,0,2,1,2,0,2,1,0,0,0,0,0,1,	0,2,0,1,0,0,0,1,0,0,0,0,0,1,0,2,	1,0,2,2,1,2,2,0,1,0,0,0,0,1,0,0,
	0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,	1,0,0,2,1,2,2,2,2,1,2,2,1,0,2,2,	2,1,2,2,2,2,1,2,0,0,2,1,2,2,2,2,
	1,2,2,2,0,1,2,2,2,2,1,2,2,2,2,1,	2,2,2,2,1,0,0,0,1,0,0,0,1,0,0,0,	1,0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,
	2,2,2,2,1,2,2,2,2,1,2,2,2,0,1,2,	2,2,1,2,2,2,0,1,2,2,2,2,1,2,2,2,	1,2,2,2,0,1,2,0,0,2,1,0,2,1,0,2,
	2,2,1,2,0,0,0,1,2,0,0,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,2,2,2,1,2,2,2,	2,1,2,2,2,2,1,2,2,2,2,1,2,2,2,0,
	1,2,2,2,2,2,1,2,0,0,2,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,0,0,2,1,2,0,0,	2,1,2,2,2,1,2,2,2,1,2,0,0,0,1,2,
	2,2,1,0,0,2,0,0,1,0,0,0,1,2,0,0,	1,2,2,2,2,1,2,2,2,0,1,2,2,2,1,2,	2,2,0,1,2,2,2,2,1,2,2,2,1,2,2,2,
	0,1,2,0,0,2,1,0,2,1,0,2,2,2,1,2,	0,0,0,1,2,0,0,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,2,2,2,1,2,2,2,2,1,2,2,
	2,2,1,2,2,2,2,1,2,2,2,0,1,2,2,2,	2,2,1,2,0,0,2,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,0,0,2,1,2,0,0,2,1,2,2,
	2,1,0,0,2,2,1,0,2,1,2,2,0,0,1,0,	0,0,0,0,1,
	0,0,1,0,2,1,2,0,2,1,0,0,0,0,0,1,	0,2,2,1,2,0,0,1,2,2,2,0,0,1,0,2,	1,2,0,0,1,0,0,2,1,0,0,0,0,1,0,0,
	2,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,	1,0,0,2,1,2,0,0,2,1,0,2,1,0,0,0,	2,1,0,0,0,2,1,2,0,0,2,1,2,0,0,0,
	1,2,0,0,0,1,0,0,0,2,1,2,0,0,2,1,	2,0,0,2,1,0,0,0,1,0,0,0,1,0,0,2,	1,0,0,0,1,2,0,0,1,0,0,2,1,2,0,0,
	0,0,0,2,1,2,0,0,2,1,2,0,0,2,1,2,	0,0,1,2,0,0,2,1,2,0,0,0,1,2,0,0,	1,2,0,0,0,1,2,0,0,2,1,0,2,1,0,0,
	0,2,1,2,0,0,2,1,2,0,0,1,2,2,0,2,	2,1,2,2,0,0,2,1,2,0,0,2,1,2,0,0,	2,1,2,0,0,2,1,2,0,0,2,1,2,0,0,0,
	1,0,0,2,0,0,1,2,0,0,2,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,0,0,2,1,2,0,0,	2,1,0,0,2,1,2,0,0,1,2,0,0,0,1,0,
	0,2,1,0,2,2,2,0,1,0,0,0,1,2,2,0,	1,2,0,0,2,1,2,0,0,2,1,2,0,0,1,2,	0,0,2,1,2,0,0,0,1,2,0,0,1,2,0,0,
	0,1,2,0,0,2,1,0,2,1,0,0,0,2,1,2,	0,0,2,1,2,0,0,1,2,2,0,2,2,1,2,2,	0,0,2,1,2,0,0,2,1,2,0,0,2,1,2,0,
	0,2,1,2,0,0,2,1,2,0,0,0,1,0,0,2,	0,0,1,2,0,0,2,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,0,0,2,1,2,0,0,2,1,0,0,
	2,1,0,2,0,0,1,0,2,1,0,0,2,0,1,0,	0,0,0,0,1,
	0,0,1,0,2,1,0,0,0,1,0,2,0,2,0,1,	2,2,0,1,0,0,2,1,2,0,2,0,0,1,0,0,	1,2,0,0,1,0,0,2,1,2,0,0,2,1,0,0,
	2,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,	1,0,2,2,1,2,0,0,2,1,0,2,1,0,0,0,	2,1,0,0,0,2,1,2,0,0,2,1,2,0,0,0,
	1,2,0,0,0,1,0,0,0,2,1,2,0,0,2,1,	2,0,0,2,1,0,2,2,1,0,2,2,1,0,2,2,	1,2,2,2,1,2,2,0,1,0,0,2,1,2,0,2,
	2,2,0,2,1,2,0,0,2,1,2,0,0,2,1,2,	0,0,1,2,0,0,2,1,2,0,0,0,1,2,0,0,	1,2,0,0,0,1,2,0,0,2,1,0,2,1,0,0,
	0,2,1,2,0,0,2,1,2,0,0,1,2,2,2,2,	2,1,2,2,2,0,2,1,2,0,0,2,1,2,0,0,	2,1,2,0,0,2,1,2,0,0,2,1,2,0,0,0,
	1,0,0,2,0,0,1,2,0,0,2,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,0,0,2,1,2,0,0,	2,1,0,0,2,1,2,0,0,1,2,2,0,0,1,0,
	0,2,1,2,2,0,2,2,1,0,0,0,1,0,2,2,	1,2,0,0,2,1,2,0,0,2,1,2,0,0,1,2,	0,0,2,1,2,0,0,0,1,2,0,0,1,2,0,0,
	0,1,2,0,0,2,1,0,2,1,0,0,0,2,1,2,	0,0,2,1,2,0,0,1,2,2,2,2,2,1,2,2,	2,0,2,1,2,0,0,2,1,2,0,0,2,1,2,0,
	0,2,1,2,0,0,2,1,2,0,0,0,1,0,0,2,	0,0,1,2,0,0,2,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,0,0,2,1,2,0,0,2,1,0,0,
	2,1,0,2,0,0,1,0,2,1,0,0,2,0,1,0,	0,0,0,0,1,
	0,0,1,0,2,1,0,0,0,1,2,2,2,2,2,1,	2,2,2,1,0,2,2,1,2,0,2,0,0,1,0,0,	1,2,0,0,1,0,0,2,1,0,2,2,0,1,2,2,
	2,2,2,1,0,0,0,1,2,2,2,2,1,0,0,0,	1,0,2,0,1,2,0,2,2,1,0,2,1,2,2,2,	2,1,0,2,2,2,1,2,2,2,2,1,2,2,2,2,
	1,2,2,2,2,1,0,0,2,2,1,2,2,2,2,1,	2,2,2,2,1,0,2,2,1,0,2,2,1,2,2,0,	1,0,0,0,1,0,2,2,1,0,2,2,1,2,0,2,
	0,2,0,2,1,2,2,2,2,1,2,2,2,0,1,2,	0,0,1,2,0,0,2,1,2,2,2,0,1,2,2,0,	1,2,0,0,0,1,2,2,2,2,1,0,2,1,0,0,
	0,2,1,2,0,0,2,1,2,0,0,1,2,0,2,0,	2,1,2,0,2,2,2,1,2,0,0,2,1,2,2,2,	2,1,2,0,0,2,1,2,2,2,2,1,2,2,2,2,
	1,0,0,2,0,0,1,2,0,0,2,1,2,0,0,0,	2,1,2,0,0,0,2,1,0,2,2,0,1,2,0,0,	2,1,0,2,0,1,2,0,0,1,0,2,0,0,1,0,
	0,2,1,0,0,0,0,0,1,0,0,0,1,0,0,2,	1,2,2,2,2,1,2,2,2,0,1,2,0,0,1,2,	0,0,2,1,2,2,2,0,1,2,2,0,1,2,0,0,
	0,1,2,2,2,2,1,0,2,1,0,0,0,2,1,2,	0,0,2,1,2,0,0,1,2,0,2,0,2,1,2,0,	2,2,2,1,2,0,0,2,1,2,2,2,2,1,2,0,
	0,2,1,2,2,2,2,1,2,2,2,2,1,0,0,2,	0,0,1,2,0,0,2,1,2,0,0,0,2,1,2,0,	0,0,2,1,0,2,2,0,1,2,0,0,2,1,0,2,
	0,1,2,0,0,0,1,0,2,1,0,0,0,2,1,0,	2,2,0,2,1,
	0,0,1,0,2,1,0,0,0,1,0,2,0,2,0,1,	0,2,2,1,2,2,0,1,2,2,2,0,2,1,0,0,	1,2,0,0,1,0,0,2,1,0,2,2,0,1,0,0,
	2,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,	1,0,2,0,1,2,2,0,2,1,0,2,1,2,0,0,	0,1,0,0,0,2,1,0,0,0,2,1,0,0,0,2,
	1,2,0,0,2,1,0,0,2,0,1,2,0,0,2,1,	0,0,0,2,1,0,0,0,1,0,0,0,1,2,2,0,	1,2,2,2,1,0,2,2,1,0,2,0,1,2,0,2,
	2,2,0,2,1,2,0,0,2,1,2,0,0,2,1,2,	0,0,1,2,0,0,2,1,2,0,0,0,1,2,0,0,	1,2,0,2,2,1,2,0,0,2,1,0,2,1,0,0,
	0,2,1,2,2,2,0,1,2,0,0,1,2,0,0,0,	2,1,2,0,0,2,2,1,2,0,0,2,1,2,0,0,	0,1,2,0,0,2,1,2,0,2,0,1,0,0,0,2,
	1,0,0,2,0,0,1,2,0,0,2,1,2,0,0,0,	2,1,2,0,2,0,2,1,2,0,0,2,1,2,0,0,	2,1,2,0,0,1,2,0,0,1,0,2,2,0,1,0,
	0,2,1,0,0,0,0,0,1,0,0,0,1,0,0,0,	1,2,0,0,2,1,2,0,0,2,1,2,0,0,1,2,	0,0,2,1,2,0,0,0,1,2,0,0,1,2,0,2,
	2,1,2,0,0,2,1,0,2,1,0,0,0,2,1,2,	2,2,0,1,2,0,0,1,2,0,0,0,2,1,2,0,	0,2,2,1,2,0,0,2,1,2,0,0,0,1,2,0,
	0,2,1,2,0,2,0,1,0,0,0,2,1,0,0,2,	0,0,1,2,0,0,2,1,2,0,0,0,2,1,2,0,	2,0,2,1,2,0,0,2,1,2,0,0,2,1,2,0,
	0,1,0,2,0,0,1,0,2,1,0,0,2,0,1,2,	0,2,2,0,1,
	0,0,1,0,0,1,0,0,0,1,2,2,2,2,2,1,	2,2,0,1,2,0,0,1,2,0,0,2,0,1,0,0,	1,2,0,0,1,0,0,2,1,2,0,0,2,1,0,0,
	2,0,0,1,0,2,2,1,0,0,0,0,1,0,2,2,	1,2,2,0,1,2,0,0,2,1,0,2,1,2,0,0,	0,1,0,0,0,2,1,0,0,0,2,1,0,0,0,2,
	1,2,0,0,2,1,0,2,2,0,1,2,0,0,2,1,	0,0,0,2,1,0,2,2,1,0,2,2,1,0,2,2,	1,0,0,0,1,2,2,0,1,0,0,0,1,2,0,0,
	0,2,2,2,1,2,0,0,2,1,2,0,0,2,1,2,	0,0,1,2,0,0,2,1,2,0,0,0,1,2,0,0,	1,2,0,0,2,1,2,0,0,2,1,0,2,1,0,0,
	0,2,1,2,0,0,2,1,2,0,0,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,0,0,2,1,2,0,0,	0,1,2,0,0,2,1,2,0,2,0,1,0,0,0,2,
	1,0,0,2,0,0,1,2,0,0,2,1,2,2,0,2,	2,1,2,2,2,2,2,1,2,0,0,2,1,2,2,2,	2,1,2,0,0,1,2,0,0,1,0,0,2,2,1,0,
	0,2,1,0,0,0,0,0,1,0,0,0,1,0,0,0,	1,2,0,0,2,1,2,0,0,2,1,2,0,0,1,2,	0,0,2,1,2,0,0,0,1,2,0,0,1,2,0,0,
	2,1,2,0,0,2,1,0,2,1,0,0,0,2,1,2,	0,0,2,1,2,0,0,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,0,0,2,1,2,0,0,0,1,2,0,
	0,2,1,2,0,2,0,1,0,0,0,2,1,0,0,2,	0,0,1,2,0,0,2,1,2,2,0,2,2,1,2,2,	2,2,2,1,2,0,0,2,1,2,2,2,2,1,2,0,
	0,1,0,2,0,0,1,0,2,1,0,0,2,0,1,0,	0,0,0,0,1,
	0,0,1,0,2,1,0,0,0,1,0,2,0,2,0,1,	0,2,0,1,0,0,2,1,2,0,0,2,0,1,0,0,	1,2,0,0,1,0,0,2,1,0,0,0,0,1,0,0,
	0,0,0,1,0,2,2,1,0,0,0,0,1,0,2,2,	1,2,0,0,1,2,0,0,2,1,0,2,1,2,0,0,	0,1,0,0,0,2,1,0,0,0,2,1,0,0,0,2,
	1,2,0,0,2,1,0,2,0,0,1,2,0,0,2,1,	0,0,0,2,1,0,2,2,1,0,2,2,1,0,0,2,	1,0,0,0,1,2,0,0,1,0,2,0,1,0,2,2,
	0,0,0,0,1,2,0,0,2,1,2,0,0,2,1,2,	0,0,1,2,0,0,2,1,2,0,0,0,1,2,0,0,	1,2,0,0,2,1,2,0,0,2,1,0,2,1,0,0,
	0,2,1,2,0,0,2,1,2,0,0,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,0,0,2,1,2,0,0,	0,1,2,2,2,0,1,2,0,0,2,1,0,0,0,2,
	1,0,0,2,0,0,1,2,0,0,2,1,0,2,2,2,	0,1,2,2,0,2,2,1,2,0,0,2,1,0,0,0,	2,1,2,0,0,1,2,0,0,1,0,0,2,2,1,0,
	0,2,1,0,0,0,0,0,1,0,0,0,1,0,0,0,	1,2,0,0,2,1,2,0,0,2,1,2,0,0,1,2,	0,0,2,1,2,0,0,0,1,2,0,0,1,2,0,0,
	2,1,2,0,0,2,1,0,2,1,0,0,0,2,1,2,	0,0,2,1,2,0,0,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,0,0,2,1,2,0,0,0,1,2,2,
	2,0,1,2,0,0,2,1,0,0,0,2,1,0,0,2,	0,0,1,2,0,0,2,1,0,2,2,2,0,1,2,2,	0,2,2,1,2,0,0,2,1,0,0,0,2,1,2,0,
	0,1,0,2,0,0,1,0,2,1,0,0,2,0,1,0,	0,0,0,0,1,
	0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,1,	0,0,0,1,0,0,0,1,2,2,2,0,2,1,0,0,	1,0,2,2,1,2,2,0,1,0,0,0,0,1,0,0,
	0,0,0,1,2,0,0,1,0,0,0,0,1,0,0,0,	1,2,0,0,1,2,2,2,2,1,0,2,1,2,2,2,	2,1,2,2,2,2,1,0,0,0,2,1,0,2,2,2,
	1,2,2,2,2,1,0,2,0,0,1,2,2,2,2,1,	0,0,0,2,1,0,0,0,1,2,2,0,1,0,0,0,	1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,
	0,0,0,0,1,2,0,0,2,1,2,2,2,0,1,2,	2,2,1,2,2,2,0,1,2,2,2,2,1,2,0,0,	1,2,2,2,2,1,2,0,0,2,1,0,2,1,2,2,
	2,0,1,2,0,0,2,1,2,2,2,1,2,0,0,0,	2,1,2,0,0,0,2,1,2,2,2,2,1,2,0,0,	0,1,0,0,0,2,1,2,0,0,2,1,2,2,2,2,
	1,0,0,2,0,0,1,2,2,2,2,1,0,0,2,0,	0,1,2,0,0,0,2,1,2,0,0,2,1,0,2,2,	2,1,2,2,2,1,2,2,2,1,0,0,0,2,1,2,
	2,2,1,0,0,0,0,0,1,2,2,2,1,0,0,0,	1,2,0,0,2,1,2,2,2,0,1,2,2,2,1,2,	2,2,0,1,2,2,2,2,1,2,0,0,1,2,2,2,
	2,1,2,0,0,2,1,0,2,1,2,2,2,0,1,2,	0,0,2,1,2,2,2,1,2,0,0,0,2,1,2,0,	0,0,2,1,2,2,2,2,1,2,0,0,0,1,0,0,
	0,2,1,2,0,0,2,1,2,2,2,2,1,0,0,2,	0,0,1,2,2,2,2,1,0,0,2,0,0,1,2,0,	0,0,2,1,2,0,0,2,1,0,2,2,2,1,2,2,
	2,1,0,0,2,2,1,0,2,1,2,2,0,0,1,0,	0,0,0,0,1
	};
	GLuint texture[8] = {
		charTexture[1],
		charTexture[2],
		charTexture[3],
		charTexture[4],
		charTexture[3],
		charTexture[2],
		charTexture[1],
		charTexture[0],
	};

	double glw = 1.0;
	double glh = -1.0;
	int x = 0, z, w, h;
	fontlist = glGenLists(96);
	for (int i = 0; i < 96; i++) {
		z = x;
		while (font_data[0][x] != 1)	x++;
		x++;
		character[i].width = x - z - 1;
		for (w = 0; w < character[i].width; w++)
			for (h = 0; h < 8; h++)
				character[i].bitmap[w][h] = font_data[h][z + w];

		glNewList(fontlist + i, GL_COMPILE);
			for (w = 0; w < character[i].width; w++)
			for (h = 0; h < 8; h++)
			{
			if (character[i].bitmap[w][h] == 2) {
					glBindTexture(GL_TEXTURE_2D, texture[h]);
					glBegin(GL_QUADS);
						glTexCoord2f(1.0f, 1.0f);	glVertex3f(glw * (double)(w+1), glh * (double)h	   , 0.0);
						glTexCoord2f(1.0f, 0.0f);	glVertex3f(glw * (double)w    , glh * (double)h	   , 0.0);
						glTexCoord2f(0.0f, 0.0f);	glVertex3f(glw * (double)w    , glh * (double)(h+1), 0.0);
						glTexCoord2f(0.0f, 1.0f);	glVertex3f(glw * (double)(w+1), glh * (double)(h+1), 0.0);
					glEnd();
				}
			}
			glTranslatef(glw * (character[i].width + 1), 0, 0);
		glEndList();
	}
}

void Interface::beginDraw()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glEnable(GL_TEXTURE_2D);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	glOrtho(0, 400, 0, 300, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
}

void Interface::endDraw()
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
	glEnable(GL_DEPTH_TEST);
}

void Interface::drawText(const std::string& text)
{
	glListBase(fontlist - 32);
	glCallLists(text.length(), GL_UNSIGNED_BYTE, text.c_str());
}

double Interface::textWidth(const std::string& text)
{
	float width = 0.0;
	for (int i = 0; i < text.size(); i++)
	{
		width += character[text[i] - 32].width + 1.0;
	}
	return std::max(width - 1.0, 0.0);
}

void Interface::drawBackground(bool cruel)
{
	beginDraw();

	// this is dirty, but it looks cruel :)
	if (cruel) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_COLOR, GL_ONE);
	}

	glBindTexture(GL_TEXTURE_2D, backTexture);
	glColor3f(1.0, 1.0, 1.0);

	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 1.0f);	glVertex3f(0.0,0.0,0.0);
		glTexCoord2f(1.0f, 1.0f);	glVertex3f(400.0,0.0,0.0);
		glTexCoord2f(1.0f, 0.0f);	glVertex3f(400.0,300.0,0.0);
		glTexCoord2f(0.0f, 0.0f);	glVertex3f(0.0,300.0,0.0);
	glEnd();

	endDraw();
}

void Interface::drawRound()
{
	beginDraw();

	glColor3f(1.0, 1.0, 1.0);

	glTranslatef(200.0 - textWidth(roundnum)/2.0 * 2.0, 300.0 - 2.0, 0);
	glScalef(2.0, 2.0, 2.0);
	drawText(roundnum);

	endDraw();
}

void Interface::drawFPS()
{
	beginDraw();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_COLOR, GL_ONE);
	glColor4f(0.5, 1.0, 1.0, 0.75f);

	// the font has a height of 8
	glTranslatef(2.0, 10.0, 0);
	glScalef(1.0, 1.0, 1.0);

	drawText(fps);

	glLoadIdentity();
	glTranslatef(2.0, 20.0, 0);
	drawText(ping);

	glDisable(GL_BLEND);
	endDraw();
}

void Interface::drawScore()
{
	const std::vector<Player*>& player = framework->getPlayers();
	beginDraw();

	for(int i = 0; i < 2; i++)
	{
		if (i == 0) {
			glColor3f(0.5, 1.0, 0.5);
		} else {
			glColor3f(1.0, 0.5, 0.5);
		}
		int namecount = 0;
		for (int j = 0; j < player.size(); ++j)
		{
			if (player[j]->getSide() == (i == 0 ? FRONT : BACK))
			{
				const char* name = player[j]->getName().c_str();
				glLoadIdentity();
				if (i == 0) {
					glTranslatef(2.0, 0, 0);
				} else {
					glTranslatef(400.0 - textWidth(name)*2.0 - 2.0, 0, 0);
				}
				glTranslatef(0, 300.0 - 2.0 - 18.0 * namecount, 0);
				glScalef(2.0, 2.0, 2.0);
				drawText(name);
				namecount++;
			}
		}

		glLoadIdentity();
		if (i == 0) {
			glTranslatef(2.0, 0, 0);
		} else {
			glTranslatef(400.0 - textWidth(score[i])*5.0 - 2.0, 0, 0);
		}
		glTranslatef(0, 300.0 - 2.0 - 18.0 * namecount, 0);
		glScalef(5.0, 5.0, 5.0);
		drawText(score[i]);
	}

	endDraw();
}

void Interface::drawMessages()
{
	beginDraw();

	for (std::list<Message>::iterator it = message.begin(); it != message.end(); it++)
	{
		switch (*it)
		{
		case YOU_SERVE:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_COLOR, GL_ONE);
			glColor4f(0.5, 0.5, 1.0, 0.65);
			glTranslatef(200.0 - textWidth("<You serve>")/2.0 * 6.0, 150.0 + 4.0 * 6.0, 0);
			glScalef(6.0, 6.0, 6.0);
			drawText("<You serve>");

			glLoadIdentity();
			glColor4f(1.0, 1.0, 1.0, 0.5);
			glTranslatef(200.0 - textWidth("Press the left mouse button to serve the ball.")/2.0 * 0.8, 10.0, 0);
			glScalef(0.8, 0.8, 0.8);
			drawText("Press the left mouse button to serve the ball.");
			glDisable(GL_BLEND);
			break;
		case CONNECTING:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_COLOR, GL_ONE);
			glColor4f(0.5, 0.5, 1.0, 0.65);
			glTranslatef(200.0 - textWidth("<Connecting>")/2.0 * 6.0, 150.0 + 4.0 * 6.0, 0);
			glScalef(6.0, 6.0, 6.0);
			drawText("<Connecting>");

			glLoadIdentity();
			glColor4f(1.0, 1.0, 1.0, 0.5);
			glTranslatef(200.0 - textWidth("Please make sure a server is running.")/2.0 * 0.8, 10.0, 0);
			glScalef(0.8, 0.8, 0.8);
			drawText("Please make sure a server is running.");
			glDisable(GL_BLEND);
			break;
		case WAITING_FOR_OPPONENT:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_COLOR, GL_ONE);
			glColor4f(0.5, 0.5, 1.0, 0.5);
			glTranslatef(200.0 - textWidth("<Waiting for opponent>")/2.0 * 2.0, 250.0 + 4.0 * 2.0, 0);
			glScalef(2.0, 2.0, 2.0);
			drawText("<Waiting for opponent>");
			glDisable(GL_BLEND);
			break;
		case FLASH_GAME_STARTED:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1.0, 1.0, 0.5, flashalpha);
			glTranslatef(200.0 - textWidth("<Game started>")/2.0 * 4.0, 200.0 + 4.0 * 4.0, 0);
			glScalef(4.0, 4.0, 4.0);
			drawText("<Game started>");
			glDisable(GL_BLEND);
			break;
		case FLASH_YOU_LOST:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1.0, 0.5, 0.0, flashalpha);
			glTranslatef(200.0 - textWidth("): YOU LOST :(")/2.0 * 4.0, 200.0 + 4.0 * 4.0, 0);
			glScalef(4.0, 4.0, 4.0);
			drawText("): YOU LOST :(");
			glDisable(GL_BLEND);
			break;
		case FLASH_YOU_WIN:
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(0.5, 1.0, 0.0, flashalpha);
			glTranslatef(200.0 - textWidth("(: YOU WIN :)")/2.0 * 4.0, 200.0 + 4.0 * 4.0, 0);
			glScalef(4.0, 4.0, 4.0);
			drawText("(: YOU WIN :)");
			glDisable(GL_BLEND);
			break;
		}
		glLoadIdentity();
	}
	if (paused) {
			glColor4f(1.0, 1.0, 1.0, 1.0);
			glTranslatef(200.0 - textWidth("* PAUSED *")/2.0 * 8.0, 150.0 + 4.0 * 8.0, 0);
			glScalef(8.0, 8.0, 8.0);
			drawText("* PAUSED *");
	}

	endDraw();
}
