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
#include <unistd.h>

#include "grapple_defines.h"
#include "grapple_server.h"
#include "grapple_server_internal.h"
#include "grapple_server_thread.h"
#include "grapple_error_internal.h"
#include "grapple_queue.h"
#include "grapple_message_internal.h"
#include "grapple_connection.h"
#include "grapple_comms_api.h"
#include "grapple_callback.h"
#include "grapple_callback_internal.h"
#include "grapple_group.h"
#include "grapple_internal.h"
#include "socket.h"
#include "tools.h"
#include "prototypes.h"

/**************************************************************************
 ** The functions in this file are generally those that are accessible   **
 ** to the end user. Obvious exceptions are those that are static which  **
 ** are just internal utilities.                                         **
 ** Care should be taken to not change the parameters of outward facing  **
 ** functions unless absolutely required                                 **
 **************************************************************************/


//This is a static variable which keeps track of the list of all servers
//run by this program. The servers are kept in a linked list. This variable
//is global to this file only.
static internal_server_data *grapple_server_head=NULL;

//Link a server to the list
static int internal_server_link(internal_server_data *data)
{
  if (!grapple_server_head)
    {
      grapple_server_head=data;
      data->next=data;
      data->prev=data;
      return 1;
    }

  data->next=grapple_server_head;
  data->prev=grapple_server_head->prev;
  data->next->prev=data;
  data->prev->next=data;

  grapple_server_head=data;
  
  return 1;
}
//Remove a server from the linked list
static int internal_server_unlink(internal_server_data *data)
{
  if (data->next==data)
    {
      grapple_server_head=NULL;
      return 1;
    }

  data->next->prev=data->prev;
  data->prev->next=data->next;

  if (data==grapple_server_head)
    grapple_server_head=data->next;

  data->next=NULL;
  data->prev=NULL;

  return 1;
}

//Find the server from the ID number passed by the user
internal_server_data *internal_server_get(grapple_server num)
{
  internal_server_data *scan;
  
  //By default if passed 0, then the oldest server is returned
  if (!num)
    return grapple_server_head;

  //This is a cache as most often you will want the same one as last time

  //Loop through the servers
  scan=grapple_server_head;

  while (scan)
    {
      if (scan->servernum==num)
	{
	  return scan;
	}
      
      scan=scan->next;
      if (scan==grapple_server_head)
	return NULL;
    }

  //No match
  return NULL;
}

//Create a new server
static internal_server_data *server_create(void)
{
  static int nextval=1;
  internal_server_data *data;
  pthread_mutexattr_t attr;

  //Create the structure
  data=(internal_server_data *)calloc(1,sizeof(internal_server_data));

  //Assign it a default ID
  data->servernum=nextval++;

  //Create the mutexes we'll need
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  pthread_mutex_init(&data->connection_mutex,&attr);
  pthread_mutex_init(&data->group_mutex,&attr);
  pthread_mutex_init(&data->failover_mutex,&attr);
  pthread_mutex_init(&data->message_in_mutex,&attr);
  pthread_mutex_init(&data->callback_mutex,&attr);
  pthread_mutex_init(&data->confirm_mutex,&attr);
  pthread_mutex_init(&data->internal_mutex,&attr);

  data->user_serverid=65536;

  //Link it into the array of servers
  internal_server_link(data);

  return data;
}


//User function for initialising the server
grapple_server grapple_server_init(const char *name,const char *version)
{
  internal_server_data *data;

  //Create the internal data
  data=server_create();

  //Assign the user supplied values
  data->productname=(char *)malloc(strlen(name)+1);
  strcpy(data->productname,name);

  data->productversion=(char *)malloc(strlen(version)+1);
  strcpy(data->productversion,version);

  //Return the client ID - the end user only gets an integer, called a
  //'grapple_server'
  return data->servernum;
}

//Set the port number to connect to
int grapple_server_port_set(grapple_server server,int port)
{
  internal_server_data *data;

  //Get the server data
  data=internal_server_get(server);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->sock)
    {
      grapple_server_error_set(data,GRAPPLE_ERROR_SERVER_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Set the port
  data->port=port;

  return GRAPPLE_OK;
}

//Retrieve the port number
int grapple_server_port_get(grapple_server server)
{
  internal_server_data *data;

  //Get the server data
  data=internal_server_get(server);

  if (!data)
    {
      return 0;
    }

  //Return the port
  return data->port;
}

//Set the IP address to bind to. This is an optional, if not set, then all
//local addresses are bound to
int grapple_server_ip_set(grapple_server server,const char *ip)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  if (serverdata->sock)
    {
      grapple_server_error_set(serverdata,GRAPPLE_ERROR_SERVER_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Free the old data if set
  if (serverdata->ip)
    free (serverdata->ip);

  //Set the new value
  serverdata->ip=(char *)malloc(strlen(ip)+1);
  strcpy(serverdata->ip,ip);

  return GRAPPLE_OK;
}

//Get the IP address we have bound to
const char *grapple_server_ip_get(grapple_server server)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }
  
  //Send the IP back - this may or may not be NULL
  return serverdata->ip;
}


