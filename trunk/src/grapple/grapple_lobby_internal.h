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

#ifndef GRAPPLE_LOBBY_INTERNAL_H
#define GRAPPLE_LOBBY_INTERNAL_H

#include <pthread.h>

#include "grapple_types.h"
#include "grapple_lobby.h"

#define GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_DISCONNECTED 0
#define GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_PENDING 1
#define GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_REJECTED 2
#define GRAPPLE_LOBBYCLIENT_CONNECTSTATUS_CONNECTED 3

typedef enum
  {
    GRAPPLE_LOBBYMESSAGE_DUPLICATENAME       = 1,
    GRAPPLE_LOBBYMESSAGE_CONNECTED           = 2,
    GRAPPLE_LOBBYMESSAGE_CHAT                = 3,
    GRAPPLE_LOBBYMESSAGE_REGISTERGAME        = 4,
    GRAPPLE_LOBBYMESSAGE_YOURGAMEID          = 5,
    GRAPPLE_LOBBYMESSAGE_DELETEGAME          = 6,
    GRAPPLE_LOBBYMESSAGE_GAME_USERCOUNT      = 7,
    GRAPPLE_LOBBYMESSAGE_GAME_MAXUSERCOUNT   = 8,
    GRAPPLE_LOBBYMESSAGE_GAME_CLOSED         = 9,
  } grapple_lobbymessagetype_internal;

typedef struct _grapple_lobbygame_internal
{
  grapple_lobbygameid id;
  char *session;
  char *address;
  int port;
  grapple_protocol protocol;
  int currentusers;
  int maxusers;
  int needpassword;
  int room;
  int closed;
  grapple_user owner;

  struct _grapple_lobbygame_internal *next;
  struct _grapple_lobbygame_internal *prev;
} grapple_lobbygame_internal;

typedef struct _grapple_lobbyconnection 
{
  char *name;
  grapple_user id;
  int connected;
  grapple_lobbygameid game;
  grapple_user currentroom;
  struct _grapple_lobbyconnection *next;
  struct _grapple_lobbyconnection *prev;
} grapple_lobbyconnection;

typedef struct _grapple_lobbycallback_internal
{
  grapple_lobbymessagetype type;
  void *context;
  grapple_lobbycallback callback;
  struct _grapple_lobbycallback_internal *next;
  struct _grapple_lobbycallback_internal *prev;
} grapple_lobbycallback_internal;


typedef struct _internal_lobby_data
{
  grapple_server server;
  grapple_lobby lobbynum;
  grapple_user mainroom;
  grapple_error last_error;
  pthread_mutex_t userlist_mutex;
  grapple_lobbyconnection *userlist;
  pthread_mutex_t message_mutex;
  grapple_lobbymessage *messages;
  pthread_mutex_t games_mutex;
  grapple_lobbygame_internal *games;
  pthread_mutex_t callback_mutex;
  grapple_lobbycallback_internal *callbacks;
  struct _internal_lobby_data *next;
  struct _internal_lobby_data *prev;
} internal_lobby_data;

typedef struct _internal_lobbyclient_data
{
  grapple_client client;
  grapple_lobbyclient lobbyclientnum;
  char *name;
  int connectstatus;
  grapple_lobbygameid gameid;
  int ingame;
  grapple_error last_error;
  pthread_t thread;
  int threaddestroy;
  grapple_server runninggame;
  grapple_client joinedgame;
  grapple_user currentroom;
  grapple_user firstroom;
  grapple_user serverid;
  pthread_mutex_t userlist_mutex;
  grapple_lobbyconnection *userlist;
  pthread_mutex_t message_mutex;
  grapple_lobbymessage *messages;
  pthread_mutex_t games_mutex;
  grapple_lobbygame_internal *games;
  pthread_mutex_t callback_mutex;
  grapple_lobbycallback_internal *callbacks;
  struct _internal_lobbyclient_data *next;
  struct _internal_lobbyclient_data *prev;
} internal_lobbyclient_data;

typedef union
{
  int i;
  char c[4];
} intchar;


#endif
