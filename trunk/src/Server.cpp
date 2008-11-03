#include <iostream>
#include <cmath>
#include "Server.hpp"
#include "Client.hpp"
#include "Buffer.hpp"

Server::Server(void *surf, const Configuration& conf)
 : Framework(surf, conf, UNINITIALIZED), ballouttimer(-1), ballspeed(6.0)
{
	ball.push_back(Ball(this));
	player.push_back(new Player(this, "Mr. Wand", BACK, field.getLength()/2.0f));
	// Mr. Wand is cheating!
	player[0]->setSize(3.5, 3.5); // *****************
	player[0]->run();
	resetScore();
	output.addMessage(Interface::WAITING_FOR_OPPONENT);

	server = initNetwork(conf.version, conf.playername, conf.port);
	loopback = Client::initNetwork(conf.version, "localhost", conf.port, conf.playername);

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

	//grapple_server_autoping(server, 5.0);

	grapple_error error = grapple_server_error_get(server);
	if (error != GRAPPLE_NO_ERROR) {
		std::cerr << "Error starting the client: " << grapple_error_text(error) << std::endl;
		return -1;
	}
	return server;
}

void Server::movePaddle(double x, double y, unsigned int time)
{
	if (state == UNINITIALIZED)
		return;

	// we move it ourselves.. the client will report it to the server
	Player* player = peer[localid].player;
	player->move(x, y, time);
	if (state == RUNNING) {
		Vec2f pos = player->getPosition();
		Buffer sbuf(PADDLEPOSITION);
		sbuf.pushId(localid);
		sbuf.pushDouble(pos.y);
		sbuf.pushDouble(pos.x);
		sendPacket(sbuf, false);
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
			sendPacket(sbuf, false);
		}
	}
}

void Server::resetScore()
{
	round = 0;
	score[0] = score[1] = 0;

	output.updateRound(0);
	output.updateScore(FRONT, 0);
	output.updateScore(BACK, 0);
}

void Server::doScore(Side side)
{
	if (ballouttimer == -1)
	{
		// we didn't process it already
		if (side == BACK) {
			score[0]++;
			output.updateScore(FRONT, score[0]);
			output.addMessage(Interface::FLASH_YOU_WIN);
		} else {
			score[1]++;
			output.updateScore(BACK, score[1]);
			output.addMessage(Interface::FLASH_YOU_LOST);
		}

		if (state == RUNNING) {
			Buffer sbuf(SCORE);
			sbuf.pushSide(side);
			if (side == BACK)
				sbuf.pushInt(score[0]);
			else	sbuf.pushInt(score[1]);
			sendPacket(sbuf, true);
		}
		output.updateRound(++round);
		if (state == RUNNING) {
			Buffer sbuf(ROUND);
			sbuf.pushInt(round);
			sendPacket(sbuf, true);
		}
		ballouttimer = addTimer(1000, BALLOUT, this);
		ball[0].shrink(1000);
	}
}

void Server::serveBall()
{
	output.removeMessage(Interface::YOU_SERVE);
	peer[localid].player->detachBall(ballspeed);
}

void Server::action(Event event)
{
	if (event == BALLOUT)
	{
		std::cout << "********************************************" << std::endl;
		/* Mr. Wand isn't as good in serving as he is in bouncing the ball
		   So as long the game is not running yet, it's always the user who serves
		*/
		if ((state == RUNNING)&&((int)floor(round / 5.0) % 2 == 0))
		{
			Buffer sbuf(SERVE_BALL);
			for (std::map<grapple_user, Peer>::iterator i = peer.begin(); i != peer.end(); ++i)
			{
				if ((*i).second.player->getSide() == BACK)
				{
					sbuf.pushId((*i).first);
					(*i).second.player->attachBall(&ball[0]);
					break;
				}
			}
			sendPacket(sbuf, true);
		} else {
			output.addMessage(Interface::YOU_SERVE);
			peer[localid].player->attachBall(&ball[0]);
			if (state == RUNNING) {
				Buffer sbuf(SERVE_BALL);
				sbuf.pushId(localid);
				sendPacket(sbuf, true);
			}
		}
		removeTimer(ballouttimer);
		ballouttimer = -1;
	}
}


// the client sends the ping
void Server::ping() {}

