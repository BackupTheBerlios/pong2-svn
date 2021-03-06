#include "Client.hpp"
#include <iostream>
#include <sstream>

Client::Client(void *surf, const Configuration& conf)
 : Framework(surf, conf, CONNECTING), playername(conf.playername)
{
	client = initNetwork(conf.version, conf.servername, conf.port, conf.playername);
	if (client == -1)
		shutdown();

	output.addMessage(Interface::CONNECTING);
	loop();
}

Client::~Client()
{
	if (state != CONNECTING) grapple_client_destroy(client);
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
		sendPacket(sbuf, false);
	}
}

// nothing to do here on the client side
void Client::updateGame(int ticks) {}

void Client::serveBall()
{
	if (state == RUNNING)
	{
		output.removeMessage(Interface::YOU_SERVE);
		sendSimplePacket(SERVE_BALL);
	}
}

void Client::ping()
{
	grapple_client_ping(client);
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
			peer[message->NEW_USER.id] = Peer("Unnamed");
			break;
		case GRAPPLE_MSG_NEW_USER_ME:
			peer[message->NEW_USER.id] = Peer("Local Player");
			localid = message->NEW_USER.id;
			sendSimplePacket(READY);
			break;
		case GRAPPLE_MSG_USER_NAME:
			peer[message->USER_NAME.id].name = message->USER_NAME.name;
			break;
		case GRAPPLE_MSG_SESSION_NAME:
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

					for (std::map<grapple_user, Peer>::iterator i = peer.begin(); i != peer.end(); ++i)
					{
						Side side = (i->first == localid ? FRONT : BACK);
						i->second.player = new Player(this, i->second.name, side, field.getLength()/2.0f);
						player.push_back(i->second.player);
						i->second.player->run();
					}
					output.updateScore(FRONT, 0);
					output.updateScore(BACK, 0);
					output.addMessage(Interface::FLASH_GAME_STARTED);
					state = RUNNING;
				break;
				case PAUSE_REQUEST:
					togglePause(true, true);
				break;
				case RESUME_REQUEST:
					togglePause(false, true);
				break;
				case ROUND:
					output.updateRound(buf.popInt());
				break;
				case SCORE:
					{
						Side side = buf.popSide();
						output.updateScore(side, buf.popInt());
						if (side == BACK)
							output.addMessage(Interface::FLASH_YOU_LOST);
						else
							output.addMessage(Interface::FLASH_YOU_WIN);
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
						grapple_user id = buf.popId();
						if (peer[id].player != NULL)
							peer[id].player->setPosition(-buf.popDouble(), buf.popDouble());
						else
							std::cerr << "Fatal: Wanted to access uninitialized player " << peer[id].name << std::endl;
					}
				break;
				case SERVE_BALL:
					ball[0].grow(500);
					if (buf.popId() == localid)
						output.addMessage(Interface::YOU_SERVE);
				break;
				}
			}
			break;
		case GRAPPLE_MSG_USER_DISCONNECTED:
			break;
		case GRAPPLE_MSG_SERVER_DISCONNECTED:
			if (state == CONNECTING)
				std::cout << "Unable to connect! (no answer - is the server running?)" << std::endl;
			else
				std::cout << "Server disconnected!" << std::endl;
			shutdown();
			break;
		case GRAPPLE_MSG_CONNECTION_REFUSED:
			std::cout << "Connection refused:\t";
			switch (message->CONNECTION_REFUSED.reason)
			{
			case GRAPPLE_NOCONN_VERSION_MISMATCH:
				std::cout << "Wrong network protocol version. (see pong2 -v)" << std::endl;
			case GRAPPLE_NOCONN_SERVER_FULL:
				std::cout << "The server is already full. :-(" << std::endl;
			case GRAPPLE_NOCONN_SERVER_CLOSED:
				std::cout << "The server is closed at the moment." << std::endl;
			}
			shutdown();
			break;
		case GRAPPLE_MSG_PING:
			if (message->PING.id == localid) output.updatePing(message->PING.pingtime);
			break;
		}
		grapple_message_dispose(message);
	}
}

void Client::sendPacket(Buffer& data, bool reliable)
{
	grapple_client_send(client, GRAPPLE_SERVER, reliable * GRAPPLE_RELIABLE, data.getData(), data.getSize());
}
