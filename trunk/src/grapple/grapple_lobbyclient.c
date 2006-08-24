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

#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>

#include "grapple_lobby.h"
#include "grapple_lobby_internal.h"
#include "grapple_defines.h"
#include "grapple_error.h"
#include "grapple_client.h"
#include "grapple_server.h"
#include "tools.h"
#include "grapple_lobbyconnection.h"
#include "grapple_lobbymessage.h"
#include "grapple_lobbygame.h"
#include "grapple_lobbycallback.h"
#include "grapple_lobbyclient_thread.h"

/**************************************************************************
 ** The functions in this file are generally those that are accessible   **
 ** to the end user. Obvious exceptions are those that are static which  **
 ** are just internal utilities.                                         **
 ** Care should be taken to not change the parameters of outward facing  **
 ** functions unless absolutely required                                 **
 **************************************************************************/

//This is a static variable which keeps track of the list of all lobbyclients
//run by this program. The lobbyclients are kept in a linked list. This 
//variable is global to this file only.
static internal_lobbyclient_data *grapple_lobbyclient_head=NULL;

//Link a lobbyclient to the list
static int internal_lobbyclient_link(internal_lobbyclient_data *data)
{
  if (!grapple_lobbyclient_head)
    {
      grapple_lobbyclient_head=data;
      data->next=data;
      data->prev=data;
      return 1;
    }

  data->next=grapple_lobbyclient_head;
  data->prev=grapple_lobbyclient_head->prev;
  data->next->prev=data;
  data->prev->next=data;

  grapple_lobbyclient_head=data;
  
  return 1;
}

//Remove a lobbyclient from the linked list
static int internal_lobbyclient_unlink(internal_lobbyclient_data *data)
{
  if (data->next==data)
    {
      grapple_lobbyclient_head=NULL;
      return 1;
    }

  data->next->prev=data->prev;
  data->prev->next=data->next;

  if (data==grapple_lobbyclient_head)
    grapple_lobbyclient_head=data->next;

  data->next=NULL;
  data->prev=NULL;

  return 1;
}

//Find the lobbyclient from the ID number passed by the user
static internal_lobbyclient_data *internal_lobbyclient_get(grapple_lobbyclient num)
{
  internal_lobbyclient_data *scan;
  
  //By default if passed 0, then the oldest lobbyclient is returned
  if (!num)
    return grapple_lobbyclient_head;

  //This is a cache as most often you will want the same one as last time
  //Loop through the lobbyclients
  scan=grapple_lobbyclient_head;

  while (scan)
    {
      if (scan->lobbyclientnum==num)
	{
	  return scan;
	}
      
      scan=scan->next;
      if (scan==grapple_lobbyclient_head)
	return NULL;
    }

  //No match
  return NULL;
}

static void grapple_lobbyclient_error_set(internal_lobbyclient_data *client,
					  grapple_error error)
{
  client->last_error=error;
}

//Create a new lobbyclient
static internal_lobbyclient_data *lobbyclient_create(void)
{
  static int nextval=256;
  internal_lobbyclient_data *data;
  pthread_mutexattr_t attr;

  //Create the structure
  data=(internal_lobbyclient_data *)calloc(1,sizeof(internal_lobbyclient_data));

  //Assign it a default ID
  data->lobbyclientnum=nextval++;

  //Set up the mutexes
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  pthread_mutex_init(&data->userlist_mutex,&attr);
  pthread_mutex_init(&data->message_mutex,&attr);
  pthread_mutex_init(&data->games_mutex,&attr);

  //Link it into the array of lobbies
  internal_lobbyclient_link(data);

  return data;
}


//User function for initialising the lobbyclient
grapple_lobbyclient grapple_lobbyclient_init(const char *name,const char *version)
{
  internal_lobbyclient_data *data;

  //Create the internal data
  data=lobbyclient_create();

  data->client=grapple_client_init(name,version);

  //Return the client ID - the end user only gets an integer, called a
  //'grapple_lobbyclient'
  return data->lobbyclientnum;
}

//Set the port number to connect to
int grapple_lobbyclient_port_set(grapple_lobbyclient lobbyclient,int port)
{
  internal_lobbyclient_data *data;

  //Get the lobbyclient data
  data=internal_lobbyclient_get(lobbyclient);

  if (!data || !data->client)
    {
      return GRAPPLE_FAILED;
    }

  //Set this in grapple
  return grapple_client_port_set(data->client,port);
}

//Set the IP address to bind to. This is an optional, if not set, then all
//local addresses are bound to
int grapple_lobbyclient_address_set(grapple_lobbyclient lobbyclient,
				    const char *address)
{
  internal_lobbyclient_data *data;

  //Get the lobbyclient data
  data=internal_lobbyclient_get(lobbyclient);

  if (!data || !data->client)
    {
      return GRAPPLE_FAILED;
    }

  return grapple_client_address_set(data->client,address);
}