void Server::doNetworking()
{
	grapple_message *message;
	while (grapple_server_messages_waiting(server))
	{
		message = grapple_server_message_pull(server);
		switch (message->type)
		{
		case GRAPPLE_MSG_NEW_USER:
			peer[message->NEW_USER.id] = Peer(grapple_server_client_address_get(server, message->NEW_USER.id));
			std::cerr << "client connected from " << peer[message->NEW_USER.id].name << std::endl;
		break;
		case GRAPPLE_MSG_USER_NAME:
			peer[message->USER_NAME.id].name = message->USER_NAME.name;
		break;
		case GRAPPLE_MSG_USER_MSG:
			{
				grapple_user id = message->USER_MSG.id;
				Buffer buf((char*)message->USER_MSG.data, message->USER_MSG.length);
				switch (buf.getType())
				{
				case READY:
					peer[id].ready = true;

					if (peer.size() > 1) {
						bool ready = true;
						for (std::map<grapple_user, Peer>::iterator i = peer.begin(); i != peer.end(); ++i)
						{
							if (!(*i).second.ready)
							{
								ready = false;
								break;
							}
						}

						if (ready)
							startGame();
					}
				break;
				case PADDLEMOVE:
					{
						unsigned int time = buf.popInt();
						peer[id].player->move(buf.popDouble(), buf.popDouble(), time);
						Vec2f pos = peer[id].player->getPosition();
						Buffer sbuf(PADDLEPOSITION);
						sbuf.pushId(id);
						sbuf.pushDouble(pos.y);
						sbuf.pushDouble(pos.x);
						sendPacket(sbuf, false);
					}
				break;
				case SERVE_BALL:
					peer[id].player->detachBall(ballspeed);
					break;
				case PAUSE_REQUEST:
					togglePause(true, true);
					std::cout << "Player " << peer[id].name << " paused the game." << std::endl;
				break;
				case RESUME_REQUEST:
					togglePause(false, true);
					std::cout << "Player " << peer[id].name << " resumed the game." << std::endl;
				break;
				}
			break;
			}
		case GRAPPLE_MSG_USER_DISCONNECTED:
			std::cout << "Player " << peer[message->USER_DISCONNECTED.id].name << " disconnected!" << std::endl;
			shutdown();
		break;
		}
		grapple_message_dispose(message);
	}
	while (grapple_client_messages_waiting(loopback))
	{
		message=grapple_client_message_pull(loopback);
		switch (message->type)
		{
		case GRAPPLE_MSG_NEW_USER_ME:
			{
				localid = message->NEW_USER.id;
				peer[localid].player = new Player(this, peer[localid].name, FRONT, field.getLength()/2.0f);
				player.push_back(peer[localid].player);
				peer[localid].player->attachBall(&ball[0]);
				output.addMessage(Interface::YOU_SERVE);
				peer[localid].ready = true;
				state = WAITING;
			}
			break;
		}
		grapple_message_dispose(message);
	}
}

void Server::startGame()
{
	output.removeMessage(Interface::WAITING_FOR_OPPONENT);
	resetScore();

	// it could be we are serving at this moment
	output.removeMessage(Interface::YOU_SERVE);
	player[0]->detachBall(0.0);

	// create new players
	delete player[0];	delete player[1];
	player.clear();

	for (std::map<grapple_user, Peer>::iterator i = peer.begin(); i != peer.end(); ++i)
	{
		Side side = (i->first == localid ? FRONT : BACK);
		i->second.player = new Player(this, i->second.name, side, field.getLength()/2.0f);
		player.push_back(i->second.player);
		i->second.player->run();
	}

	// now we are up & ..
	state = RUNNING;
	sendSimplePacket(READY);

	// reset ball in a cool way
	output.addMessage(Interface::FLASH_GAME_STARTED);
	ballouttimer = addTimer(10, BALLOUT, this);

	// tell pause state
	if (paused)
		sendSimplePacket(PAUSE_REQUEST);
	else
		sendSimplePacket(RESUME_REQUEST);

	// tell our initial position
	Vec2f pos = peer[localid].player->getPosition();
	Buffer sbuf(PADDLEPOSITION);
	sbuf.pushId(localid);
	sbuf.pushDouble(pos.y);
	sbuf.pushDouble(pos.x);
	sendPacket(sbuf, false);
}

void Server::sendPacket(Buffer& data, bool reliable)
{
	grapple_server_send(server, GRAPPLE_EVERYONE, reliable * GRAPPLE_RELIABLE, data.getData(), data.getSize());
}
