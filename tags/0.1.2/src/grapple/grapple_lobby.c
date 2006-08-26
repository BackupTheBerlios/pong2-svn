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

#include "grapple_lobby.h"
#include "grapple_lobby_internal.h"
#include "grapple_defines.h"
#include "grapple_error.h"
#include "grapple_server.h"
#include "grapple_lobbyconnection.h"
#include "grapple_lobbymessage.h"
#include "grapple_lobbygame.h"

/**************************************************************************
 ** The functions in this file are generally those that are accessible   **
 ** to the end user. Obvious exceptions are those that are static which  **
 ** are just internal utilities.                                         **
 ** Care should be taken to not change the parameters of outward facing  **
 ** functions unless absolutely required                                 **
 **************************************************************************/

//This is a static variable which keeps track of the list of all lobbys
//run by this program. The lobbys are kept in a linked list. This variable
//is global to this file only.
static internal_lobby_data *grapple_lobby_head=NULL;

//Link a lobby to the list
static int internal_lobby_link(internal_lobby_data *data)
{
  if (!grapple_lobby_head)
    {
      grapple_lobby_head=data;
      data->next=data;
      data->prev=data;
      return 1;
    }

  data->next=grapple_lobby_head;
  data->prev=grapple_lobby_head->prev;
  data->next->prev=data;
  data->prev->next=data;

  grapple_lobby_head=data;
  
  return 1;
}
//Remove a lobby from the linked list
static int internal_lobby_unlink(internal_lobby_data *data)
{
  if (data->next==data)
    {
      grapple_lobby_head=NULL;
      return 1;
    }

  data->next->prev=data->prev;
  data->prev->next=data->next;

  if (data==grapple_lobby_head)
    grapple_lobby_head=data->next;

  data->next=NULL;
  data->prev=NULL;

  return 1;
}

//Find the lobby from the ID number passed by the user
static internal_lobby_data *internal_lobby_get(grapple_lobby num)
{
  internal_lobby_data *scan;
  
  //By default if passed 0, then the oldest lobby is returned
  if (!num)
    return grapple_lobby_head;

  //This is a cache as most often you will want the same one as last time
  //Loop through the lobbys
  scan=grapple_lobby_head;

  while (scan)
    {
      if (scan->lobbynum==num)
	{
	  return scan;
	}
      
      scan=scan->next;
      if (scan==grapple_lobby_head)
	return NULL;
    }

  //No match
  return NULL;
}

//Create a new lobby
static internal_lobby_data *lobby_create(void)
{
  static int nextval=1;
  internal_lobby_data *data;
  pthread_mutexattr_t attr;

  //Create the structure
  data=(internal_lobby_data *)calloc(1,sizeof(internal_lobby_data));

  //Assign it a default ID
  data->lobbynum=nextval++;

  //Set up the mutexes
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&data->userlist_mutex,&attr);
  pthread_mutex_init(&data->message_mutex,&attr);
  pthread_mutex_init(&data->games_mutex,&attr);

  //Link it into the array of lobbies
  internal_lobby_link(data);

  return data;
}


//User function for initialising the lobby
grapple_lobby grapple_lobby_init(const char *name,const char *version)
{
  internal_lobby_data *data;

  //Create the internal data
  data=lobby_create();

  data->server=grapple_server_init(name,version);

  //Return the client ID - the end user only gets an integer, called a
  //'grapple_lobby'
  return data->lobbynum;
}

//Set the port number to connect to
int grapple_lobby_port_set(grapple_lobby lobby,int port)
{
  internal_lobby_data *data;

  //Get the lobby data
  data=internal_lobby_get(lobby);

  if (!data || !data->server)
    {
      return GRAPPLE_FAILED;
    }

  //Set this in the grapple data
  return grapple_server_port_set(data->server,port);
}

//Set the IP address to bind to. This is an optional, if not set, then all
//local addresses are bound to
int grapple_lobby_ip_set(grapple_lobby lobby,const char *ip)
{
  internal_lobby_data *data;

  //Get the lobby data
  data=internal_lobby_get(lobby);

  if (!data || !data->server)
    {
      return GRAPPLE_FAILED;
    }

  return grapple_server_ip_set(data->server,ip);
}