//Set the name of the user.
int grapple_lobbyclient_name_set(grapple_lobbyclient lobbyclient,
				 const char *name)
{
  internal_lobbyclient_data *data;

  //Get the lobbyclient data
  data=internal_lobbyclient_get(lobbyclient);

  if (!data || !data->client)
    {
      return GRAPPLE_FAILED;
    }

  //Connectstatus is a lobby value used to show how connected we are
  if (data->connectstatus!=GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_DISCONNECTED)
    {
      grapple_lobbyclient_error_set(data,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  if (data->name)
    free(data->name);

  data->name=(char *)malloc(strlen(name)+1);
  strcpy(data->name,name);

  return grapple_client_name_set(data->client,name);
}

//Get the top message from the list of messages for the clients attention
grapple_lobbymessage *grapple_lobbyclient_message_pull(grapple_lobbyclient lobbyclient)
{
  internal_lobbyclient_data *client;
  grapple_lobbymessage *message;

  //Get the lobbyclient data
  client=internal_lobbyclient_get(lobbyclient);

  if (!client || !client->client)
    {
      return NULL;
    }

  if (!client->messages)
    return NULL;

  //Get the message at the top of the queue
  pthread_mutex_lock(&client->message_mutex);
  message=client->messages;

  //Unlink it from the message list
  client->messages=grapple_lobbymessage_unlink(client->messages,message);
  pthread_mutex_unlock(&client->message_mutex);

  message->next=NULL;
  message->prev=NULL;

  return message;
}

//A message is going out to the end user, prepare it
static int grapple_lobbyclient_process_message(internal_lobbyclient_data *client,
					       grapple_lobbymessage *message)
{
  //If we are in a game, only send a disconnectmessage
  if (client->ingame && message->type!=GRAPPLE_LOBBYMSG_DISCONNECTED)
    return 0;

  //handle callbacks, we are in a thread so we can just do it
  if (grapple_lobbyclient_callback_process(client,message))
    {
      return 0;
    }

  //If not a callback, add it to the users message queue
  pthread_mutex_lock(&client->message_mutex);
  client->messages=grapple_lobbymessage_link(client->messages,message);
  pthread_mutex_unlock(&client->message_mutex);
  
  return 0;
}

//Have received a lobbymsg_connected message. This indicates that the name
//we have passed is acceptable to the lobby
static int grapple_lobbyclient_process_lobbymsg_connected(internal_lobbyclient_data *client,
							  grapple_message *message)
{
  intchar val;
  grapple_user id;
  grapple_lobbyconnection *user;
  char *name;
  void *data;
  int length;

  length=message->USER_MSG.length-4;
  data=message->USER_MSG.data+4;

  if (length < 4)
    return 0;

  memcpy(val.c,data,4);
  id=ntohl(val.i);

  //Get the name so we know what we have been accepted as
  name=grapple_client_name_get(client->client,id);

  //Fidn the user and set them as connected
  pthread_mutex_lock(&client->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(client->userlist,id);

  if (!user)
    {
      //This user must have disconnected or something
      pthread_mutex_unlock(&client->userlist_mutex);
      return 0;
    }

  user->connected=1;

  //Allocate the name into the user structure
  if (user->name)
    free(user->name);
  user->name=(char *)malloc(strlen(name)+1);
  strcpy(user->name,name);
  pthread_mutex_unlock(&client->userlist_mutex);

  //Now see if it is US that has just connected
  if (id == client->serverid)
    {
      //Set the connectstatus to 'connected'
      client->connectstatus=GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_CONNECTED;
    }

  return 0;
}

//Received a chat message from another user
static int grapple_lobbyclient_process_lobbymsg_chat(internal_lobbyclient_data *client,
						     grapple_message *message)
{
  void *data;
  int length;
  grapple_lobbymessage *outmessage;

  length=message->USER_MSG.length-4;
  data=message->USER_MSG.data+4;

  if (length < 4)
    return 0;

  //Decode the message into a lobbymessage

  outmessage=grapple_lobbymessage_aquire();
  
  outmessage->type=GRAPPLE_LOBBYMSG_CHAT;
  outmessage->CHAT.id=message->USER_MSG.id;
  outmessage->CHAT.length=length;
  outmessage->CHAT.message=(char *)malloc(length+1);
  memcpy(outmessage->CHAT.message,data,length);
  outmessage->CHAT.message[length]=0;

  //Send it to the outbound message processor
  grapple_lobbyclient_process_message(client,outmessage);

  return 0;
}

//A game has been registered
static int grapple_lobbyclient_process_lobbymsg_registergame(internal_lobbyclient_data *client,
							      grapple_message *message)
{
  void *data;
  int length,varlength;
  grapple_lobbymessage *outmessage=NULL;
  int offset;
  intchar val;
  grapple_lobbygame_internal *game;

  length=message->USER_MSG.length-4;
  data=message->USER_MSG.data+4;

  if (length < 4)
    return 0;

  //A new game is now being registered. We need to deconstruct the complex 
  //data packet

  game=grapple_lobbygame_internal_create();

  //4 bytes : game ID
  //4 bytes : Session name length
  //        ; Session name
  //4 bytes : Address length
  //        : address
  //4 bytes : portnumber
  //4 bytes : protocol
  //4 bytes : Maximum number of users
  //4 bytes : Password required (could be 1 byte but lets stick with ints)
  //4 bytes : Room number

  memcpy(val.c,data,4);
  game->id=ntohl(val.i);

  memcpy(val.c,data+4,4);
  varlength=ntohl(val.i);

  game->session=(char *)malloc(varlength+1);
  memcpy(game->session,data+8,varlength);
  game->session[varlength]=0;
  offset=varlength+8;

  memcpy(val.c,data+offset,4);
  varlength=ntohl(val.i);
  offset+=4;

  game->address=(char *)malloc(varlength+1);
  memcpy(game->address,data+offset,varlength);
  game->address[varlength]=0;
  offset+=varlength;

  memcpy(val.c,data+offset,4);
  game->port=ntohl(val.i);
  offset+=4;

  memcpy(val.c,data+offset,4);
  game->protocol=ntohl(val.i);
  offset+=4;

  memcpy(val.c,data+offset,4);
  game->maxusers=ntohl(val.i);
  offset+=4;

  memcpy(val.c,data+offset,4);
  game->needpassword=ntohl(val.i);
  offset+=4;

  memcpy(val.c,data+offset,4);
  game->room=ntohl(val.i);

  if (game->room==client->currentroom)
    {
      //Set up a message to tell the player
      outmessage=grapple_lobbymessage_aquire();
      
      outmessage->type=GRAPPLE_LOBBYMSG_NEWGAME;
      outmessage->GAME.id=game->id;
      outmessage->GAME.name=(char *)malloc(strlen(game->session)+1);
      strcpy(outmessage->GAME.name,game->session);
      outmessage->GAME.maxusers=game->maxusers;
      outmessage->GAME.needpassword=game->needpassword;
    }

  //Now link the game into the list
  pthread_mutex_lock(&client->games_mutex);
  client->games=grapple_lobbygame_internal_link(client->games,game);
  pthread_mutex_unlock(&client->games_mutex);

  if (outmessage)
    //Send the players message to the outbound message processor
    grapple_lobbyclient_process_message(client,outmessage);

  return 0;
}

//A game has been deleted
static int grapple_lobbyclient_process_lobbymsg_deletegame(internal_lobbyclient_data *client,
							      grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  grapple_lobbymessage *outmessage=NULL;

  memcpy(val.c,message->USER_MSG.data+4,4);

  gameid=ntohl(val.i);

  pthread_mutex_lock(&client->games_mutex);
  game=grapple_lobbygame_internal_locate_by_id(client->games,gameid);

  //Locate the game
  if (game)
    {
      //Unlink it from the game list
      client->games=grapple_lobbygame_internal_unlink(client->games,game);
      
      pthread_mutex_unlock(&client->games_mutex);

      if (game->room == client->currentroom)
	{
	  //Set up a message to tell the player
	  outmessage=grapple_lobbymessage_aquire();
	  
	  outmessage->type=GRAPPLE_LOBBYMSG_DELETEGAME;
	  outmessage->GAME.id=game->id;
	  grapple_lobbyclient_process_message(client,outmessage);
	}

      //Delete it, its dead
      grapple_lobbygame_internal_dispose(game);
    }
  else
    pthread_mutex_unlock(&client->games_mutex);

  return 0;
}

//The server has sent us an ID for the game we have just started
static int grapple_lobbyclient_process_lobbymsg_yourgameid(internal_lobbyclient_data *client,
							   grapple_message *message)
{
  void *data;
  int length;
  intchar val;

  length=message->USER_MSG.length-4;
  data=message->USER_MSG.data+4;

  if (length < 4)
    return 0;

  //Set the internal game ID
  memcpy(val.c,data,4);
  client->gameid=ntohl(val.i);

  return 0;
}

//The number of users connected to a game has changed
static int grapple_lobbyclient_process_lobbymsg_game_usercount(internal_lobbyclient_data *client,
							       grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  int count;
  grapple_lobbymessage *outmessage;

  memcpy(val.c,message->USER_MSG.data+4,4);
  gameid=ntohl(val.i);

  memcpy(val.c,message->USER_MSG.data+8,4);
  count=ntohl(val.i);

  //Find the game
  pthread_mutex_lock(&client->games_mutex);
  game=grapple_lobbygame_internal_locate_by_id(client->games,gameid);

  if (game)
    {
      //Set its new user value
      game->currentusers=count;
      
      pthread_mutex_unlock(&client->games_mutex);

      //Send the data to the user
      outmessage=grapple_lobbymessage_aquire();
      outmessage->type=GRAPPLE_LOBBYMSG_GAME_USERS;
      outmessage->GAME.id=gameid;
      outmessage->GAME.currentusers=count;
      grapple_lobbyclient_process_message(client,outmessage);
    }
  else
    pthread_mutex_unlock(&client->games_mutex);

  return 0;
}

//The maximum number of users that can connect to a game has changed
static int grapple_lobbyclient_process_lobbymsg_game_maxusercount(internal_lobbyclient_data *client,
								  grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  int count;
  grapple_lobbymessage *outmessage;

  memcpy(val.c,message->USER_MSG.data+4,4);
  gameid=ntohl(val.i);

  memcpy(val.c,message->USER_MSG.data+8,4);
  count=ntohl(val.i);

  pthread_mutex_lock(&client->games_mutex);
  //Find the game
  game=grapple_lobbygame_internal_locate_by_id(client->games,gameid);

  if (game)
    {
      ////Set the new value
      game->maxusers=count;
      
      pthread_mutex_unlock(&client->games_mutex);

      //Tell the user
      outmessage=grapple_lobbymessage_aquire();
      outmessage->type=GRAPPLE_LOBBYMSG_GAME_MAXUSERS;
      outmessage->GAME.id=gameid;
      outmessage->GAME.maxusers=count;
      grapple_lobbyclient_process_message(client,outmessage);
    }
  else
    pthread_mutex_unlock(&client->games_mutex);

  return 0;
}

//The games open/closed status has changed
static int grapple_lobbyclient_process_lobbymsg_game_closed(internal_lobbyclient_data *client,
							    grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  int state;
  grapple_lobbymessage *outmessage;

  memcpy(val.c,message->USER_MSG.data+4,4);
  gameid=ntohl(val.i);

  memcpy(val.c,message->USER_MSG.data+8,4);
  state=ntohl(val.i);

  pthread_mutex_lock(&client->games_mutex);
  //Find the game
  game=grapple_lobbygame_internal_locate_by_id(client->games,gameid);

  if (game)
    {
      ////Set the new value
      game->closed=state;
      
      pthread_mutex_unlock(&client->games_mutex);

      //Tell the user
      outmessage=grapple_lobbymessage_aquire();
      outmessage->type=GRAPPLE_LOBBYMSG_GAME_MAXUSERS;
      outmessage->GAME.id=gameid;
      outmessage->GAME.closed=state;
      grapple_lobbyclient_process_message(client,outmessage);
    }
  else
    pthread_mutex_unlock(&client->games_mutex);

  return 0;
}

//A user message has come through. User messages are what are contains the
//lobby specific messages, for the protocol that the lobby uses ontop of
//grapple
static int grapple_lobbyclient_process_user_msg(internal_lobbyclient_data *client,
						grapple_message *message)
{
  grapple_lobbymessagetype_internal type;
  intchar val;

  //User message - break it into its components

  if (message->USER_MSG.length < 4)
    return 0;

  //Find the type of message
  memcpy(val.c,message->USER_MSG.data,4);
  type=ntohl(val.i);

  //Hand off the message to a sub-handler
  switch (type)
    {
    case GRAPPLE_LOBBYMESSAGE_DUPLICATENAME:
      client->connectstatus=GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_REJECTED;
      break;
    case GRAPPLE_LOBBYMESSAGE_YOURGAMEID:
      grapple_lobbyclient_process_lobbymsg_yourgameid(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_CONNECTED:
      grapple_lobbyclient_process_lobbymsg_connected(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_CHAT:
      grapple_lobbyclient_process_lobbymsg_chat(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_REGISTERGAME:
      grapple_lobbyclient_process_lobbymsg_registergame(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_DELETEGAME:
      grapple_lobbyclient_process_lobbymsg_deletegame(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_GAME_USERCOUNT:
      grapple_lobbyclient_process_lobbymsg_game_usercount(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_GAME_MAXUSERCOUNT:
      grapple_lobbyclient_process_lobbymsg_game_maxusercount(client,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_GAME_CLOSED:
      grapple_lobbyclient_process_lobbymsg_game_closed(client,message);
      break;
    }
  
  return 0;
}

//A new user has connected - This user MAY not be connected properly, do
//dont set their connected flag
static int grapple_lobbyclient_process_new_user(internal_lobbyclient_data *client,
						grapple_message *message)
{
  grapple_lobbyconnection *newuser;

  //Create the users local data
  newuser=grapple_lobbyconnection_create();

  newuser->id=message->NEW_USER.id;
  
  pthread_mutex_lock(&client->userlist_mutex);
  //Link it in
  client->userlist=grapple_lobbyconnection_link(client->userlist,newuser);
  pthread_mutex_unlock(&client->userlist_mutex);

  if (message->NEW_USER.me)
    //If it is us, set our server id
    client->serverid=message->NEW_USER.id;

  return 0;
}


//A group has been created. In the lobby a group is associated with a room
static int grapple_lobbyclient_process_group_create(internal_lobbyclient_data *client,
						    grapple_message *message)
{
  grapple_lobbymessage *outmessage;

  //If the user isnt in the first room, they dont hear about room creation
  //as rooms are only created off of the main room
  if (client->currentroom != client->firstroom)
    return 0;

  if (client->currentroom!=0)
    {
      //Inform the user

      outmessage=grapple_lobbymessage_aquire();
      
      outmessage->type=GRAPPLE_LOBBYMSG_ROOMCREATE;
      
      outmessage->ROOM.roomid=message->GROUP.groupid;
      outmessage->ROOM.name=message->GROUP.name;
      message->GROUP.name=NULL;
      
      grapple_lobbyclient_process_message(client,outmessage);
    }

  return 0;
}

//Someone has joined a group. In effect, they have 'joined the room'
static int grapple_lobbyclient_process_group_add(internal_lobbyclient_data *client,
						 grapple_message *message)
{
  grapple_lobbymessage *outmessage;

  //If it is us
  if (message->GROUP.memberid == client->serverid)
    {
      //If it is our first join
      if (client->currentroom==0)
	{
	  //Note this as the main room
	  client->firstroom=message->GROUP.groupid;
	}
      //Now set our current room to here
      client->currentroom=message->GROUP.groupid;
    }

  if (message->GROUP.groupid!=client->currentroom)
    //The message isnt in the room we are in, we dont care
    return 0;

  outmessage=grapple_lobbymessage_aquire();
  
  outmessage->type=GRAPPLE_LOBBYMSG_ROOMENTER;

  outmessage->ROOM.userid=message->GROUP.memberid;

  //Send the message to the user
  grapple_lobbyclient_process_message(client,outmessage);

  return 0;
}

//Someone has left a room
static int grapple_lobbyclient_process_group_remove(internal_lobbyclient_data *client,
						    grapple_message *message)
{
  grapple_lobbymessage *outmessage;

  //If it isnt our room, we dont care
  if (message->GROUP.groupid!=client->currentroom)
    return 0;

  //Send a message to the user
  outmessage=grapple_lobbymessage_aquire();
  
  outmessage->type=GRAPPLE_LOBBYMSG_ROOMLEAVE;

  outmessage->ROOM.userid=message->GROUP.memberid;

  grapple_lobbyclient_process_message(client,outmessage);

  return 0;
}

//A room has been deleted
static int grapple_lobbyclient_process_group_delete(internal_lobbyclient_data *client,
						    grapple_message *message)
{
  grapple_lobbymessage *outmessage;

  //Only get room delete messages from the first room
  if (client->currentroom != client->firstroom)
    return 0;

  outmessage=grapple_lobbymessage_aquire();
  
  outmessage->type=GRAPPLE_LOBBYMSG_ROOMDELETE;

  outmessage->ROOM.roomid=message->GROUP.groupid;

  outmessage->ROOM.name=message->GROUP.name;
  message->GROUP.name=NULL;

  grapple_lobbyclient_process_message(client,outmessage);

  return 0;
}

//Connection was refused - probably becuse we have a non-unique name
static int grapple_lobbyclient_process_connection_refused(internal_lobbyclient_data *client,
							  grapple_message *message)
{
  //Set the status - we are in a callback thread here, so in the main
  //thread, the status is being waited on.

  client->connectstatus=GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_DISCONNECTED;

  return 0;
}

//A user has disconected
static int grapple_lobbyclient_process_user_disconnected(internal_lobbyclient_data *client,
							 grapple_message *message)
{
  grapple_lobbyconnection *user;
  grapple_lobbymessage *outmessage;

  pthread_mutex_lock(&client->userlist_mutex);
  //Remove them from the userlist
  user=grapple_lobbyconnection_locate_by_id(client->userlist,
					    message->USER_DISCONNECTED.id);
  if (user)
    client->userlist=grapple_lobbyconnection_unlink(client->userlist,user);

  pthread_mutex_unlock(&client->userlist_mutex);


  //Is it us?
  if (message->USER_DISCONNECTED.id==client->serverid)
    {
      //Let the user know we're disconnected
      outmessage=grapple_lobbymessage_aquire();
      outmessage->type=GRAPPLE_LOBBYMSG_DISCONNECTED;
      grapple_lobbyclient_process_message(client,outmessage);
    }
  else
    {
      //If the user is in the same room as us
      if (user && user->currentroom == client->currentroom)
	{
	  outmessage=grapple_lobbymessage_aquire();
	  outmessage->type=GRAPPLE_LOBBYMSG_ROOMLEAVE;
	  outmessage->ROOM.userid=message->USER_DISCONNECTED.id;

	  outmessage->ROOM.name=user->name;
	  user->name=NULL;

	  //send the user the information
	  grapple_lobbyclient_process_message(client,outmessage);
	}
    }

  //Get rid of the disconnected user
  if (user)
    grapple_lobbyconnection_dispose(user);

  return 0;
}

//The server has disconnected, this is the whole lobby going
static int grapple_lobbyclient_process_server_disconnected(internal_lobbyclient_data *client,
							   grapple_message *message)
{
  grapple_lobbymessage *outmessage;

  //Send the user a message and let them handle cleanup  
  outmessage=grapple_lobbymessage_aquire();
  outmessage->type=GRAPPLE_LOBBYMSG_DISCONNECTED;
  grapple_lobbyclient_process_message(client,outmessage);

  return 0;
}

//All messages from grapple and sent here as callbacks to be distributed to
//subfinctions
static int grapple_lobbyclient_generic_callback(grapple_message *message,
						void *context)
{
  internal_lobbyclient_data *client;

  client=(internal_lobbyclient_data *)context;

  //Send the message to a handler
  switch (message->type)
    {
    case GRAPPLE_MSG_NEW_USER:
    case GRAPPLE_MSG_NEW_USER_ME:
      grapple_lobbyclient_process_new_user(client,message);
      break;
    case GRAPPLE_MSG_USER_MSG:
      grapple_lobbyclient_process_user_msg(client,message);
      break;
    case GRAPPLE_MSG_GROUP_CREATE:
      grapple_lobbyclient_process_group_create(client,message);
      break;
    case GRAPPLE_MSG_GROUP_ADD:
      grapple_lobbyclient_process_group_add(client,message);
      break;
    case GRAPPLE_MSG_GROUP_REMOVE:
      grapple_lobbyclient_process_group_remove(client,message);
      break;
    case GRAPPLE_MSG_GROUP_DELETE:
      grapple_lobbyclient_process_group_delete(client,message);
      break;
    case GRAPPLE_MSG_CONNECTION_REFUSED:
      grapple_lobbyclient_process_connection_refused(client,message);
      break;
    case GRAPPLE_MSG_USER_DISCONNECTED:
      grapple_lobbyclient_process_user_disconnected(client,message);
      break;
    case GRAPPLE_MSG_SERVER_DISCONNECTED:
      grapple_lobbyclient_process_server_disconnected(client,message);
      break;
    case GRAPPLE_MSG_SESSION_NAME:
    case GRAPPLE_MSG_CONFIRM_RECEIVED:
    case GRAPPLE_MSG_CONFIRM_TIMEOUT:
    case GRAPPLE_MSG_YOU_ARE_HOST:
    case GRAPPLE_MSG_USER_NAME:
    case GRAPPLE_MSG_PING:
      //Dont care about these
      break;
    }

  grapple_message_dispose(message);

  return 0;
}

//Start the lobbyclient
int grapple_lobbyclient_start(grapple_lobbyclient lobbyclient)
{
  internal_lobbyclient_data *data;
  int returnval;

  data=internal_lobbyclient_get(lobbyclient);

  //Check the lobbyclients minimum defaults are set
  if (!data || !data->client)
    {
      return GRAPPLE_FAILED;
    }

  ////The name isnt set, cant connect to the lobby without a name
  if (!data->name)
    {
      grapple_lobbyclient_error_set(data,GRAPPLE_ERROR_NAME_NOT_SET);
      return GRAPPLE_FAILED;
    }

  //Set the grapple details for connecting to the lobby using grapple
  grapple_client_protocol_set(data->client,GRAPPLE_PROTOCOL_TCP);

  grapple_client_callback_setall(data->client,
				 grapple_lobbyclient_generic_callback,
				 (void *)data);


  //now set their connection status to pending
  data->connectstatus=GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_PENDING;

  //Start the client
  returnval=grapple_client_start(data->client,0);

  if (returnval!=GRAPPLE_OK)
    {
      grapple_lobbyclient_error_set(data,
				    grapple_client_error_get(data->client));
      return returnval;
    }

  grapple_client_sequential_set(data->client,GRAPPLE_SEQUENTIAL);

  //Connection status:

  //This will be changed in a callback that is run in the grapple
  //callback thread, so we just wait for it to change

  while (data->connectstatus == GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_PENDING)
    microsleep(10000);

  if (data->connectstatus==GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_CONNECTED)
    {
      while (data->firstroom==0)
	microsleep(10000);
	
      return GRAPPLE_OK;
    }

  //The name wasnt good, abort the connection
  free(data->name);
  data->name=NULL;

  if (data->connectstatus==GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_REJECTED)
    {
      grapple_lobbyclient_error_set(data,GRAPPLE_ERROR_NAME_NOT_UNIQUE);
      data->connectstatus=GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_DISCONNECTED;
    }
  else if (data->connectstatus==GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_DISCONNECTED)
    {
      grapple_lobbyclient_error_set(data,GRAPPLE_ERROR_CANNOT_CONNECT);
    }

  //Stop the client, ready to restart when a new name has been picked
  grapple_client_stop(data->client);

  return GRAPPLE_FAILED;
}

//Destroy the lobbyclient
int grapple_lobbyclient_destroy(grapple_lobbyclient lobbyclient)
{
  internal_lobbyclient_data *data;
  grapple_lobbygame_internal *gametarget;
  grapple_lobbyconnection *connection;
  grapple_lobbymessage *message;

  data=internal_lobbyclient_get(lobbyclient);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  //If we have a thread, close it first
  if (data->thread)
    {
      data->threaddestroy=1;

      while (data->threaddestroy)
	microsleep(10000);
    }

  //finish the grapple client
  if (data->client)
    grapple_client_destroy(data->client);

  //Remove this client from the list of lobby clients
  internal_lobbyclient_unlink(data);


  //Unlink all the games
  pthread_mutex_lock(&data->games_mutex);
  while (data->games)
    {
      gametarget=data->games;
      data->games=grapple_lobbygame_internal_unlink(data->games,data->games);
      grapple_lobbygame_internal_dispose(gametarget);
    }
  pthread_mutex_unlock(&data->games_mutex);

  //Unlink all the users
  pthread_mutex_lock(&data->userlist_mutex);
  while (data->userlist)
    {
      connection=data->userlist;
      data->userlist=grapple_lobbyconnection_unlink(data->userlist,
						    data->userlist);
      grapple_lobbyconnection_dispose(connection);
    }
  pthread_mutex_unlock(&data->userlist_mutex);

  //Unlink all the remaining incoming messages
  pthread_mutex_lock(&data->message_mutex);
  while (data->messages)
    {
      message=data->messages;
      data->messages=grapple_lobbymessage_unlink(data->messages,
						 data->messages);
      grapple_lobbymessage_dispose(message);
    }
  pthread_mutex_unlock(&data->message_mutex);

  //Unlink all the remaining callbacks
  pthread_mutex_lock(&data->callback_mutex);
  while (data->callbacks)
    {
      data->callbacks=grapple_lobbycallback_remove(data->callbacks,
						   data->callbacks->type);
    }
  pthread_mutex_unlock(&data->callback_mutex);

  pthread_mutex_destroy(&data->callback_mutex);
  pthread_mutex_destroy(&data->userlist_mutex);
  pthread_mutex_destroy(&data->message_mutex);
  pthread_mutex_destroy(&data->games_mutex);

  if (data->name)
    free(data->name);

  free(data);

  return GRAPPLE_OK;
}

//Create a room in the lobby. All rooms require someone to be in them,
//so creating a room will also move the user into it.
int grapple_lobbyclient_room_create(grapple_lobbyclient clientnum,
				    const char *name)
{
  internal_lobbyclient_data *client;
  grapple_user group;
  grapple_lobbyconnection *user;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  //Find if the group is already made
  group=grapple_client_group_from_name(client->client,name);

  if (!group)
    //Create a group if it isnt there
    group=grapple_client_group_create(client->client,name);


  //If they have a room already
  if (client->currentroom)
    //Move them out of it
    grapple_client_group_remove(client->client,client->currentroom,
				client->serverid);

  client->currentroom=group;
  //Move the player into the group (new room)
  grapple_client_group_add(client->client,group,client->serverid);

  //Set the current room of the user
  pthread_mutex_lock(&client->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(client->userlist,
					    client->serverid);
  if (user)
    user->currentroom=client->currentroom;
  pthread_mutex_unlock(&client->userlist_mutex);

  return GRAPPLE_OK;
}

//Enter an existing room
int grapple_lobbyclient_room_enter(grapple_lobbyclient clientnum,
				   grapple_lobbyroomid group)
{
  internal_lobbyclient_data *client;
  grapple_lobbyconnection *user;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  //Move out of the current room
  if (client->currentroom)
    grapple_client_group_remove(client->client,client->currentroom,
				client->serverid);
      
  client->currentroom=group;

  //Move the player into the new group (room)
  grapple_client_group_add(client->client,group,client->serverid);

  pthread_mutex_lock(&client->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(client->userlist,
					    client->serverid);
  if (user)
    user->currentroom=client->currentroom;
  pthread_mutex_unlock(&client->userlist_mutex);

  return GRAPPLE_OK;
}

//Leave a room (return to the main lobby)
int grapple_lobbyclient_room_leave(grapple_lobbyclient clientnum)
{
  internal_lobbyclient_data *client;
  grapple_lobbyconnection *user;

  client=internal_lobbyclient_get(clientnum);

  if (!client || !client->client)
    {
      return GRAPPLE_FAILED;
    }

  //If they are already in the main room, just OK it
  if (client->firstroom==client->currentroom)
    return GRAPPLE_OK;

  //Leave the current group, join the main one
  grapple_client_group_remove(client->client,client->currentroom,
			      client->serverid);

  grapple_client_group_add(client->client,client->firstroom,
			   client->serverid);
  
  client->currentroom=client->firstroom;

  //Update the user
  pthread_mutex_lock(&client->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(client->userlist,
					    client->serverid);
  if (user)
    user->currentroom=client->currentroom;
  pthread_mutex_unlock(&client->userlist_mutex);

  return 0;
}

//send a chat message - a message to everyone in the 'room'
int grapple_lobbyclient_chat(grapple_lobbyclient clientnum,
			     const char *message)
{
  internal_lobbyclient_data *client;
  char *outdata;
  intchar val;
  int length;
  
  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }


  //Make up the packet
  //4 bytes : protocol
  //4 bytes : message length
  //        : message

  length=strlen(message);

  outdata=(char *)malloc(length+4);

  val.i=htonl(GRAPPLE_LOBBYMESSAGE_CHAT);
  memcpy(outdata,val.c,4);

  memcpy(outdata+4,message,length);

  //Send the message to the current 'room' (group)
  grapple_client_send(client->client,client->currentroom,0,outdata,length+4);

  return 0;
}

//Starting a new game via the lobby.
//Here the user passes in a grapple_server that is already running, and the
//lobby extracts the information it requires

grapple_lobbygameid grapple_lobbyclient_game_register(grapple_lobbyclient clientnum,
						      grapple_server server)
{
  internal_lobbyclient_data *client;
  const char *session;
  const char *address;
  int port;
  int maxusers;
  int needpassword;
  grapple_protocol protocol;
  intchar val;
  char *outdata;
  int length,offset,sessionlength,addresslength,createval;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return 0;
    }

  if (client->gameid || client->ingame)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return 0;
    }

  //We have been passed a running server - lets find out if it has all the
  //requirements for a lobby game set.
  if (!grapple_server_running(server))
    { 
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_SERVER_NOT_CONNECTED);
      return 0;
   }

  port=grapple_server_port_get(server);
  if (!port)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_PORT_NOT_SET);
      return 0;
    }

  protocol=grapple_server_protocol_get(server);
  if (!protocol)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_PROTOCOL_NOT_SET);
      return 0;
    }

  session=grapple_server_session_get(server);
  if (!session)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_SESSION_NOT_SET);
      return 0;
    }

  //This is optional so no fail
  address=grapple_server_ip_get(server);

  maxusers=grapple_server_maxusers_get(server);

  needpassword=grapple_server_password_required(server);

  //We have all the information we need, now we assemble it into one huge
  //outgoing packet

  //set the length to be:
  length=20; //Ints for lobbyprotocol, port, protocol, maxusers, needpassword
    
  sessionlength=strlen(session);
  length+=(sessionlength+4); //The length of the session plus a length int

  if (address && *address)
    {
      addresslength=strlen(address);
      length+=(addresslength+4); //The length of the address plus a length int
    }
  else
    {
      addresslength=0;
      length+=4;
    }

  outdata=(char *)malloc(length);

  //Now copy the data into the buffer
  
  //4 bytes : Lobby protocol
  //4 bytes : Session name length
  //        ; Session name
  //4 bytes : Address length
  //        : address (may be 0 bytes)
  //4 bytes : portnumber
  //4 bytes : protocol
  //4 bytes : Maximum number of users
  //4 bytes : Password required (could be 1 byte but lets stick with ints)

  val.i=htonl(GRAPPLE_LOBBYMESSAGE_REGISTERGAME);
  memcpy(outdata,val.c,4);

  val.i=htonl(sessionlength);
  memcpy(outdata+4,val.c,4);

  memcpy(outdata+8,session,sessionlength);
  offset=sessionlength+8;

  val.i=htonl(addresslength);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  if (addresslength)
    {
      memcpy(outdata+offset,address,addresslength);
      offset=addresslength;
    }

  val.i=htonl(port);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(protocol);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(maxusers);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(needpassword);
  memcpy(outdata+offset,val.c,4);

  client->gameid=0;

  //We have the data!
  //Send it to the server
  grapple_client_send(client->client,GRAPPLE_SERVER,0,outdata,length);

  free(outdata);

  //Now wait for this game to appear on the list

  //This is changed via the grapple callback thread so will change
  //while we wait for it
  while (client->gameid==0)
    microsleep(10000);

  //Set to -1 means the game creation failed
  if (client->gameid==-1)
    {
      client->gameid=0;
      return 0;
    }

  //start up the subthread that monitors the game and keeps sending messages
  //to the lobby server about number of users etc
  client->runninggame=server;
  client->threaddestroy=0;

  //Move the client into the game itself
  client->ingame=1;

  //If they have a room already
  if (client->currentroom)
    //Move them out of it
    grapple_client_group_remove(client->client,client->currentroom,
				client->serverid);


  createval=-1;

  //Create the thread
  while(createval!=0)
    {
      createval=pthread_create(&client->thread,NULL,
			       grapple_lobbyclient_serverthread_main,
			       (void *)client);

      if (createval!=0)
	{
	  if (errno!=EAGAIN)
	    {
	      //Problem creating the thread that isnt a case of 'it will work
	      //later, dont create it
	      return 0;
	    }
	}
    }

  pthread_detach(client->thread);

  //Send the client the ID of the game
  return client->gameid;
}

//Stop running a game on the lobby
int grapple_lobbyclient_game_unregister(grapple_lobbyclient clientnum)
{
  char outdata[8];
  internal_lobbyclient_data *client;
  intchar val;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  //If the client isnt running a game, just nod and return
  if (!client->gameid)
    {
      return GRAPPLE_OK;
    }

  //If they have a room already
  if (client->currentroom)
    {
    //Move them back into it
      if (!grapple_client_group_add(client->client,client->currentroom,
				    client->serverid))
	{
	  //We couldnt move them into their old room, move them into the
	  //main room
	  grapple_client_group_add(client->client,client->firstroom,
				      client->serverid);
	  client->currentroom=client->firstroom;
	}
    }
  else
    {
      grapple_client_group_add(client->client,client->firstroom,
				  client->serverid);
      client->currentroom=client->firstroom;
    }

  //Send a message to the server to delete this game
  val.i=htonl(GRAPPLE_LOBBYMESSAGE_DELETEGAME);
  memcpy(outdata,val.c,4);

  val.i=htonl(client->gameid);
  memcpy(outdata+4,val.c,4);
  
  grapple_client_send(client->client,GRAPPLE_SERVER,0,outdata,8);

  //Reset all game variables
  client->gameid=0;
  client->ingame=0;
  client->runninggame=0;

  //Shutdown the game thread
  if (client->thread)
    {
      client->threaddestroy=1;

      //Wait for the thread to finish
      while (client->threaddestroy)
	microsleep(10000);
    }

  return GRAPPLE_OK;
}


//Join a game - we are passed a client which just needs to know where to go
int grapple_lobbyclient_game_join(grapple_lobbyclient clientnum,
				  grapple_lobbygameid gameid,
				  grapple_client newclient)
{
  internal_lobbyclient_data *client;
  grapple_lobbygame_internal *game;
  int returnval;
  int createval;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  //Only join one at a time
  if (client->ingame)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }
  
  pthread_mutex_lock(&client->games_mutex);

  //Find the game
  game=grapple_lobbygame_internal_locate_by_id(client->games,gameid);
  if (!game)
    {
      pthread_mutex_unlock(&client->games_mutex);
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CANNOT_CONNECT);
      return GRAPPLE_FAILED;
    }

  //Set the details on the client we have been passed
  grapple_client_address_set(newclient,game->address);
  grapple_client_port_set(newclient,game->port);
  grapple_client_protocol_set(newclient,game->protocol);

  pthread_mutex_unlock(&client->games_mutex);

  grapple_client_name_set(newclient,client->name);

  //Actually connect the client and return the return value
  returnval=grapple_client_start(newclient,GRAPPLE_WAIT);

  if (returnval!=GRAPPLE_OK)
    return returnval;

  client->joinedgame=newclient;

  //start up the subthread that monitors the game sends message to the lobby
  //if the client disconnects
  client->threaddestroy=0;

  //Move the client into the game itself
  client->ingame=1;

  //If they have a room already
  if (client->currentroom)
    //Move them out of it
    grapple_client_group_remove(client->client,client->currentroom,
				client->serverid);


  createval=-1;

  //Create the thread
  while(createval!=0)
    {
      createval=pthread_create(&client->thread,NULL,
			       grapple_lobbyclient_clientthread_main,
			       (void *)client);

      if (createval!=0)
	{
	  if (errno!=EAGAIN)
	    {
	      //Problem creating the thread that isnt a case of 'it will work
	      //later, dont create it
	      return GRAPPLE_FAILED;
	    }
	}
    }

  pthread_detach(client->thread);

  return GRAPPLE_OK;
}

//Client has told us they have left the game
int grapple_lobbyclient_game_leave(grapple_lobbyclient clientnum,
				   grapple_client oldclient)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  //They werent in one anyway!
  if (!client->ingame)
    {
      return GRAPPLE_OK;
    }

  client->joinedgame=0;

  //Just reset the ingame flag  
  client->ingame=0;

  //If they have a room already
  if (client->currentroom)
    {
      //Move them back into it
      if (!grapple_client_group_add(client->client,client->currentroom,
				    client->serverid))
	{
	  //We couldnt move them into their old room, move them into the
	  //main room
	  grapple_client_group_add(client->client,client->firstroom,
				      client->serverid);
	  client->currentroom=client->firstroom;
	}
    }
  else
    {
      grapple_client_group_add(client->client,client->firstroom,
				  client->serverid);
      client->currentroom=client->firstroom;
    }

  //Shutdown the game thread
  if (client->thread)
    {
      client->threaddestroy=1;

      //Wait for the thread to finish
      while (client->threaddestroy)
	microsleep(10000);
    }

  return grapple_client_stop(oldclient);
}