//Set the protocol this server must use
int grapple_server_protocol_set(grapple_server server,
				grapple_protocol protocol)
{
  internal_server_data *data;

  //Get the server
  data=internal_server_get(server);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->sock)
    {
      grapple_server_error_set(data,GRAPPLE_ERROR_SERVER_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Set the protocol
  data->protocol=protocol;

  return GRAPPLE_OK;
}

//Get the protocol
grapple_protocol grapple_server_protocol_get(grapple_server server)
{
  internal_server_data *data;

  //Get the server
  data=internal_server_get(server);

  if (!data)
    {
      return 0;
    }

  //Return the protocol
  return data->protocol;
}

//Find out if this server is running
int grapple_server_running(grapple_server server)
{
  internal_server_data *data;

  //Get the server
  data=internal_server_get(server);

  if (!data)
    {
      //No server, not running
      return 0;
    }

  if (data->sock)
    {
      if (socket_dead(data->sock))
	{
	  return 0;
	}

      //Have a live socket, running
      return 1;
    }

  //Otherwise, not running
  return 0;
}

//Set the maximum number of users that may connect to the server at any time
int grapple_server_maxusers_set(grapple_server server,int maxusers)
{
  internal_server_data *data;

  //Get the server
  data=internal_server_get(server);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }


  //Set the value
  data->maxusers=maxusers;

  return GRAPPLE_OK;
}

//Get the maximum number of users that may connect to the server at any time
int grapple_server_maxusers_get(grapple_server server)
{
  internal_server_data *data;

  //Get the server
  data=internal_server_get(server);

  if (!data)
    {
      return 0;
    }

  //Get the value
  return data->maxusers;
}

int grapple_server_currentusers_get(grapple_server server)
{
  internal_server_data *data;

  //Get the server
  data=internal_server_get(server);

  if (!data)
    {
      return 0;
    }

  //Get the value
  return data->usercount;
}

//Count the number of outstanding messages in the incoming queue
int grapple_server_messagecount_get(grapple_server server)
{
  internal_server_data *data;
  int returnval;

  //Find the server data
  data=internal_server_get(server);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&data->message_in_mutex);

  //Count the messages
  returnval=grapple_queue_count(data->message_in_queue);

  pthread_mutex_unlock(&data->message_in_mutex);

  //Return the count
  return returnval;

}

//return true if there are any messages waiting
int grapple_server_messages_waiting(grapple_server server)
{
  internal_server_data *data;

  data=internal_server_get(server);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->message_in_queue)
    return 1;
  else
    return 0;
}

//Start the server
int grapple_server_start(grapple_server server)
{
  internal_server_data *data;

  data=internal_server_get(server);

  //Check the servers minimum defaults are set
  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->sock)
    {
      grapple_server_error_set(data,GRAPPLE_ERROR_SERVER_CONNECTED);
      return GRAPPLE_FAILED;
    }

  if (!data->port)
    {
      grapple_server_error_set(data,GRAPPLE_ERROR_PORT_NOT_SET);
      return GRAPPLE_FAILED;
    }

  if (!data->port)
    {
      grapple_server_error_set(data,GRAPPLE_ERROR_PROTOCOL_NOT_SET);
      return GRAPPLE_FAILED;
    }

  if (!data->session)
    {
      grapple_server_error_set(data,GRAPPLE_ERROR_SESSION_NOT_SET);
      return GRAPPLE_FAILED;
    }

  switch (data->protocol)
    {
    case GRAPPLE_PROTOCOL_TCP:
      //Create a TCP listener socket
      data->sock=socket_create_inet_tcp_listener_on_ip(data->ip,data->port);
      break;
    case GRAPPLE_PROTOCOL_UDP:
      //Create a 2 way UDP listener socket
      data->sock=socket_create_inet_udp2way_listener_on_ip(data->ip,
							   data->port);
      break;
    }


  if (!data->sock)
    {
      //The socket couldnt be created
      grapple_server_error_set(data,GRAPPLE_ERROR_SERVER_CANNOT_BIND_SOCKET);
      return GRAPPLE_FAILED;
    }

  //Set the socket mode to be sequential if required
  if (data->sequential)
    socket_mode_set(data->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);
  else 
    socket_mode_unset(data->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);

  //Start up the wakeup socket. This is a socket that can break into the 
  //long timeout incoming loop, tell it that there is something to do locally
  data->wakesock=socket_create_interrupt();

  //Start the server thread that will handle all the communication
  grapple_server_thread_start(data);

  return GRAPPLE_OK;
}

