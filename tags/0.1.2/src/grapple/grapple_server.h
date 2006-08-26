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

#ifndef GRAPPLE_SERVER_H
#define GRAPPLE_SERVER_H

#include "grapple_callback.h"
#include "grapple_protocols.h"
#include "grapple_message.h"
#include "grapple_error.h"
#include "grapple_types.h"

#ifdef __cplusplus
extern "C" {
#endif

  extern grapple_server grapple_server_init(const char *,const char *);
  extern int grapple_server_port_set(grapple_server,int);
  extern int grapple_server_port_get(grapple_server);
  extern int grapple_server_ip_set(grapple_server,const char *);
  extern const char *grapple_server_ip_get(grapple_server);
  extern int grapple_server_protocol_set(grapple_server,grapple_protocol);
  extern grapple_protocol grapple_server_protocol_get(grapple_server);
  extern int grapple_server_session_set(grapple_server,const char *);
  extern const char *grapple_server_session_get(grapple_server);
  extern int grapple_server_start(grapple_server);
  extern int grapple_server_running(grapple_server);
  extern int grapple_server_stop(grapple_server);
  extern int grapple_server_destroy(grapple_server);

  extern int grapple_server_enumgrouplist(grapple_server,
					  grapple_user_enum_callback,
					  void *);
  extern int grapple_server_enumgroup(grapple_server,
				      grapple_user,
				      grapple_user_enum_callback,
				      void *);
  extern int grapple_server_enumusers(grapple_server,
				      grapple_user_enum_callback,
				      void *);


  extern int grapple_server_sequential_set(grapple_server,int);
  extern int grapple_server_sequential_get(grapple_server);

  extern int grapple_server_failover_set(grapple_server,int);

  extern int grapple_server_maxusers_set(grapple_server,int);
  extern int grapple_server_maxusers_get(grapple_server);
  extern int grapple_server_currentusers_get(grapple_server);

  extern int grapple_server_password_set(grapple_server,const char *);
  extern int grapple_server_password_required(grapple_server);

  extern int grapple_server_messagecount_get(grapple_server);
  extern int grapple_server_messages_waiting(grapple_server);

  extern grapple_message *grapple_server_message_pull(grapple_server);

  extern grapple_confirmid grapple_server_send(grapple_server,grapple_user,
					       int,void *,int);

  extern grapple_user *grapple_server_userlist_get(grapple_server);

  extern int grapple_server_callback_set(grapple_server,
					 grapple_messagetype,
					 grapple_callback,
					 void *);
  extern int grapple_server_callback_setall(grapple_server,
					    grapple_callback,
					    void *);
  extern int grapple_server_callback_unset(grapple_server,
					   grapple_messagetype);

  extern grapple_server grapple_server_default_get(void);

  extern int grapple_server_closed_get(grapple_server);
  extern void grapple_server_closed_set(grapple_server,int);

  extern int grapple_server_disconnect_client(grapple_server,grapple_user);

  extern int grapple_server_ping(grapple_server,grapple_user);
  extern double grapple_server_ping_get(grapple_server,grapple_user);
  extern int grapple_server_autoping(grapple_server,double);

  extern grapple_user grapple_server_group_create(grapple_server,const char *);
  extern int grapple_server_group_add(grapple_server,grapple_user,
				      grapple_user);
  extern int grapple_server_group_remove(grapple_server,grapple_user,
					 grapple_user);
  extern int grapple_server_group_delete(grapple_server,grapple_user);
  extern grapple_user grapple_server_group_from_name(grapple_server,const char *);
  extern grapple_user *grapple_server_groupusers_get(grapple_server,
						     grapple_user);

  extern grapple_user *grapple_server_grouplist_get(grapple_server);

  extern char *grapple_server_client_address_get(grapple_server,
						 grapple_user);
  extern char *grapple_server_groupname_get(grapple_server,grapple_user);

  extern grapple_error grapple_server_error_get(grapple_server);

#ifdef __cplusplus
}
#endif

#endif
