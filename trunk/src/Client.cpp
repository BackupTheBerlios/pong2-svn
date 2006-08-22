#include "Client.hpp"
#include <iostream>
#include <sstream>

Client::Client(void *surf, const Configuration& conf)
 : Framework(surf, conf, CONNECTING), server(resolveHost(conf.servername)), playername(conf.playername), ping(0)
{
	sendport = conf.port;
	listenport = conf.port+1;

	if (server == NULL) {
		std::cerr << "Couldn't resolve server host: " << conf.servername << std::endl;
		shutdown();
	}
	startListen();
	output.addMessage(Interface::CONNECTING);

	loop();
}

Client::~Client()
{
	Buffer sbuf(QUIT);
	sendPacket(server, sbuf.getData(), sbuf.getSize());
	exit(0);
}

void Client::movePaddle(double x, double y, unsigned int time)
{
	if (state == RUNNING)
	{
		Buffer sbuf(PADDLEMOVE);
		sbuf.pushInt(time);
		sbuf.pushDouble(y);
		sbuf.pushDouble(-x);
		sendPacket(server, sbuf.getData(), sbuf.getSize());
	}
}

void Client::updateGame(int ticks)
{
	if (state == CONNECTING) {
		ping += ticks;
		if (ping >= 1000)
		{
			Buffer sbuf(HELO);
			sbuf.pushInt(version);
			sendPacket(server, sbuf.getData(), sbuf.getSize());
			ping = 0;
		}
	}
	// we will test here for timeouts from the server!
}

void Client::score(Side side)
{
}

void Client::serveBall()
{
	if (state == RUNNING)
	{
		output.removeMessage(Interface::YOU_SERVE);
		Buffer sbuf(SERVE_BALL);
		if (state == RUNNING) sendPacket(server, sbuf.getData(), sbuf.getSize());
	}
}

void Client::receivePacket(Peer* sender, char* data, int size)
{
	//std::cout << "packet of size " << size << " received" << std::endl;
	Buffer buf(data, size);
	if (state == CONNECTING)
	{
		switch (buf.getType())
		{
		case SERVER_FULL:
			std::cerr << "Server is full!" << std::endl;
			shutdown();
			break;
		case VERSION_MISMATCH:
			{
				int sversion = buf.popInt();
				std::cerr << "Server has wrong version " << sversion << " instead of " << version << "!" << std::endl;
				shutdown();
			}
			break;
		case READY:
			{
				Buffer sbuf(PLAYERNAME);
				sbuf.pushString(playername);
				sendPacket(server, sbuf.getData(), sbuf.getSize());
				state = TRANSMITTING_DATA;
				output.removeMessage(Interface::CONNECTING);
			}
			break;
		}
	} else if (state == TRANSMITTING_DATA)
	{
		switch (buf.getType())
		{
		case PLAYERNAME:
			{
				std::string sname = buf.popString();
				std::cout << "Player " << sname << " entered the game :)" << std::endl;
				ball.push_back(Ball(this));
				player.push_back(Player(this, playername, FRONT, field.getLength()/2.0f));
				player.push_back(Player(this, sname, BACK, field.getLength()/2.0f));
				player[0].run();
				player[1].run();
				sender->player = &(player[1]);
				output.updateScore(LEFT, 0, player[0].getName(), 0);
				output.updateScore(RIGHT, 0, player[1].getName(), 0);
				output.addMessage(Interface::FLASH_GAME_STARTED);
				state = RUNNING;
			}
			break;
		}
	} else if (state == RUNNING)
	{
		switch (buf.getType())
		{
		case PAUSE_REQUEST:
			togglePause(true, true);
			std::cout << "Player " << sender->player->getName() << " paused the game." << std::endl;
			break;
		case RESUME_REQUEST:
			togglePause(false, true);
			std::cout << "Player " << sender->player->getName() << " resumed the game." << std::endl;
			break;
		case ROUND:
			output.updateRound(buf.popInt());
			break;
		case SCORE:
			{
				Side side = buf.popSide();
				if (side == BACK)
				{
					player[1].setScore(buf.popInt());
					output.updateScore(RIGHT, 0, player[1].getName(), player[1].getScore());
					output.addMessage(Interface::FLASH_YOU_LOST);
				} else {
					player[0].setScore(buf.popInt());
					output.updateScore(LEFT, 0, player[0].getName(), player[0].getScore());
					output.addMessage(Interface::FLASH_YOU_WIN);
				}
				ball[0].shrink(1000);
			}
		case BALLPOSITION:
			{	// in the future, we have to check for the player's side
				double a = -buf.popDouble();
				double b =  buf.popDouble();
				double c = -buf.popDouble();
				ball[0].setPosition(Vec3f(a, b, c));
			}
			break;
		case PADDLEPOSITION:
			{
				Side side = buf.popSide();
				if (side == BACK) {
					player[0].setPosition(-buf.popDouble(), buf.popDouble());
				} else {
					player[1].setPosition(-buf.popDouble(), buf.popDouble());
				}
			}
			break;
		case SERVE_BALL:
			ball[0].grow(500);
			output.addMessage(Interface::YOU_SERVE);
			break;
		case OPPOSITE_SERVE:
			ball[0].grow(500);
			break;
		}
	}
	switch (buf.getType())
	{
	case QUIT:
		std::cout << "Server closed the connection!" << std::endl;
		shutdown();
		break;
	}
}
