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

#ifndef GRAPPLE_LOBBY_H
#define GRAPPLE_LOBBY_H

#include "grapple_defines.h"
#include "grapple_error.h"
#include "grapple_protocols.h"
#include "grapple_types.h"

typedef int grapple_lobby;
typedef int grapple_lobbyclient;
typedef int grapple_lobbygameid;
typedef int grapple_lobbyroomid;

#define GRAPPLE_LOBBY_ENTRY_ROOM "Entry"

typedef enum
  {
    GRAPPLE_LOBBYMSG_ROOMLEAVE    = 1,
    GRAPPLE_LOBBYMSG_ROOMENTER,
    GRAPPLE_LOBBYMSG_ROOMCREATE,
    GRAPPLE_LOBBYMSG_ROOMDELETE,
    GRAPPLE_LOBBYMSG_CHAT,
    GRAPPLE_LOBBYMSG_DISCONNECTED,
    GRAPPLE_LOBBYMSG_NEWGAME,
    GRAPPLE_LOBBYMSG_DELETEGAME,
    GRAPPLE_LOBBYMSG_GAME_MAXUSERS,
    GRAPPLE_LOBBYMSG_GAME_USERS,
    GRAPPLE_LOBBYMSG_GAME_CLOSED,
  } grapple_lobbymessagetype;

typedef struct _grapple_lobbymessage
{
  grapple_lobbymessagetype type;

  union
  {
    struct 
    {
      grapple_user id;
      int length;
      char *message;
    } CHAT;
    struct 
    {
      grapple_lobbyroomid roomid;
      grapple_user userid;
      char *name;
    } ROOM;
    struct
    {
      grapple_lobbygameid id;
      char *name;
      int maxusers;
      int currentusers;
      int needpassword;
      int closed;
    } GAME;
  };

  struct _grapple_lobbymessage *next;
  struct _grapple_lobbymessage *prev;

} grapple_lobbymessage;

typedef struct
{
  grapple_lobbygameid gameid;
  char *name;
  int currentusers;
  int maxusers;
  int needpassword;
  grapple_lobbyroomid room;
  int closed;
} grapple_lobbygame;

//The callback typedef
typedef int(*grapple_lobbycallback)(grapple_lobbymessage *,void *);


/////////////////////SERVER//////////////////////

extern grapple_lobby grapple_lobby_init(const char *,const char *);
extern int grapple_lobby_ip_set(grapple_lobby,const char *);
extern int grapple_lobby_port_set(grapple_lobby,int);
extern int grapple_lobby_start(grapple_lobby);

extern int grapple_lobby_destroy(grapple_lobby);
extern grapple_error grapple_lobby_error_get(grapple_lobbyclient);

/////////////////////CLIENT//////////////////////

extern grapple_lobbyclient grapple_lobbyclient_init(const char *,const char *);
extern int grapple_lobbyclient_address_set(grapple_lobbyclient, const char *);
extern int grapple_lobbyclient_port_set(grapple_lobbyclient,int);
extern int grapple_lobbyclient_name_set(grapple_lobbyclient, const char *);
extern int grapple_lobbyclient_start(grapple_lobbyclient);
extern int grapple_lobbyclient_destroy(grapple_lobbyclient);

extern int grapple_lobbyclient_room_create(grapple_lobbyclient,const char *);
extern int grapple_lobbyclient_room_enter(grapple_lobbyclient,grapple_lobbyroomid);
extern int grapple_lobbyclient_room_leave(grapple_lobbyclient);
extern int grapple_lobbyclient_chat(grapple_lobbyclient,const char *);

extern grapple_lobbymessage *grapple_lobbyclient_message_pull(grapple_lobbyclient);

extern grapple_lobbygameid grapple_lobbyclient_game_register(grapple_lobbyclient,
							     grapple_server);
extern int grapple_lobbyclient_game_unregister(grapple_lobbyclient);
extern int grapple_lobbyclient_game_join(grapple_lobbyclient,
					 grapple_lobbygameid, grapple_client);
extern int grapple_lobbyclient_game_leave(grapple_lobbyclient,grapple_client);

extern grapple_lobbyroomid grapple_lobbyclient_currentroomid_get(grapple_lobbyclient);

extern grapple_lobbyroomid *grapple_lobbyclient_roomlist_get(grapple_lobbyclient);
extern char *grapple_lobbyclient_roomname_get(grapple_lobbyclient,
					      grapple_lobbyroomid);
extern grapple_lobbyroomid grapple_lobbyclient_roomid_get(grapple_lobbyclient,
							  const char *);

extern grapple_user *grapple_lobbyclient_roomusers_get(grapple_lobbyclient,
						       grapple_lobbyroomid);
extern grapple_lobbygameid *grapple_lobbyclient_gamelist_get(grapple_lobbyclient,
							     grapple_user);

extern grapple_lobbygame *grapple_lobbyclient_game_get(grapple_lobbyclient,grapple_lobbygameid);
extern int grapple_lobbyclient_game_dispose(grapple_lobbygame *);

extern int grapple_lobbyclient_callback_set(grapple_lobbyclient,
					    grapple_lobbymessagetype,
					    grapple_lobbycallback,
					    void *);
extern int grapple_lobbyclient_callback_setall(grapple_lobbyclient,
					       grapple_lobbycallback,
					       void *);
extern int grapple_lobbyclient_callback_unset(grapple_lobbyclient,
					      grapple_lobbymessagetype);

extern grapple_error grapple_lobbyclient_error_get(grapple_lobbyclient);


/////////////////////////OTHER//////////////////////
extern int grapple_lobbymessage_dispose(grapple_lobbymessage *);

#endif