//Pull the oldest message
grapple_message *grapple_server_message_pull(grapple_server server)
{
  internal_server_data *data;
  grapple_queue *queuedata;
  grapple_message *returnval=NULL;

  //Find the server data
  data=internal_server_get(server);

  if (!data)
    {
      return NULL;
    }
  
  pthread_mutex_lock(&data->message_in_mutex);
  if (data->message_in_queue)
    {
      //Remove the oldest message
      queuedata=data->message_in_queue;
      data->message_in_queue=
	queue_unlink(data->message_in_queue,data->message_in_queue);
      
      pthread_mutex_unlock(&data->message_in_mutex);


      /*Now we have the message, clone it into a new form useful for the end
	user*/
      returnval=server_convert_message_for_user(queuedata);
      
      //Get rid of the queue message
      queue_struct_dispose(queuedata);
    }
  else
    {
      pthread_mutex_unlock(&data->message_in_mutex);
    }
  
  //Return the message
  return returnval;
}

//This is the function used to send messages by the server to either
//the one or more clients, or a group
grapple_confirmid grapple_server_send(grapple_server server,
				      grapple_user serverid,
				      int flags,void *data,int datalen)
{
  internal_server_data *serverdata;
  grapple_connection *target,*scan;
  grapple_confirmid thismessageid=0;
  static int staticmessageid=1; /*This gets incrimented for each message
				  that is requiring confirmation*/
  int *group_data,loopa,count=0;

  //Find the data
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  if (flags & GRAPPLE_WAIT)
    flags |= GRAPPLE_CONFIRM;

  //This message requests a confirmation
  if (flags & GRAPPLE_CONFIRM)
    {
      //Set it a message ID
      thismessageid=staticmessageid++;
      flags|=GRAPPLE_RELIABLE;
    }

  switch (serverid)
    {
    case GRAPPLE_USER_UNKNOWN:
      //The target was the unknown user - cant send to this one
      break;
    case GRAPPLE_EVERYONE:
      //Sending a message to ALL players
      pthread_mutex_lock(&serverdata->connection_mutex);

      //Loop through all players
      scan=serverdata->userlist;
      while (scan)
	{
	  //Send a message to this one
	  s2c_message(serverdata,scan,flags,thismessageid,data,datalen);

	  //Count the number sent to
	  count++;
	  scan=scan->next;
	  if (scan==serverdata->userlist)
	    scan=0;
	}
      pthread_mutex_unlock(&serverdata->connection_mutex);
      break;
    default:
      //Sending to a specific single user or a group
      pthread_mutex_lock(&serverdata->connection_mutex);

      //Locate the user
      target=connection_from_serverid(serverdata->userlist,serverid);
      if (target)
	{
	  //Send to the user
	  s2c_message(serverdata,target,flags,thismessageid,data,datalen);

	  //Count it
	  count++;
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	}
      else
	{
	  //Cant find a user with that ID
	  pthread_mutex_unlock(&serverdata->connection_mutex);

	  //Try and send to a group instead, as there is no such user
	  pthread_mutex_lock(&serverdata->group_mutex);
	  if (group_locate(serverdata->groups,serverid))
	      {
		//We have a group that matches
		pthread_mutex_unlock(&serverdata->group_mutex);

		//Get the mist of all users in the group
		group_data=server_group_unroll(serverdata,serverid);
		
		//Loop through this array of ints
		loopa=0;
		while (group_data[loopa])
		  {
		    pthread_mutex_lock(&serverdata->connection_mutex);
		    
		    //Loop through the users
		    scan=serverdata->userlist;
		    while (scan)
		      {
			if (scan->serverid==group_data[loopa])
			  {
			    //The user is a match
			    //Send the message to them
			    s2c_message(serverdata,scan,flags,thismessageid,
					data,datalen);

			    //Count the send
			    count++;
			    break;
			  }
			
			scan=scan->next;
			if (scan==serverdata->userlist)
			  scan=0;
		      }
		    
		    pthread_mutex_unlock(&serverdata->connection_mutex);
		    loopa++;
		  }
		free(group_data);
	      }
	    else
	      {
		//Cant find any match for the user to send to
		pthread_mutex_unlock(&serverdata->group_mutex);
		grapple_server_error_set(serverdata,
					 GRAPPLE_ERROR_NO_SUCH_USER);
		return GRAPPLE_FAILED;
	      }
	}
      break;
    }

  //If we didnt send to anyone, but they requested a message be sent, we send
  //a confirm message right back to the server queue
  if (count == 0 && flags & GRAPPLE_CONFIRM)
    {
      s2SUQ_confirm_received(serverdata,thismessageid);
    }
  else
    {
      if (flags & GRAPPLE_WAIT)
	{
	  serverdata->sendwait=thismessageid;

	  while (serverdata->sendwait==thismessageid)
	    microsleep(1000);
	}
    }

  //Return the message ID
  return thismessageid;
}