//Check if a room is empty, return 1 if it is
static int grapple_lobby_room_empty(internal_lobby_data *server,
				    grapple_user roomid)
{
  int returnval=0;
  grapple_user *userlist;
  grapple_lobbygame_internal *scan;
  int count;
  
  userlist=grapple_server_groupusers_get(server->server,roomid);
					 

  if (!userlist || !userlist[0])
    if (roomid!=server->mainroom)
      {
	//If the room is now empty, and this ISNT the main room, delete
	//the group (room)

	//also need to check if there are any games running in this room
	pthread_mutex_lock(&server->games_mutex);

	scan=server->games;
	count=0;
	
	while (scan && !count)
	  {
	    if (scan->room==roomid)
	      count=1;
	    scan=scan->next;
	    if (scan==server->games)
	      scan=NULL;
	  }

	pthread_mutex_unlock(&server->games_mutex);

	if (!count)
	  returnval=1;
      }

  free(userlist);

  return returnval;
}

//The lobby server has been passed a message to delete a game
static int grapple_lobby_process_lobbymsg_delete_game(internal_lobby_data *server,
							grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  char outdata[8];

  memcpy(val.c,message->USER_MSG.data+4,4);

  gameid=ntohl(val.i);

  pthread_mutex_lock(&server->games_mutex);
  //Locate the game
  game=grapple_lobbygame_internal_locate_by_id(server->games,gameid);

  //delete the game for the server
  if (game && game->owner==message->USER_MSG.id)
    {
      //Unlink from the list
      server->games=grapple_lobbygame_internal_unlink(server->games,game);
      
      
      //Its not in the list we can release the mutex now
      pthread_mutex_unlock(&server->games_mutex);

      //Send a message to all clients informing them
      val.i=htonl(GRAPPLE_LOBBYMESSAGE_DELETEGAME);
      memcpy(outdata,val.c,4);
	      
      val.i=htonl(game->id);
      memcpy(outdata+4,val.c,4);

      //Send the message
      grapple_server_send(server->server,GRAPPLE_EVERYONE,0,outdata,8);


      //Do NOT delete the room here cos all the users in the game are about
      //to be tossed back into this room. It will NOT be empty at this point

      grapple_lobbygame_internal_dispose(game);
    }
  else
    {
      pthread_mutex_unlock(&server->games_mutex);
    }

  return 0;
}

//The client has sent us a count of how many people are connected to their game
static int grapple_lobby_process_lobbymsg_game_usercount(internal_lobby_data *server,
							 grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  int count;

  memcpy(val.c,message->USER_MSG.data+4,4);
  gameid=ntohl(val.i);

  memcpy(val.c,message->USER_MSG.data+8,4);
  count=ntohl(val.i);

  pthread_mutex_lock(&server->games_mutex);
  game=grapple_lobbygame_internal_locate_by_id(server->games,gameid);

  //Only the owner can send the data
  if (game && game->owner==message->USER_MSG.id)
    {
      //Set the value in the game data
      game->currentusers=count;
      
      pthread_mutex_unlock(&server->games_mutex);

      //We can just resend the data, it is all correct as needed
      grapple_server_send(server->server,GRAPPLE_EVERYONE,0,
			  message->USER_MSG.data,12);
    }
  else
    pthread_mutex_unlock(&server->games_mutex);

  return 0;
}

//We have been told how many users at maximum a game can now have
static int grapple_lobby_process_lobbymsg_game_maxusercount(internal_lobby_data *server,
							    grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  int count;

  memcpy(val.c,message->USER_MSG.data+4,4);
  gameid=ntohl(val.i);

  memcpy(val.c,message->USER_MSG.data+8,4);
  count=ntohl(val.i);

  pthread_mutex_lock(&server->games_mutex);
  //FInd the game
  game=grapple_lobbygame_internal_locate_by_id(server->games,gameid);

  //Only do it if the user is the owner
  if (game && game->owner==message->USER_MSG.id)
    {
      //Set the value
      game->maxusers=count;
      
      pthread_mutex_unlock(&server->games_mutex);

      //We can just resend the data, it is all correct as needed
      grapple_server_send(server->server,GRAPPLE_EVERYONE,0,
			  message->USER_MSG.data,12);
    }
  else
    pthread_mutex_unlock(&server->games_mutex);

  return 0;
}