//Get the list of all rooms
grapple_lobbyroomid *grapple_lobbyclient_roomlist_get(grapple_lobbyclient
						      clientnum)
{
  internal_lobbyclient_data *client;
  int loopa,offset;
  grapple_lobbyroomid *returnval;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return NULL;
    }

  if (!client->client)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return NULL;
    }

  if (!client->firstroom)
    return NULL;

  //Use the lowlevel grapple function for the list of groups
  returnval=grapple_client_grouplist_get(client->client);

  if (returnval)
    {
      loopa=0;
      offset=0;
      
      while (returnval[loopa])
	{
	  if (offset)
	    returnval[loopa]=returnval[loopa+1];
	  else
	    {
	      if (returnval[loopa]==client->firstroom)
		{
		  returnval[loopa]=returnval[loopa+1];
		  offset=1;
		}
	    }
	  loopa++;
	}
    }

  return returnval;
}

//Find the name of a room
char *grapple_lobbyclient_roomname_get(grapple_lobbyclient clientnum,
				       grapple_lobbyroomid roomid)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return NULL;
    }

  if (!client->client)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return NULL;
    }

  //Use the lowlevel grapple function for the name of a group
  return grapple_client_groupname_get(client->client,roomid);
}

grapple_lobbyroomid grapple_lobbyclient_roomid_get(grapple_lobbyclient clientnum,
						     const char *name)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return 0;
    }

  if (!client->client)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return 0;
    }

  //Use the lowlevel grapple function for the name of a group
  return grapple_client_group_from_name(client->client,name);
}

