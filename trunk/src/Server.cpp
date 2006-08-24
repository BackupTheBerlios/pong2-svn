#include "Server.hpp"
#include "Client.hpp"
#include <iostream>
#include "Buffer.hpp"
#include <math.h>

Server::Server(void *surf, const Configuration& conf)
 : Framework(surf, conf, WAITING), round(0), ballouttimer(-1), ballspeed(6.0)
{
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

	server = initNetwork(conf.version, conf.playername, conf.port);
	//loopback = Client::initNetwork(conf.version, "localhost", conf.port, conf.playername);

	if (server == -1 || loopback == -1)
		shutdown();

	loop();
}

Server::~Server()
{
	grapple_server_destroy(server);
	exit(0);
}

grapple_server Server::initNetwork(const std::string& version, const std::string& name, const unsigned short port)
{
	grapple_server server = grapple_server_init("Pong2", version.c_str());
	grapple_server_sequential_set(server, GRAPPLE_NONSEQUENTIAL);
	grapple_server_port_set(server, port);
	grapple_server_protocol_set(server, GRAPPLE_PROTOCOL_UDP);
	grapple_server_maxusers_set(server, 2);
	grapple_server_session_set(server, name.c_str());
	grapple_server_start(server);

	grapple_error error = grapple_server_error_get(server);
	if (error != GRAPPLE_NO_ERROR) {
		std::cerr << "Error starting the client: " << grapple_error_text(error) << std::endl;
		return -1;
	}
	return server;
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
		sendPacket(sbuf);
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
			sendPacket(sbuf);
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
			sendPacket(sbuf);
		}
		output.updateRound(++round);
		if (state == RUNNING) {
			Buffer sbuf(ROUND);
			sbuf.pushInt(round);
			sendPacket(sbuf);
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
			// !! FIXME !!
			sendPacket(sbuf);
		} else {
			output.addMessage(Interface::YOU_SERVE);
			player[0].attachBall(&ball[0]);
			if (state == RUNNING) {
				Buffer sbuf(OPPOSITE_SERVE);
				// !! FIXME !!
				sendPacket(sbuf);
			}
		}
		removeTimer(ballouttimer);
		ballouttimer = -1;
	}
}

void Server::doNetworking()
{
	grapple_message *message;
	while (grapple_server_messages_waiting(server))
	{
		message = grapple_server_message_pull(server);
		switch (message->type)
		{
		case GRAPPLE_MSG_NEW_USER:
			std::cout << "New user: " << message->NEW_USER.id << std::endl;
		break;
		case GRAPPLE_MSG_USER_NAME:
		  	std::cout << "User " << message->USER_NAME.id << " set name " << message->USER_NAME.name << std::endl;
			player[1].setName(message->USER_NAME.name);
		break;
		case GRAPPLE_MSG_USER_MSG:
			std::cout << "Packet!" << std::endl;
			{
				Buffer buf((char*)message->USER_MSG.data, message->USER_MSG.length);
				switch (buf.getType())
				{
				case READY:
					std::cout << "client is ready, alter!" << std::endl;
					player[1].setScore(0);
					player[1].setSize(1.0, 1.0);
					round = 0;
					// only to be sure
					output.removeMessage(Interface::YOU_SERVE);
					player[0].detachBall(0.0);

					output.removeMessage(Interface::WAITING_FOR_OPPONENT);
					output.updateRound(round);
					output.updateScore(RIGHT, 0, player[1].getName(), player[1].getScore());

					{
						Buffer sbuf(READY);
						sendPacket(sbuf);
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
						sendPacket(sbuf);
					}
					{
						Vec2f pos = player[0].getPosition();
						Buffer sbuf(PADDLEPOSITION);
						sbuf.pushSide(FRONT);
						sbuf.pushDouble(pos.y);
						sbuf.pushDouble(pos.x);
						sendPacket(sbuf);
					}
				break;
				case PADDLEMOVE:
					{
						unsigned int time = buf.popInt();
						player[1].move(buf.popDouble(), buf.popDouble(), time);
						Vec2f pos = player[1].getPosition();
						Buffer sbuf(PADDLEPOSITION);
						sbuf.pushSide(BACK);
						sbuf.pushDouble(pos.y);
						sbuf.pushDouble(pos.x);
						sendPacket(sbuf);
					}
				break;
				case SERVE_BALL:
					player[1].detachBall(ballspeed);
					break;
				case PAUSE_REQUEST:
					togglePause(true, true);
					//std::cout << "Player " << sender->player->getName() << " paused the game." << std::endl;
				break;
				case RESUME_REQUEST:
					togglePause(false, true);
					//std::cout << "Player " << sender->player->getName() << " resumed the game." << std::endl;
				break;
				}
			break;
			}
		case GRAPPLE_MSG_USER_DISCONNECTED:
			std::cout << "User " << message->USER_DISCONNECTED.id << " disconnected!" << std::endl;
			shutdown();
		break;
		}
		grapple_message_dispose(message);
	}
}

void Server::sendPacket(Buffer& data)
{
	grapple_server_send(server, GRAPPLE_EVERYONE, GRAPPLE_RELIABLE, data.getData(), data.getSize());
}