//Destroy the server
int grapple_server_destroy(grapple_server server)
{
  internal_server_data *serverdata;
  grapple_queue *target;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Remove it from the list
  internal_server_unlink(serverdata);

  //Tell the thread to kill itself
  if (serverdata->thread)
    {
      serverdata->threaddestroy=1;

      pthread_mutex_lock(&serverdata->internal_mutex);
      if (serverdata->wakesock)
	socket_interrupt(serverdata->wakesock);
      pthread_mutex_unlock(&serverdata->internal_mutex);

      while (serverdata->threaddestroy==1 && serverdata->thread)
	microsleep(1000);

      serverdata->threaddestroy=0;
    }

  //Free memory
  if (serverdata->session)
    free(serverdata->session);
  if (serverdata->password)
    free(serverdata->password);
  if (serverdata->productname)
    free(serverdata->productname);
  if (serverdata->productversion)
    free(serverdata->productversion);


  //Delete the message queue
  while (serverdata->message_in_queue)
    {
      target=serverdata->message_in_queue;
      serverdata->message_in_queue=queue_unlink(serverdata->message_in_queue,
						serverdata->message_in_queue);
      queue_struct_dispose(target);
    }

  //Delete the mutexes
  pthread_mutex_destroy(&serverdata->message_in_mutex);
  pthread_mutex_destroy(&serverdata->connection_mutex);
  pthread_mutex_destroy(&serverdata->group_mutex);
  pthread_mutex_destroy(&serverdata->failover_mutex);
  pthread_mutex_destroy(&serverdata->callback_mutex);
  pthread_mutex_destroy(&serverdata->confirm_mutex);
  pthread_mutex_destroy(&serverdata->internal_mutex);
  
  //Free the last bit
  free(serverdata);

  return GRAPPLE_OK;
}

//Get an array of ints - the users connected
grapple_user *grapple_server_userlist_get(grapple_server server)
{
  internal_server_data *serverdata;

  //Get the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }

  //Return the array.
  return connection_server_intarray_get(serverdata);
}

//Set a callback. Callbacks are so that instead of needing to poll for
//messages, a callback can be set so that the messages are handled immediately
int grapple_server_callback_set(grapple_server server,
				grapple_messagetype message,
				grapple_callback callback,
				void *context)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&serverdata->callback_mutex);

  //Add the callback to the list of callbacks
  serverdata->callbackanchor=grapple_callback_add(serverdata->callbackanchor,
						  message,callback,context);

  pthread_mutex_unlock(&serverdata->callback_mutex);

  return GRAPPLE_OK;
}

//Set ALL callbacks to the function requested
int grapple_server_callback_setall(grapple_server server,
				   grapple_callback callback,
				   void *context)
{
  //Set one using the function above
  if (grapple_server_callback_set(server,GRAPPLE_MSG_NEW_USER,callback,
				  context)==GRAPPLE_FAILED)
    return GRAPPLE_FAILED;

  //if one is ok, they all should be
  grapple_server_callback_set(server,GRAPPLE_MSG_NEW_USER_ME,callback,context);
  grapple_server_callback_set(server,GRAPPLE_MSG_USER_MSG,callback,context);
  grapple_server_callback_set(server,GRAPPLE_MSG_USER_NAME,callback,context);
  grapple_server_callback_set(server,GRAPPLE_MSG_USER_MSG,callback,context);
  grapple_server_callback_set(server,GRAPPLE_MSG_SESSION_NAME,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_USER_DISCONNECTED,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_SERVER_DISCONNECTED,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_CONNECTION_REFUSED,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_PING,callback,context);
  grapple_server_callback_set(server,GRAPPLE_MSG_GROUP_CREATE,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_GROUP_ADD,callback,context);
  grapple_server_callback_set(server,GRAPPLE_MSG_GROUP_REMOVE,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_GROUP_DELETE,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_YOU_ARE_HOST,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_CONFIRM_RECEIVED,callback,
			      context);
  grapple_server_callback_set(server,GRAPPLE_MSG_CONFIRM_TIMEOUT,callback,
			      context);

  return GRAPPLE_OK;
}

//Remove a callback
int grapple_server_callback_unset(grapple_server server,
				  grapple_messagetype message)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&serverdata->callback_mutex);

  //Remove the callback
  serverdata->callbackanchor=grapple_callback_remove(serverdata->callbackanchor,
						     message);

  pthread_mutex_unlock(&serverdata->callback_mutex);

  return GRAPPLE_OK;
}

//Get the ID of the default server
grapple_server grapple_server_default_get()
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(0);

  if (serverdata)
    //Return its ID if we have it
    return serverdata->servernum;
  else
    //return 0 (the default anyway) if we dont
    return 0;
}