//Users in a room
grapple_user *grapple_lobbyclient_roomusers_get(grapple_lobbyclient clientnum,
						grapple_lobbyroomid roomid)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return NULL;
    }

  if (!client->client)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return NULL;
    }

  //Use the lowlevel grapple function to find users in a group
  return grapple_client_groupusers_get(client->client,roomid);
}

//Find a list of games in this room
grapple_lobbygameid *grapple_lobbyclient_gamelist_get(grapple_lobbyclient clientnum,
						      grapple_user roomid)
{
  internal_lobbyclient_data *client;
  int count;
  grapple_lobbygameid *gamelist;
  grapple_lobbygame_internal *scan;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    return NULL;

  if (!client->client)
    {
      grapple_lobbyclient_error_set(client,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return NULL;
    }


  //First count the number of games
  pthread_mutex_lock(&client->games_mutex);

  scan=client->games;
  count=0;

  while (scan)
    {
      if (scan->room == roomid)
	//Only the ones in this room
	count++;

      scan=scan->next;
      if (scan==client->games)
	scan=NULL;
    }

  if (!count)
    {
      pthread_mutex_unlock(&client->games_mutex);
      //There werent any
      return NULL;
    }

  //Allocate the memory based on the count
  gamelist=
    (grapple_lobbygameid *)malloc((count+1)*sizeof(grapple_lobbygameid));

  scan=client->games;
  count=0;

  while (scan)
    {
      if (scan->room == roomid)
	//Set the value into the array
	gamelist[count++]=scan->id;

      scan=scan->next;
      if (scan==client->games)
	scan=NULL;
    }

  pthread_mutex_unlock(&client->games_mutex);
  
  //NULL the end of the array
  gamelist[count]=0;

  return gamelist;
}

//Find the details of a game, put them into a game structure
grapple_lobbygame *grapple_lobbyclient_game_get(grapple_lobbyclient clientnum,
						grapple_lobbygameid gameid)
{
  internal_lobbyclient_data *client;
  grapple_lobbygame *returnval=NULL;
  grapple_lobbygame_internal *game;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return NULL;
    }


  pthread_mutex_lock(&client->games_mutex);

  //Find the game
  game=grapple_lobbygame_internal_locate_by_id(client->games,gameid);

  if (game)
    {
      //Set up the retrun structure
      returnval=(grapple_lobbygame *)calloc(1,sizeof(grapple_lobbygame));
      
      returnval->gameid=game->id;
      returnval->currentusers=game->currentusers;
      returnval->maxusers=game->maxusers;
      returnval->needpassword=game->needpassword;
      returnval->room=game->room;
      returnval->closed=game->closed;
      
      returnval->name=malloc(strlen(game->session)+1);
      strcpy(returnval->name,game->session);
    }
	  
  pthread_mutex_unlock(&client->games_mutex);

  return returnval;
}

