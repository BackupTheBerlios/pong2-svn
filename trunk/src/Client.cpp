#include "Client.hpp"
#include <iostream>
#include <sstream>

Client::Client(void *surf, const Configuration& conf)
 : Framework(surf, conf, CONNECTING), playername(conf.playername), ping(0)
{
	client = initNetwork(conf.version, conf.servername, conf.port, conf.playername);
	if (client == -1)
		shutdown();

	output.addMessage(Interface::CONNECTING);
	loop();
}

Client::~Client()
{
	grapple_client_destroy(client);
	exit(0);
}

int Client::initNetwork(const std::string& version, const std::string& server, const unsigned short port, const std::string& playername)
{
	grapple_client client = grapple_client_init("Pong2", version.c_str());
	grapple_client_sequential_set(client, GRAPPLE_NONSEQUENTIAL);
	grapple_client_protocol_set(client, GRAPPLE_PROTOCOL_UDP);
	grapple_client_port_set(client, port);
	grapple_client_address_set(client, server.c_str());
	grapple_client_name_set(client, playername.c_str());
	grapple_client_start(client, 0);

	grapple_error error = grapple_client_error_get(client);
	if (error != GRAPPLE_NO_ERROR) {
		std::cerr << "Error starting the client: " << grapple_error_text(error) << std::endl;
		return -1;
	}
	return client;
}

void Client::movePaddle(double x, double y, unsigned int time)
{
	if (state == RUNNING)
	{
		Buffer sbuf(PADDLEMOVE);
		sbuf.pushInt(time);
		sbuf.pushDouble(y);
		sbuf.pushDouble(-x);
		sendPacket(sbuf);
	}
}

void Client::updateGame(int ticks)
{
	if (state == CONNECTING) {
		ping += ticks;
		if (ping >= 1000)
		{
			ping = 0;
		}
	}
	// we will test here for timeouts from the server ... perhaps!
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
		sendPacket(sbuf);
	}
}

void Client::doNetworking()
{
	grapple_message *message;

	while (grapple_client_messages_waiting(client))
	{
		message=grapple_client_message_pull(client);
		switch (message->type)
		{
		case GRAPPLE_MSG_NEW_USER:
			std::cout << "New user: " << message->NEW_USER.id << std::endl;
			break;
		case GRAPPLE_MSG_NEW_USER_ME:
			std::cout << "I am " << message->NEW_USER.id << std::endl;
			{
				Buffer sbuf(READY);
				sendPacket(sbuf);
			}
			break;
		case GRAPPLE_MSG_USER_NAME:
		  	std::cout << "User " << message->USER_NAME.id << " set name " << message->USER_NAME.name << std::endl;
			break;
		case GRAPPLE_MSG_SESSION_NAME:
			std::cout << "Game name is " << message->SESSION_NAME.name << std::endl;
			state = TRANSMITTING_DATA;
			output.removeMessage(Interface::CONNECTING);
		break;
		case GRAPPLE_MSG_USER_MSG:
			{
				Buffer buf((char*)message->USER_MSG.data, message->USER_MSG.length);
				switch (buf.getType())
				{
				case READY:
					ball.push_back(Ball(this));
					player.push_back(Player(this, playername, FRONT, field.getLength()/2.0f));
					player.push_back(Player(this, "bla", BACK, field.getLength()/2.0f));
					player[0].run();
					player[1].run();
					output.updateScore(LEFT, 0, player[0].getName(), 0);
					output.updateScore(RIGHT, 0, player[1].getName(), 0);
					output.addMessage(Interface::FLASH_GAME_STARTED);
					state = RUNNING;
				break;
				case PAUSE_REQUEST:
					togglePause(true, true);
				//	std::cout << "Player " << sender->player->getName() << " paused the game." << std::endl;
				break;
				case RESUME_REQUEST:
					togglePause(false, true);
				//	std::cout << "Player " << sender->player->getName() << " resumed the game." << std::endl;
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
				break;
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
			break;
		case GRAPPLE_MSG_USER_DISCONNECTED:
			std::cout << "User " << message->USER_DISCONNECTED.id << " disconnected!" << std::endl;
			break;
		case GRAPPLE_MSG_SERVER_DISCONNECTED:
			std::cout << "Server lost!" << std::endl;
			shutdown();
			break;
		case GRAPPLE_MSG_CONNECTION_REFUSED:
			std::cout << "I'm not allowed!" << std::endl;
			break;
		case GRAPPLE_MSG_PING:
			std::cout << "Pingtime of " << message->PING.id << " is " << message->PING.pingtime << std::endl;
			break;
		}
		grapple_message_dispose(message);
	}
}

void Client::sendPacket(Buffer& data)
{
	grapple_client_send(client, GRAPPLE_SERVER, GRAPPLE_RELIABLE, data.getData(), data.getSize());
}