//Set the name of the session. This isnt functional it is cosmetic
int grapple_server_session_set(grapple_server server,const char *session)
{
  internal_server_data *serverdata;
  grapple_connection *scan;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  if (serverdata->sock)
    {
      grapple_server_error_set(serverdata,GRAPPLE_ERROR_SERVER_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Remove the old value
  if (serverdata->session)
    free (serverdata->session);

  //Set the new
  serverdata->session=(char *)malloc(strlen(session)+1);
  strcpy(serverdata->session,session);

  //If we have started
  if (serverdata->sock)
    {
      pthread_mutex_lock(&serverdata->connection_mutex);

      //Loop through all users and tell them the new session name      
      scan=serverdata->userlist;
      while (scan)
	{
	  //Tell this user
	  s2c_session_name(serverdata,scan,session);
	  
	  scan=scan->next;
	  if (scan==serverdata->userlist)
	    scan=0;
	}
      pthread_mutex_unlock(&serverdata->connection_mutex);
    }

  return GRAPPLE_OK;
}

//Get the name of the session.
const char *grapple_server_session_get(grapple_server server)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }

  return serverdata->session;
}

//Set the password required to connect.
int grapple_server_password_set(grapple_server server,const char *password)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Free the memory if it is already set
  if (serverdata->password)
    free (serverdata->password);

  //Set the new
  serverdata->password=(char *)malloc(strlen(password)+1);
  strcpy(serverdata->password,password);

  return GRAPPLE_OK;
}

//Find out if a password is required
int grapple_server_password_required(grapple_server server)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return 0;
    }

  ////If there is a password required - 1
  if (serverdata->password)
    return 1;

  //No password, return 0
  return 0;
}

//Find if the server is closed to new connections
int grapple_server_closed_get(grapple_server server)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Return the value
  return serverdata->closed;
}

//Set the server closed or open. Closed will completely stop any
//users from connecting to the server. The server will reject the handshake
void grapple_server_closed_set(grapple_server server,int state)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return;
    }

  //Set the internal value
  serverdata->closed=state;

  return;
}

//Force a client to drop, so the server can kick people off
int grapple_server_disconnect_client(grapple_server server,
				     grapple_user serverid)
{
  grapple_connection *target;

  internal_server_data *serverdata;
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&serverdata->connection_mutex);

  //Find the target
  target=connection_from_serverid(serverdata->userlist,serverid);

  if (!target)
    {
      pthread_mutex_unlock(&serverdata->connection_mutex);
      //Cant find that user
      grapple_server_error_set(serverdata,GRAPPLE_ERROR_NO_SUCH_USER);
      return GRAPPLE_FAILED;
    }

  //Send a delete message to the client
  s2c_failover_off(serverdata,target);
  s2c_disconnect(serverdata,target);

  //Set the target to be deleted next round of the server thread
  target->delete=1;

  pthread_mutex_unlock(&serverdata->connection_mutex);

  return GRAPPLE_OK;
}

//Stop the server - while keeping its data intact to start again
int grapple_server_stop(grapple_server server)
{
  internal_server_data *serverdata;
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }


  //Stop the thread
  if (serverdata->thread)
    {
      serverdata->threaddestroy=1;

      pthread_mutex_lock(&serverdata->internal_mutex);
      if (serverdata->wakesock)
	socket_interrupt(serverdata->wakesock);
      pthread_mutex_unlock(&serverdata->internal_mutex);

      //Wait for the thread to stop
      while (serverdata->threaddestroy==1 && serverdata->thread)
	microsleep(1000);

      serverdata->threaddestroy=0;
    }

  //All done, the server is now ready to restart if required
  return GRAPPLE_OK;
}

//Set the server into autoping mode. This will make the server ping all clients
//every frequency seconds.
int grapple_server_autoping(grapple_server server,double frequency)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Set the value
  serverdata->autoping=frequency;

  return GRAPPLE_OK;
}

//Manually ping a user
int grapple_server_ping(grapple_server server,grapple_user serverid)
{
  internal_server_data *serverdata;
  grapple_connection *user;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&serverdata->connection_mutex);

  //Find the user
  user=connection_from_serverid(serverdata->userlist,serverid);

  if (!user)
    {
      pthread_mutex_unlock(&serverdata->connection_mutex);
      grapple_server_error_set(serverdata,GRAPPLE_ERROR_NO_SUCH_USER);
      return GRAPPLE_FAILED;
    }


  //Send a ping. A reply will come back from the user in the form of a
  //queue message
  s2c_ping(serverdata,user,++user->pingnumber);

  gettimeofday(&user->pingstart,NULL);
  
  pthread_mutex_unlock(&serverdata->connection_mutex);

  return GRAPPLE_OK;
}

//Get the last recorded ping time for a specific user
double grapple_server_ping_get(grapple_server server,grapple_user serverid)
{
  internal_server_data *serverdata;
  grapple_connection *user;
  double returnval;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return 0;
    }

  pthread_mutex_lock(&serverdata->connection_mutex);
  //Get the user
  user=connection_from_serverid(serverdata->userlist,serverid);

  if (!user)
    {
      pthread_mutex_unlock(&serverdata->connection_mutex);
      grapple_server_error_set(serverdata,GRAPPLE_ERROR_NO_SUCH_USER);
      return 0;
    }

  //Find that users pingtime
  returnval=user->pingtime;

  pthread_mutex_unlock(&serverdata->connection_mutex);

  return returnval;
}