//Get rid of a set of game details passed to the user, freeing all memory
int grapple_lobbyclient_game_dispose(grapple_lobbygame *target)
{
  if (target->name)
    free(target->name);

  free(target);

  return GRAPPLE_OK;
}

//Set a callback. Callbacks are so that instead of needing to poll for
//messages, a callback can be set so that the messages are handled immediately
int grapple_lobbyclient_callback_set(grapple_lobbyclient clientnum,
				     grapple_lobbymessagetype message,
				     grapple_lobbycallback callback,
				     void *context)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&client->callback_mutex);

  //Add the callback to the list of callbacks
  client->callbacks=grapple_lobbycallback_add(client->callbacks,
					      message,callback,context);

  pthread_mutex_unlock(&client->callback_mutex);

  return GRAPPLE_OK;
}

//Set ALL callbacks to the function requested
int grapple_lobbyclient_callback_setall(grapple_lobbyclient client,
					grapple_lobbycallback callback,
					void *context)
{
  //Set one using the function above
  if (grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_ROOMLEAVE,
				       callback,context)==GRAPPLE_FAILED)
    return GRAPPLE_FAILED;

  //if one is ok, they all should be
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_ROOMENTER,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_ROOMCREATE,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_ROOMDELETE,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_CHAT,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_DISCONNECTED,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_NEWGAME,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_DELETEGAME,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_GAME_MAXUSERS,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_GAME_USERS,
				   callback,context);
  grapple_lobbyclient_callback_set(client,GRAPPLE_LOBBYMSG_GAME_CLOSED,
				   callback,context);

  return GRAPPLE_OK;
}

