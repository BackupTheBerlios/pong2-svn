#include "Server.hpp"
#include <iostream>
#include "Buffer.hpp"
#include <math.h>

Server::Server(void *surf, const Configuration& conf)
 : Framework(surf, conf, WAITING), round(0), ballouttimer(-1), ballspeed(6.0)
{
	sendport = conf.port+1;
	listenport = conf.port;
	ball.push_back(Ball(this));
	player.push_back(Player(this, conf.playername, FRONT, field.getLength()/2.0f));
	player.push_back(Player(this, "Mr. Wand", BACK, field.getLength()/2.0f));
	// Mr. Wand is cheating!
	player[1].setSize(3.5, 3.5); // *****************
	player[0].run();
	player[1].run();
	output.addMessage(Interface::YOU_SERVE);
	player[0].attachBall(&ball[0]);
	output.updateScore(LEFT, 0, player[0].getName(), 0);
	output.updateScore(RIGHT, 0, player[1].getName(), 0);
	output.addMessage(Interface::WAITING_FOR_OPPONENT);

	startListen();
	loop();
}

Server::~Server()
{
	if (state != WAITING) {
		Buffer sbuf(QUIT);
		for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
	}
	exit(0);
}

void Server::movePaddle(double x, double y, unsigned int time)
{
	// we move it ourselves.. the client will report it to the server
	player[0].move(x, y, time);
	if (state == RUNNING) {
		Vec2f pos = player[0].getPosition();
		Buffer sbuf(PADDLEPOSITION);
		sbuf.pushSide(FRONT);
		sbuf.pushDouble(pos.y);
		sbuf.pushDouble(pos.x);
		for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
	}
}

void Server::updateGame(int ticks)
{
	if (paused == 0) {
		ball[0].move(ticks);
		if (state == RUNNING) {
			Buffer sbuf(BALLPOSITION);
			const Vec3f& pos = ball[0].getPosition();
			sbuf.pushDouble(pos.x); sbuf.pushDouble(pos.y); sbuf.pushDouble(pos.z);
			for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
		}
	}
}

void Server::score(Side side)
{
	if (ballouttimer == -1)
	{
		// we didn't process it already
		if (side == BACK) {
			player[0].incScore();
			output.updateScore(LEFT, 0, player[0].getName(), player[0].getScore());
			output.addMessage(Interface::FLASH_YOU_WIN);
		} else {
			player[1].incScore();
			output.updateScore(RIGHT, 0, player[1].getName(), player[1].getScore());
			output.addMessage(Interface::FLASH_YOU_LOST);
		}

		if (state == RUNNING) {
			Buffer sbuf(SCORE);
			sbuf.pushSide(side);
			if (side == BACK)
				sbuf.pushInt(player[0].getScore());
			else	sbuf.pushInt(player[1].getScore());
			for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
		}
		output.updateRound(++round);
		if (state == RUNNING) {
			Buffer sbuf(ROUND);
			sbuf.pushInt(round);
			for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
		}
		ballouttimer = addTimer(1000, BALLOUT, this);
		ball[0].shrink(1000);
	}
}

void Server::serveBall()
{
	output.removeMessage(Interface::YOU_SERVE);
	player[0].detachBall(ballspeed);
}

void Server::action(Event event)
{
	if (event == BALLOUT)
	{
		std::cout << "********************************************" << std::endl;
		// Mr. Wand isn't as good in serving as he is in bouncing the ball
		if ((state == RUNNING)&&((int)floor(round / 5.0) % 2 == 0))
		{
			// up till now there can only be one peer; otherwise we had to select wisely
			player[1].attachBall(&ball[0]);
			Buffer sbuf(SERVE_BALL);
			sendPacket(&peer[0], sbuf.getData(), sbuf.getSize());
		} else {
			output.addMessage(Interface::YOU_SERVE);
			player[0].attachBall(&ball[0]);
			if (state == RUNNING) {
				Buffer sbuf(OPPOSITE_SERVE);
				sendPacket(&peer[0], sbuf.getData(), sbuf.getSize());
			}
		}
		removeTimer(ballouttimer);
		ballouttimer = -1;
	}
}

void Server::receivePacket(Peer* sender, char* data, int size)
{
	//std::cout << "packet of size " << size << " received" << std::endl;
	Buffer buf(data, size);
	switch (buf.getType())
	{
	case HELO:
		{
			Buffer sbuf;
			int cversion = buf.popInt();
			if (cversion == version)
			{
				if (state == WAITING)
				{
					sbuf.setType(READY);
					state = TRANSMITTING_DATA;
				} else sbuf.setType(SERVER_FULL);
			} else {
				sbuf.setType(VERSION_MISMATCH);
				sbuf.pushInt(version);
			}
			sendPacket(sender, sbuf.getData(), sbuf.getSize());
		}
		break;
	case PLAYERNAME:
		{
			std::string cname = buf.popString();
			std::cout << "Player " << cname << " entered the game :)" << std::endl;
			player[1].setName(cname);
			player[1].setScore(0);
			player[1].setSize(1.0, 1.0);
			sender->player = &(player[1]);
			round = 0;
			// only to be sure
			output.removeMessage(Interface::YOU_SERVE);
			player[0].detachBall(0.0);

			output.removeMessage(Interface::WAITING_FOR_OPPONENT);
			output.updateRound(round);
			output.updateScore(RIGHT, 0, player[1].getName(), player[1].getScore());

			{
				Buffer sbuf(PLAYERNAME);
				sbuf.pushString(player[0].getName());
				sendPacket(sender, sbuf.getData(), sbuf.getSize());
			}

			// it would be cool if our objects would have a reset option

			output.addMessage(Interface::FLASH_GAME_STARTED);
			state = RUNNING;
			ballouttimer = addTimer(10, BALLOUT, this);

			{
				Buffer sbuf;
				if (paused)
					sbuf.setType(PAUSE_REQUEST);
				else	sbuf.setType(RESUME_REQUEST);
				sendPacket(sender, sbuf.getData(), sbuf.getSize());
			}
			{
				Vec2f pos = player[0].getPosition();
				Buffer sbuf(PADDLEPOSITION);
				sbuf.pushSide(FRONT);
				sbuf.pushDouble(pos.y);
				sbuf.pushDouble(pos.x);
				for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
			}
	}
		break;
	case QUIT:
		std::cout << "Client disconnected!" << std::endl;
		shutdown();
		break;
	}
	if (state == RUNNING)
	{
		switch (buf.getType())
		{
		case PADDLEMOVE:
			{
				unsigned int time = buf.popInt();
				player[1].move(buf.popDouble(), buf.popDouble(), time);
				Vec2f pos = player[1].getPosition();
				Buffer sbuf(PADDLEPOSITION);
				sbuf.pushSide(BACK);
				sbuf.pushDouble(pos.y);
				sbuf.pushDouble(pos.x);
				for (int i = 0; i < peer.size(); i++) sendPacket(&peer[i], sbuf.getData(), sbuf.getSize());
			}
			break;
		case SERVE_BALL:
			player[1].detachBall(ballspeed);
			break;
		case PAUSE_REQUEST:
			togglePause(true, true);
			std::cout << "Player " << sender->player->getName() << " paused the game." << std::endl;
			break;
		case RESUME_REQUEST:
			togglePause(false, true);
			std::cout << "Player " << sender->player->getName() << " resumed the game." << std::endl;
			break;
		}
	}
}