//Set failover mode on. Failover mode being where the server - if it dies -
//will be replaced by a new server from one fo the clients and all other
//clients will reconnect to the new server
int grapple_server_failover_set(grapple_server server,int value)
{
  internal_server_data *serverdata;
  grapple_connection *scan;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Set failover to be either on or off
  serverdata->failover=value;

  if (!serverdata->sock)
    {
      //This isnt a failure, we just cant tell anyone to failover yet, cos
      //nobody is connected
      return 0;
    }

  pthread_mutex_lock(&serverdata->connection_mutex);


  //Loop through each connected user  
  scan=serverdata->userlist;
  
  while (scan)
    {
      //Tell each user failover is either on or off
      if (value)
	s2c_failover_on(serverdata,scan);
      else
	{
	  s2c_failover_off(serverdata,scan);
	}
      
      scan=scan->next;
      if (scan==serverdata->userlist)
	scan=0;
    }
  pthread_mutex_unlock(&serverdata->connection_mutex);

  return GRAPPLE_OK;
}

//Set whether the server is in sequential mode or not
int grapple_server_sequential_set(grapple_server server,int value)
{
  internal_server_data *serverdata;
  grapple_connection *scan;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  if (value)
    {
      //Turn sequential on for the server
      serverdata->sequential=1;
      if (serverdata->sock)
	{
	  //Turn it on at the socket level
	  socket_mode_set(serverdata->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);

	  pthread_mutex_lock(&serverdata->connection_mutex);

	  //Loop all users and turn sequential on on the socket at this end
	  scan=serverdata->userlist;
	  while (scan)
	    {
	      scan->sequential=1;
	      if (scan->handshook)
		//Set the socket
		socket_mode_set(scan->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);

	      scan=scan->next;
	      if (scan==serverdata->userlist)
		scan=0;
	    }
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	}
    }
  else
    {
      //Turn sequential off for the server
      serverdata->sequential=0;
      if (serverdata->sock)
	{
	  //Turn it off at the socket level
	  socket_mode_unset(serverdata->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);

	  pthread_mutex_lock(&serverdata->connection_mutex);
	  //Loop all users and turn sequential off on the socket at this end
	  scan=serverdata->userlist;
	  while (scan)
	    {
	      scan->sequential=0;
	      if (scan->handshook)
		//Set the socket
		socket_mode_unset(scan->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);

	      scan=scan->next;
	      if (scan==serverdata->userlist)
		scan=0;
	    }
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	}
    }

  return GRAPPLE_OK;
}

//Find out if we are running sequential or not
int grapple_server_sequential_get(grapple_server server)
{
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Simply return the internal value
  return serverdata->sequential;
}

//Messages can be sent to groups, not just to users. This function
//returns the ID of a group from the name
grapple_user grapple_server_group_from_name(grapple_server server,const char *name)
{
  internal_server_data *serverdata;
  int returnval;
  internal_grapple_group *scan;

  serverdata=internal_server_get(server);

  pthread_mutex_lock(&serverdata->group_mutex);

  //Loop through all groups
  scan=serverdata->groups;

  while (scan)
    {
      //If the name matches
      if (!strcmp(scan->name,name))
	{
          //return this groups ID
	  returnval=scan->id;
	  pthread_mutex_unlock(&serverdata->group_mutex);
	  return returnval;
	}

      scan=scan->next;
      if (scan==serverdata->groups)
	scan=NULL;
    }

  pthread_mutex_unlock(&serverdata->group_mutex);

  //No ID to find

  return 0;
}

//create a group.
grapple_user grapple_server_group_create(grapple_server server,
					 const char *name)
{
  //create a group.
  
  internal_server_data *serverdata;
  int returnval;
  grapple_connection *scan;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Find the new ID
  returnval=serverdata->user_serverid++;

  //Now create a group locally
  create_server_group(serverdata,returnval,name);

  //Now go to each client and tell them there is a new group in town
  pthread_mutex_lock(&serverdata->connection_mutex);

  scan=serverdata->userlist;
  while (scan)
    {
      //Tell this user
      s2c_group_create(serverdata,scan,returnval,name);

      scan=scan->next;

      if (scan==serverdata->userlist)
	scan=0;
    }

  pthread_mutex_unlock(&serverdata->connection_mutex);

  //Return the ID of the group
  return returnval;
}

//Adding a user to a group. This will mean that any messages sent to the
//group will also be sent to that user
int grapple_server_group_add(grapple_server server,grapple_user group,
			     grapple_user add)
{
  internal_server_data *serverdata;
  grapple_connection *scan;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Now add to the group locally
  if (server_group_add(serverdata,group,add))
    {
        //Now go to each client and tell them there is a new member in this group
      pthread_mutex_lock(&serverdata->connection_mutex);

      scan=serverdata->userlist;
      while (scan)
	{
	  //Send the message
	  s2c_group_add(serverdata,scan,group,add);

	  scan=scan->next;
	  
	  if (scan==serverdata->userlist)
	    scan=0;
	}

      pthread_mutex_unlock(&serverdata->connection_mutex);

      return GRAPPLE_OK;
    }

  return GRAPPLE_FAILED;
}