//Remove a callback
int grapple_lobbyclient_callback_unset(grapple_lobbyclient clientnum,
				       grapple_lobbymessagetype message)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&client->callback_mutex);

  //Remove the callback
  client->callbacks=grapple_lobbycallback_remove(client->callbacks,
						 message);

  pthread_mutex_unlock(&client->callback_mutex);

  return GRAPPLE_OK;
}

grapple_lobbyroomid grapple_lobbyclient_currentroomid_get(grapple_lobbyclient clientnum)
{
  internal_lobbyclient_data *client;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_FAILED;
    }

  //If they are in a game, they are in no room
  if (client->joinedgame || client->runninggame)
    return 0;
  //Otherwise they must be in a room - if it is 0 then they are just connecting
  //wait for the room

  while (client->currentroom==0 && 
	 client->connectstatus==GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_CONNECTED)
    microsleep(10000);
  
  return client->currentroom;
}

//Get the last error
grapple_error grapple_lobbyclient_error_get(grapple_lobbyclient clientnum)
{
  internal_lobbyclient_data *client;
  grapple_error returnval;

  client=internal_lobbyclient_get(clientnum);

  if (!client)
    {
      return GRAPPLE_ERROR_NOT_INITIALISED;
    }

  returnval=client->last_error;

  //Now wipe the last error
  client->last_error=GRAPPLE_NO_ERROR;

  return returnval;
}