//We have been told if the game is open or closed
static int grapple_lobby_process_lobbymsg_game_closed(internal_lobby_data *server,
						      grapple_message *message)
{
  intchar val;
  grapple_lobbygameid gameid;
  grapple_lobbygame_internal *game;
  int state;

  memcpy(val.c,message->USER_MSG.data+4,4);
  gameid=ntohl(val.i);

  memcpy(val.c,message->USER_MSG.data+8,4);
  state=ntohl(val.i);

  pthread_mutex_lock(&server->games_mutex);

  //FInd the game
  game=grapple_lobbygame_internal_locate_by_id(server->games,gameid);

  //Only do it if the user is the owner
  if (game && game->owner==message->USER_MSG.id)
    {
      //Set the value
      game->closed=state;
      
      pthread_mutex_unlock(&server->games_mutex);

      //We can just resend the data, it is all correct as needed
      grapple_server_send(server->server,GRAPPLE_EVERYONE,0,
			  message->USER_MSG.data,12);
    }
  else
    pthread_mutex_unlock(&server->games_mutex);

  return 0;
}

//We have been asked to register a game
static int grapple_lobby_process_lobbymsg_register_game(internal_lobby_data *server,
							grapple_message *message)
{
  void *data;
  char *outdata;
  int length,outlength,addresslength,sessionlength;
  intchar val;
  int offset;
  grapple_lobbygame_internal *game;
  int varlength;
  static int gameid=1;
  int localgameid;
  grapple_lobbyconnection *user;

  length=message->USER_MSG.length-4;
  data=message->USER_MSG.data+4;

  if (length < 4)
    return 0;

  //Unpack all the data
  //4 bytes : Session name length
  //        ; Session name
  //4 bytes : Address length
  //        : address (may be 0 bytes)
  //4 bytes : portnumber
  //4 bytes : protocol
  //4 bytes : Maximum number of users
  //4 bytes : Password required (could be 1 byte but lets stick with ints)

  //Allocate a new grapple_lobbygame structure
  game=grapple_lobbygame_internal_create();


  memcpy(val.c,data,4);
  varlength=ntohl(val.i);

  game->session=(char *)malloc(varlength+1);
  memcpy(game->session,data+4,varlength);
  game->session[varlength]=0;
  offset=varlength+4;

  memcpy(val.c,data+offset,4);
  varlength=ntohl(val.i);
  offset+=4;

  if (varlength)
    {
      game->address=(char *)malloc(varlength+1);
      memcpy(game->address,data+offset,varlength);
      game->address[varlength]=0;
      offset+=varlength;
    }

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

  //The game structure is allocated.

  //Now check there is an address. If not, get one
  if (!game->address)
    {
      game->address=grapple_server_client_address_get(server->server,
						      message->USER_MSG.id);

      if (!game->address)
	{
	  //We have NO idea where the request actually came from - this
	  //should never happen, but if it does...

	  outdata=(char *)malloc(8);

	  val.i=htonl(GRAPPLE_LOBBYMESSAGE_YOURGAMEID);
	  memcpy(outdata,val.c,4);
	  
	  val.i=htonl(-1);
	  memcpy(outdata+4,val.c,4);
	  
	  //Sent this message - game ID is -1, to the client - that is a fail
	  grapple_server_send(server->server,message->USER_MSG.id,0,
			      outdata,8);
	  
	  free(outdata);
	  
	  return 0;
	}
    }

  localgameid=gameid++;
  game->id=localgameid;

  //Find the room that the game has been created in
  pthread_mutex_lock(&server->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(server->userlist,
					    message->USER_MSG.id);
  if (user)
    {
      game->room=user->currentroom;

      //This sets the users game so we can unlink the game when the user goes
      user->game=game->id;
    }

  pthread_mutex_unlock(&server->userlist_mutex);

  game->owner=message->USER_MSG.id;

  //set the length to be:
  outlength=28; /*Ints for lobbyprotocol, port, protocol, maxusers, 
		  needpassword , game ID and roomnumber */
  
  sessionlength=strlen(game->session);
  outlength+=(sessionlength+4); //The length of the session plus a length int

  addresslength=strlen(game->address);
  outlength+=(addresslength+4); //The length of the address plus a length int

  outdata=(char *)malloc(outlength);

  //Now we need to put together the more complicated data packet that is
  //showing the new game to the players.
  //4 bytes : Lobby protocol
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

  val.i=htonl(GRAPPLE_LOBBYMESSAGE_REGISTERGAME);
  memcpy(outdata,val.c,4);

  val.i=htonl(game->id);
  memcpy(outdata+4,val.c,4);

  val.i=htonl(sessionlength);
  memcpy(outdata+8,val.c,4);

  memcpy(outdata+12,game->session,sessionlength);
  offset=sessionlength+12;

  val.i=htonl(addresslength);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  memcpy(outdata+offset,game->address,addresslength);
  offset+=addresslength;
  
  val.i=htonl(game->port);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(game->protocol);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(game->maxusers);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(game->needpassword);
  memcpy(outdata+offset,val.c,4);
  offset+=4;

  val.i=htonl(game->room);
  memcpy(outdata+offset,val.c,4);

  //Link this into the servers list before we tell everyone about it
  pthread_mutex_lock(&server->games_mutex);
  server->games=grapple_lobbygame_internal_link(server->games,game);
  pthread_mutex_unlock(&server->games_mutex);

  //Send this message to everyone now, so they can all register the new game
  grapple_server_send(server->server,GRAPPLE_EVERYONE,0,outdata,outlength);

  free(outdata);

  //Now tell the client the new ID of their game
  outdata=(char *)malloc(8);
  
  val.i=htonl(GRAPPLE_LOBBYMESSAGE_YOURGAMEID);
  memcpy(outdata,val.c,4);
  
  val.i=htonl(localgameid);
  memcpy(outdata+4,val.c,4);
  
  grapple_server_send(server->server,message->USER_MSG.id,0,outdata,8);
  
  free(outdata);
  
  return 0;
}


//A generic user message. This is a grapple message containing user data,
//in this case, the data for the lobby protocol
//This gets handed off to protocol handling functions
static int grapple_lobby_process_user_msg(internal_lobby_data *server,
					  grapple_message *message)
{
  grapple_lobbymessagetype_internal type;
  intchar val;

  //User message - break it into its components

  if (message->USER_MSG.length < 4)
    return 0;

  memcpy(val.c,message->USER_MSG.data,4);
  type=ntohl(val.i);

  //Send off to a handler  
  switch (type)
    {
    case GRAPPLE_LOBBYMESSAGE_REGISTERGAME:
      grapple_lobby_process_lobbymsg_register_game(server,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_DELETEGAME:
      grapple_lobby_process_lobbymsg_delete_game(server,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_GAME_USERCOUNT:
      grapple_lobby_process_lobbymsg_game_usercount(server,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_GAME_MAXUSERCOUNT:
      grapple_lobby_process_lobbymsg_game_maxusercount(server,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_GAME_CLOSED:
      grapple_lobby_process_lobbymsg_game_closed(server,message);
      break;
    case GRAPPLE_LOBBYMESSAGE_CONNECTED:
    case GRAPPLE_LOBBYMESSAGE_CHAT:
    case GRAPPLE_LOBBYMESSAGE_DUPLICATENAME:
    case GRAPPLE_LOBBYMESSAGE_YOURGAMEID:
      //Never sent to the server
      break;
    }
  
  return 0;
}

//A new user has connected
static int grapple_lobby_process_new_user(internal_lobby_data *server,
					  grapple_message *message)
{
  grapple_lobbyconnection *newuser;

  //Create the users local data
  newuser=grapple_lobbyconnection_create();

  newuser->id=message->NEW_USER.id;
  
  pthread_mutex_lock(&server->userlist_mutex);
  server->userlist=grapple_lobbyconnection_link(server->userlist,newuser);
  pthread_mutex_unlock(&server->userlist_mutex);

  //We dont tell the clients, because
  //a) Grapple will tell them
  //b) we tell them to activate the client when a clients name is set as OK
  
  return 0;
}


static int grapple_lobby_process_user_name(internal_lobby_data *server,
					   grapple_message *message)
{
  int found=0;
  intchar val;
  char outdata[8];
  grapple_lobbyconnection *user;

  //Check there isnt a user by this name already - names are unique
  pthread_mutex_lock(&server->userlist_mutex);
  if (grapple_lobbyconnection_locate_by_name(server->userlist,
				       message->USER_NAME.name))
    found=1;
  pthread_mutex_unlock(&server->userlist_mutex);

  if (found)
    {
      val.i=htonl(GRAPPLE_LOBBYMESSAGE_DUPLICATENAME);
      //Send them the rejection message
      grapple_server_send(server->server,
			  message->USER_NAME.id,
			  0,val.c,4);

      //Dont disconnect them, they will close themself, and reconnect
      //with a new name

      return 0;
    }

  pthread_mutex_lock(&server->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(server->userlist,
					    message->USER_NAME.id);

  if (!user)
    {
      pthread_mutex_unlock(&server->userlist_mutex);
      //This user seems to have vanished
      return 0;
    }

  //The name is fine, allocate it to the user
  if (user->name)
    free(user->name);
  user->name=(char *)malloc(strlen(message->USER_NAME.name)+1);
  strcpy(user->name,message->USER_NAME.name);
  pthread_mutex_unlock(&server->userlist_mutex);

  user->connected=1;

  //Now tell all users that this user is connected
  
  val.i=htonl(GRAPPLE_LOBBYMESSAGE_CONNECTED);
  memcpy(outdata,val.c,4);

  val.i=htonl(message->USER_NAME.id);
  memcpy(outdata+4,val.c,4);
  
  //Send them the accept message
  grapple_server_send(server->server,
		      GRAPPLE_EVERYONE,
		      0,outdata,8);

  //Join the user to the entry group
  grapple_server_group_add(server->server,server->mainroom,
			   message->USER_NAME.id);
  user->currentroom=server->mainroom;
  
  //The user is now connected

  return 0;
}

//The user has added themself to a group - they have entered a room
static int grapple_lobby_process_group_add(internal_lobby_data *server,
					      grapple_message *message)
{
  grapple_lobbyconnection *user;


  //Change their current room in the user data
  pthread_mutex_lock(&server->userlist_mutex);
  user=grapple_lobbyconnection_locate_by_id(server->userlist,
					    message->GROUP.memberid);
  user->currentroom=message->GROUP.groupid;
  pthread_mutex_unlock(&server->userlist_mutex);

  return 0;
}


//The user has removed themself from a group - they have left a room
static int grapple_lobby_process_group_remove(internal_lobby_data *server,
					      grapple_message *message)
{
  //If the room is now empty, delete the room
  if (grapple_lobby_room_empty(server,message->GROUP.groupid))
    grapple_server_group_delete(server->server,message->GROUP.groupid);

  return 0;
}

//A user has disconnected
static int grapple_lobby_process_user_disconnected(internal_lobby_data *server,
					      grapple_message *message)
{
  grapple_lobbyconnection *user;
  grapple_lobbygame_internal *game;
  char outdata[8];
  intchar val;

  pthread_mutex_lock(&server->userlist_mutex);

  //Find the users details
  user=grapple_lobbyconnection_locate_by_id(server->userlist,
					    message->USER_DISCONNECTED.id);
					 
  if (user)
    {
      //Remove them from the list
      server->userlist=grapple_lobbyconnection_unlink(server->userlist,user);

      pthread_mutex_unlock(&server->userlist_mutex);
      
      //Remove them from their room (group)
      grapple_server_group_remove(server->server,user->currentroom,
				  user->id);

      //Check the room - is it now empty?

      //We dont do this if the user is in a game, because the users in the
      //game will be bailed out into the room when the game ends, so the room
      //needs to still be here
      if (!user->game)
	{
	  if (grapple_lobby_room_empty(server,user->currentroom))
	    grapple_server_group_delete(server->server,user->currentroom);
	}
      else
	{
	  //Find the users game and remove it
	  if (user->game)
	    {
	      
	      pthread_mutex_lock(&server->games_mutex);
	      game=grapple_lobbygame_internal_locate_by_id(server->games,user->game);
	      
	      if (game)
		{
		  server->games=grapple_lobbygame_internal_unlink(server->games,game);
		  pthread_mutex_unlock(&server->games_mutex);
		  
		  //Let everyone know that this game is gone now
		  val.i=htonl(GRAPPLE_LOBBYMESSAGE_DELETEGAME);
		  memcpy(outdata,val.c,4);
		  
		  val.i=htonl(user->game);
		  memcpy(outdata+4,val.c,4);
		  
		  grapple_server_send(server->server,GRAPPLE_EVERYONE,0,
				      outdata,8);
		  
		  grapple_lobbygame_internal_dispose(game);
		  
		  user->game=0;
		}
	      else
		pthread_mutex_unlock(&server->games_mutex);
	    } 
	}
      //Dispose of the user
      grapple_lobbyconnection_dispose(user);
    }
  else
    pthread_mutex_unlock(&server->userlist_mutex);
  
  return 0;
}

//A generic callback to handle all grapple messages that come through from the
//network
static int grapple_lobby_generic_callback(grapple_message *message,
					  void *context)
{
  internal_lobby_data *server;

  server=(internal_lobby_data *)context;

  //Hand off the message based on what it is
  switch (message->type)
    {
    case GRAPPLE_MSG_USER_NAME:
      grapple_lobby_process_user_name(server,message);
      break;
    case GRAPPLE_MSG_NEW_USER:
      grapple_lobby_process_new_user(server,message);
      break;
    case GRAPPLE_MSG_GROUP_REMOVE:
      grapple_lobby_process_group_remove(server,message);
      break;
    case GRAPPLE_MSG_GROUP_ADD:
      grapple_lobby_process_group_add(server,message);
      break;
    case GRAPPLE_MSG_USER_DISCONNECTED:
      grapple_lobby_process_user_disconnected(server,message);
      break;
    case GRAPPLE_MSG_USER_MSG:
      grapple_lobby_process_user_msg(server,message);
      break;
    case GRAPPLE_MSG_CONFIRM_RECEIVED:
    case GRAPPLE_MSG_CONFIRM_TIMEOUT:
    case GRAPPLE_MSG_SESSION_NAME:
    case GRAPPLE_MSG_GROUP_CREATE:
    case GRAPPLE_MSG_GROUP_DELETE:
    case GRAPPLE_MSG_PING:
      //Dont care about these ones
      break;
    case GRAPPLE_MSG_NEW_USER_ME:
    case GRAPPLE_MSG_YOU_ARE_HOST:
    case GRAPPLE_MSG_SERVER_DISCONNECTED:
    case GRAPPLE_MSG_CONNECTION_REFUSED:
      //These never come to the server
      break;
    }
  
  grapple_message_dispose(message);

  return 0;
}


//Start the lobby
int grapple_lobby_start(grapple_lobby lobby)
{
  internal_lobby_data *data;
  int returnval;

  data=internal_lobby_get(lobby);

  //Check the lobbys minimum defaults are set
  if (!data || !data->server)
    {
      return GRAPPLE_FAILED;
    }

  //Set the servers network details
  grapple_server_protocol_set(data->server,GRAPPLE_PROTOCOL_TCP);
  grapple_server_session_set(data->server,"Grapple Lobby");
  grapple_server_callback_setall(data->server,
				 grapple_lobby_generic_callback,
				 (void *)data);

  //Start the server
  returnval=grapple_server_start(data->server);


  if (returnval!=GRAPPLE_OK)
    return returnval;


  //Set up the room as the mainroom
  data->mainroom=grapple_server_group_create(data->server,
					     GRAPPLE_LOBBY_ENTRY_ROOM);
  
  return GRAPPLE_OK;
}


//Destroy the lobby
int grapple_lobby_destroy(grapple_lobby lobby)
{
  internal_lobby_data *data;
  grapple_lobbygame_internal *gametarget;
  grapple_lobbyconnection *connection;
  grapple_lobbymessage *message;

  data=internal_lobby_get(lobby);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  //Destrouy the grapple layer
  if (data->server)
    grapple_server_destroy(data->server);

  //remove it from the list
  internal_lobby_unlink(data);

  //Delete connected games
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

  //Delete the mutexes
  pthread_mutex_destroy(&data->userlist_mutex);
  pthread_mutex_destroy(&data->message_mutex);
  pthread_mutex_destroy(&data->games_mutex);

  free(data);

  return GRAPPLE_OK;
}

//Get the last error
grapple_error grapple_lobby_error_get(grapple_lobby num)
{
  internal_lobby_data *data;
  grapple_error returnval;

  data=internal_lobby_get(num);

  if (!data)
    {
      return GRAPPLE_ERROR_NOT_INITIALISED;
    }

  returnval=data->last_error;

  //Now wipe the last error
  data->last_error=GRAPPLE_NO_ERROR;

  return returnval;
}