//Remove a user from a group
int grapple_server_group_remove(grapple_server server,grapple_user group,
				grapple_user removeid)
{
  internal_server_data *serverdata;
  grapple_connection *scan;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Now remove a group member locally
  if (server_group_remove(serverdata,group,removeid))
    {
      pthread_mutex_lock(&serverdata->connection_mutex);

      //Tell all connected users
      scan=serverdata->userlist;
      while (scan)
	{
	  //Send the message to this user
	  s2c_group_remove(serverdata,scan,group,removeid);
	  
	  scan=scan->next;
	  if (scan==serverdata->userlist)
	    scan=0;
	}
      
      pthread_mutex_unlock(&serverdata->connection_mutex);

      return GRAPPLE_OK;
    }

  return GRAPPLE_FAILED;
}

//Delete a group entirely
int grapple_server_group_delete(grapple_server server,grapple_user group)
{
  grapple_connection *scan;
  internal_server_data *serverdata;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Now delete the group locally
  if (delete_server_group(serverdata,group))
    {
      //Now go to each client and tell them 
      pthread_mutex_lock(&serverdata->connection_mutex);
      
      scan=serverdata->userlist;
      while (scan)
	{
	  //Tell this user
	  s2c_group_delete(serverdata,scan,group);
	  
	  scan=scan->next;
	  if (scan==serverdata->userlist)
	    scan=0;
	}
      
      pthread_mutex_unlock(&serverdata->connection_mutex);

      return GRAPPLE_OK;
    }

  return GRAPPLE_FAILED;
}

grapple_user *grapple_server_groupusers_get(grapple_server server,
					    grapple_user groupid)
{
  internal_server_data *serverdata;
  grapple_user *userarray;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }

  //Get the user array
  userarray=server_group_unroll(serverdata,groupid);

  return userarray;
}

char *grapple_server_client_address_get(grapple_server server,
					grapple_user target)
{
  internal_server_data *serverdata;
  grapple_connection *user;
  char *returnval=0;
  const char *address;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }

  //Lock the user list
  pthread_mutex_lock(&serverdata->connection_mutex);

  //Locate the user
  user=connection_from_serverid(serverdata->userlist,target);

  if (user)
    {
      address=socket_host_get(user->sock);
      returnval=(char *)malloc(strlen(address)+1);
      strcpy(returnval,address);
    }

  pthread_mutex_unlock(&serverdata->connection_mutex);

  return returnval;
}


//Enumerate the users. Effectively this means run the passed callback
//function for each user in the group
int grapple_server_enumgroup(grapple_server server,
			     grapple_user groupid,
			     grapple_user_enum_callback callback,
			     void *context)
{
  internal_server_data *serverdata;
  int *userarray;
  grapple_user serverid;
  int loopa;
  grapple_connection *user;
  char *tmpname;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Get the user array
  userarray=server_group_unroll(serverdata,groupid);

  loopa=0;

  //Loop for each user
  while (userarray[loopa])
    {
      pthread_mutex_lock(&serverdata->connection_mutex);

      //Find the user
      user=connection_from_serverid(serverdata->userlist,userarray[loopa]);
      if (user)
	{
	  //Set the default values to an unnamed user
	  serverid=user->serverid;
	  tmpname=NULL;
	  if(user->name && *user->name)
	    {
	      //If the user has a name, note that
	      tmpname=(char *)malloc(strlen(user->name)+1);
	      strcpy(tmpname,user->name);
	    }

	  //Unlock the mutex, we are now only using copied data
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	  
	  //If the user is valid
	  if (serverid != GRAPPLE_USER_UNKNOWN)
	    {
	      //Run the callback
	      (*callback)(serverid,tmpname,0,context);
	    }
	  if (tmpname)
	    free(tmpname);
	}
      else
	{
	  //Unlock the mutex
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	}
      
      loopa++;
    }

  return GRAPPLE_OK;
}


