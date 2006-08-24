/*
    Grapple - A fully featured network layer with a simple interface
    Copyright (C) 2006 Michael Simms

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Michael Simms
    michael@linuxgamepublishing.com
*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#include "grapple_lobby_internal.h"
#include "grapple_lobbyclient_thread.h"
#include "grapple_lobby.h"
#include "grapple_server.h"
#include "grapple_client.h"
#include "tools.h"

//This file contains the thread that a lobby client starts up when it starts
//a new game. This thread feeds data about the game to the lobby server

void *grapple_lobbyclient_serverthread_main(void *data)
{
  internal_lobbyclient_data *client;
  int finished=0;
  int count,oldcount=0;
  int maxcount,oldmaxcount=0;
  int closed,oldclosed=GRAPPLE_SERVER_OPEN;
  char outdata[12];
  intchar val;

  client=(internal_lobbyclient_data *)data;

  //Loop as long as needed
  while (finished==0)
    {
      //Find conditions where the looping should stop
      if (!client->gameid || client->threaddestroy || 
	  !client->client)
	finished=1;

      //If we havent finished
      if (!finished)
	{
	  //Find the number of users connected to the game
	  count=grapple_server_currentusers_get(client->runninggame);

	  //If the number has changed
	  if (count!=oldcount)
	    {
	      oldcount=count;

	      //Send a message to the server with the new connection count
	      val.i=htonl(GRAPPLE_LOBBYMESSAGE_GAME_USERCOUNT);
	      memcpy(outdata,val.c,4);

	      val.i=htonl(client->gameid);
	      memcpy(outdata+4,val.c,4);

	      val.i=htonl(count);
	      memcpy(outdata+8,val.c,4);

	      grapple_client_send(client->client,GRAPPLE_SERVER,0,outdata,12);
	    }

	  //Now find the maximum number of users that can connect
	  maxcount=grapple_server_maxusers_get(client->runninggame);

	  //If the number has changed
	  if (maxcount!=oldmaxcount)
	    {
	      oldmaxcount=maxcount;

	      //Send a message to the server with the new connection count
	      val.i=htonl(GRAPPLE_LOBBYMESSAGE_GAME_MAXUSERCOUNT);
	      memcpy(outdata,val.c,4);

	      val.i=htonl(client->gameid);
	      memcpy(outdata+4,val.c,4);

	      val.i=htonl(maxcount);
	      memcpy(outdata+8,val.c,4);

	      grapple_client_send(client->client,GRAPPLE_SERVER,0,outdata,12);
	    }

	  //Now find if the game is open or closed
	  closed=grapple_server_closed_get(client->runninggame);

	  //If the number has changed
	  if (closed!=oldclosed)
	    {
	      oldclosed=closed;

	      //Send a message to the server with the new closed state
	      val.i=htonl(GRAPPLE_LOBBYMESSAGE_GAME_CLOSED);
	      memcpy(outdata,val.c,4);

	      val.i=htonl(client->gameid);
	      memcpy(outdata+4,val.c,4);

	      val.i=htonl(closed);
	      memcpy(outdata+8,val.c,4);

	      grapple_client_send(client->client,GRAPPLE_SERVER,0,outdata,12);
	    }

	  //Id the game has finished
	  if (!grapple_server_running(client->runninggame))
	    {
	      finished=1;
	      client->thread=0;
	      grapple_lobbyclient_game_unregister(client->lobbyclientnum);
	    }

	}

      if (!finished)
	microsleep(300000);
    }

  //We are finishing the thread
  client->thread=0;

  if (client->threaddestroy)
    client->threaddestroy=0;

  //On return the thread terminates

  return 0;
}


void *grapple_lobbyclient_clientthread_main(void *data)
{
  internal_lobbyclient_data *client;
  int finished=0;

  client=(internal_lobbyclient_data *)data;

  //Loop as long as needed
  while (finished==0)
    {
      //Find conditions where the looping should stop
      if (!client->ingame || client->threaddestroy || 
	  !client->client)
	finished=1;

      if (!finished)
	{
	  if (!grapple_client_connected(client->joinedgame))
	    {
	      finished=1;
	      client->thread=0;
	      grapple_lobbyclient_game_leave(client->lobbyclientnum,
					     client->joinedgame);
	    }

	  if (!finished)
	    microsleep(300000);
	}
    }

  //We are finishing the thread
  client->thread=0;

  if (client->threaddestroy)
    client->threaddestroy=0;

  //On return the thread terminates

  return 0;
}
