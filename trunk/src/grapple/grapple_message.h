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

#ifndef GRAPPLE_MESSAGE
#define GRAPPLE_MESSAGE

#include "grapple_types.h"

typedef enum
  {
    GRAPPLE_MSG_NEW_USER             =  1,
    GRAPPLE_MSG_NEW_USER_ME,
    GRAPPLE_MSG_USER_NAME,
    GRAPPLE_MSG_USER_MSG,
    GRAPPLE_MSG_SESSION_NAME,
    GRAPPLE_MSG_USER_DISCONNECTED,
    GRAPPLE_MSG_SERVER_DISCONNECTED,
    GRAPPLE_MSG_CONNECTION_REFUSED,
    GRAPPLE_MSG_PING,
    GRAPPLE_MSG_GROUP_CREATE,
    GRAPPLE_MSG_GROUP_ADD,
    GRAPPLE_MSG_GROUP_REMOVE,
    GRAPPLE_MSG_GROUP_DELETE,
    GRAPPLE_MSG_YOU_ARE_HOST,
    GRAPPLE_MSG_CONFIRM_RECEIVED,
    GRAPPLE_MSG_CONFIRM_TIMEOUT,
  } grapple_messagetype;

typedef enum
  {
    GRAPPLE_NOCONN_VERSION_MISMATCH    =  1,
    GRAPPLE_NOCONN_SERVER_FULL,
    GRAPPLE_NOCONN_SERVER_CLOSED,
    GRAPPLE_NOCONN_PASSWORD_MISMATCH,
  } grapple_connection_refused;

typedef struct
{
  grapple_messagetype type;
  union
  {
    struct 
    {
      grapple_user id;
      char *name;
    } USER_NAME;
    struct 
    {
      char *name;
    } SESSION_NAME;
    struct
    {
      grapple_user id;
      int me;
    } NEW_USER;
    struct
    {
      grapple_user id;
      void *data;
      int length;
    } USER_MSG;
    struct
    {
      grapple_user id;
    } USER_DISCONNECTED;
    struct
    {
      grapple_connection_refused reason;
    } CONNECTION_REFUSED;
    struct
    {
      grapple_user id;
      double pingtime;
    } PING;
    struct
    {
      grapple_user groupid;
      char *name;
      grapple_user memberid;
    } GROUP;
    struct
    {
      grapple_confirmid messageid;
      int usercount;
      grapple_user *timeouts;
    } CONFIRM;
  };
} grapple_message;

#ifdef __cplusplus
extern "C" {
#endif

  extern void grapple_message_dispose(grapple_message *);

#ifdef __cplusplus
}
#endif

#endif