//Enumerate the list of groups, running a user function for each group
int grapple_server_enumgrouplist(grapple_server server,
				 grapple_user_enum_callback callback,
				 void *context)
{
  internal_server_data *serverdata;
  int *grouplist;
  grapple_user groupid;
  int count;
  char *tmpname;
  internal_grapple_group *scan;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }


  //The rest of this is pretty inefficient, but it is done this way for a
  //reason. It is done to minimise the lock time on the group mutex,
  //as calling a user function with that mutex locked could be disasterous for
  //performance.

  //Get the group list into an array
  count=0;
  scan=serverdata->groups;

  pthread_mutex_lock(&serverdata->group_mutex);

  //Count them first so we can size the array
  while (scan)
    {
      count++;
      scan=scan->next;
      if (scan==serverdata->groups)
	scan=NULL;
    }
  
  if (!count)
    {
      pthread_mutex_unlock(&serverdata->group_mutex);
      return GRAPPLE_OK;
    }

  //The array allocation
  grouplist=(int *)malloc(count * (sizeof(int)));
  
  scan=serverdata->groups;
  count=0;

  //Insert the groups into it
  while (scan)
    {
      grouplist[count++]=scan->id;
      scan=scan->next;
      if (scan==serverdata->groups)
	scan=NULL;
    }

  pthread_mutex_unlock(&serverdata->group_mutex);

  //We now have the list of groups
  while (count>0)
    {
      //Loop backwards through the groups. We make no guarentee of enumeration
      //order
      groupid=grouplist[--count];
      pthread_mutex_lock(&serverdata->group_mutex);
      scan=group_locate(serverdata->groups,groupid);
      tmpname=NULL;
      if (scan)
	{
	  //If the group has a name, note that
	  tmpname=(char *)malloc(strlen(scan->name)+1);
	  strcpy(tmpname,scan->name);
	}
      //We're finished with the mutex, unlock it
      pthread_mutex_unlock(&serverdata->group_mutex);

      if (groupid)
	{
	  //Run the callback
	  (*callback)(groupid,tmpname,0,context);
	}

      if (tmpname)
	free(tmpname);
    }

  free(grouplist);

  return GRAPPLE_OK;
}

//Get an int array list of groups
grapple_user *grapple_server_grouplist_get(grapple_server server)
{
  internal_server_data *serverdata;
  int *grouplist;
  int count;
  internal_grapple_group *scan;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }

  //Get the group list into an array
  count=0;
  scan=serverdata->groups;

  pthread_mutex_lock(&serverdata->group_mutex);

  //Count them first so we can size the array
  while (scan)
    {
      count++;
      scan=scan->next;
      if (scan==serverdata->groups)
	scan=NULL;
    }
  
  if (!count)
    {
      pthread_mutex_unlock(&serverdata->group_mutex);
      return NULL;
    }

  //The array allocation
  grouplist=(int *)malloc((count+1) * (sizeof(int)));
  
  scan=serverdata->groups;
  count=0;

  //Insert the groups into it
  while (scan)
    {
      grouplist[count++]=scan->id;
      scan=scan->next;
      if (scan==serverdata->groups)
	scan=NULL;
    }

  pthread_mutex_unlock(&serverdata->group_mutex);

  grouplist[count]=0;

  //We now have the list of groups

  return grouplist;
}

//Enumerate the users. Effectively this means run the passed callback
//function for each user
int grapple_server_enumusers(grapple_server server,
			     grapple_user_enum_callback callback,
			     void *context)
{
  internal_server_data *serverdata;
  int *userarray;
  grapple_user serverid;
  int loopa;
  grapple_connection *user;
  char *tmpname;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_FAILED;
    }

  //Get the user array
  userarray=grapple_server_userlist_get(server);


  loopa=0;

  //Loop for each user
  while (userarray[loopa])
    {
      pthread_mutex_lock(&serverdata->connection_mutex);

      //Find the user
      user=connection_from_serverid(serverdata->userlist,userarray[loopa]);
      if (user)
	{
	  //Set the default values to an unnamed user
	  serverid=user->serverid;
	  tmpname=NULL;
	  if(user->name && *user->name)
	    {
	      //If the user has a name, note that
	      tmpname=(char *)malloc(strlen(user->name)+1);
	      strcpy(tmpname,user->name);
	    }

	  //Unlock the mutex, we are now only using copied data
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	  
	  //If the user is valid
	  if (serverid != GRAPPLE_USER_UNKNOWN)
	    {
	      //Run the callback
	      (*callback)(serverid,tmpname,0,context);
	    }
	  if (tmpname)
	    free(tmpname);
	}
      else
	{
	  //Unlock the mutex
	  pthread_mutex_unlock(&serverdata->connection_mutex);
	}
      
      loopa++;
    }

  return GRAPPLE_OK;
}

char *grapple_server_groupname_get(grapple_server server,grapple_user groupid)
{
  internal_server_data *serverdata;
  internal_grapple_group *group;
  char *groupname;

  //Find the server
  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return NULL;
    }

  pthread_mutex_lock(&serverdata->group_mutex);
  group=group_locate(serverdata->groups,groupid);

  if (!group)
    return NULL;

  groupname=(char *)malloc(strlen(group->name)+1);
  strcpy(groupname,group->name);

  return groupname;
}

//Get the last error
grapple_error grapple_server_error_get(grapple_server server)
{
  internal_server_data *serverdata;

  grapple_error returnval;

  serverdata=internal_server_get(server);

  if (!serverdata)
    {
      return GRAPPLE_ERROR_NOT_INITIALISED;
    }

  returnval=serverdata->last_error;

  //Now wipe the last error
  serverdata->last_error=GRAPPLE_NO_ERROR;

  return returnval;
}

