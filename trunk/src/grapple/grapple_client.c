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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "grapple_defines.h"
#include "grapple_callback.h"
#include "grapple_callback_internal.h"
#include "grapple_client.h"
#include "grapple_client_internal.h"
#include "grapple_client_thread.h"
#include "grapple_comms_api.h"
#include "grapple_queue.h"
#include "grapple_error_internal.h"
#include "grapple_message_internal.h"
#include "grapple_internal.h"
#include "grapple_group.h"
#include "grapple_connection.h"
#include "prototypes.h"
#include "tools.h"

/**************************************************************************
 ** The functions in this file are generally those that are accessible   **
 ** to the end user. Obvious exceptions are those that are static which  **
 ** are just internal utilities.                                         **
 ** Care should be taken to not change the parameters of outward facing  **
 ** functions unless absolutely required                                 **
 **************************************************************************/

//This is a static variable which keeps track of the list of all clients
//run by this program. The clients are kept in a linked list. This variable
//is global to this file only.
static internal_client_data *grapple_client_head=NULL;

//Link a client into the list
static int internal_client_link(internal_client_data *data)
{
  if (!grapple_client_head)
    {
      grapple_client_head=data;
      data->next=data;
      data->prev=data;
      return 1;
    }

  data->next=grapple_client_head;
  data->prev=grapple_client_head->prev;
  data->next->prev=data;
  data->prev->next=data;

  grapple_client_head=data;

  return 1;
}

//Remove a client from the linked list
static int internal_client_unlink(internal_client_data *data)
{
  if (data->next==data)
    {
      grapple_client_head=NULL;
      return 1;
    }

  data->next->prev=data->prev;
  data->prev->next=data->next;

  if (data==grapple_client_head)
    grapple_client_head=data->next;

  data->next=NULL;
  data->prev=NULL;

  return 1;
}

//Find the client from the ID number passed by the user
static internal_client_data *internal_client_get(grapple_client num)
{
  internal_client_data *scan;

  //By default if passed 0, then the oldest client is returned
  if (!num)
    return grapple_client_head;

  //This is a cache as most often you will want the same one as last time

  //Loop through the clients
  scan=grapple_client_head;

  while (scan)
    {
      if (scan->clientnum==num)
	{
	  //Match and return it
	  return scan;
	}

      scan=scan->next;
      if (scan==grapple_client_head)
	return NULL;
    }

  //No match
  return NULL;
}

//Create a new client
static internal_client_data *client_create(void)
{
  static int nextval=256; /*A unique value for the clients ID. This will be
			    changed by the server, but is a good unique start*/
  internal_client_data *data;
  pthread_mutexattr_t attr;

  //Create the structure
  data=(internal_client_data *)calloc(1,sizeof(internal_client_data));

  //Assign it some default values
  data->clientnum=nextval++;
  data->serverid=GRAPPLE_USER_UNKNOWN;

  //Create the mutexes we'll need
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  pthread_mutex_init(&data->message_in_mutex,&attr);
  pthread_mutex_init(&data->message_out_mutex,&attr);
  pthread_mutex_init(&data->connection_mutex,&attr);
  pthread_mutex_init(&data->group_mutex,&attr);
  pthread_mutex_init(&data->failover_mutex,&attr);
  pthread_mutex_init(&data->callback_mutex,&attr);
  pthread_mutex_init(&data->internal_mutex,&attr);

  //Link it into the array of clients
  internal_client_link(data);

  return data;
}

//User function for initialising the client
grapple_client grapple_client_init(const char *name,const char *version)
{
  internal_client_data *data;

  //Create the internal data
  data=client_create();

  //Assign the user supplied values
  data->productname=(char *)malloc(strlen(name)+1);
  strcpy(data->productname,name);

  data->productversion=(char *)malloc(strlen(version)+1);
  strcpy(data->productversion,version);

  //Return the client ID - the end user only gets an integer, called a
  //'grapple_client'

  return data->clientnum;
}

//Set the address to connect to
int grapple_client_address_set(grapple_client client,const char *address)
{
  internal_client_data *data;

  //Locate the client
  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  //Cant set this if we're connected already
  if (data->sock)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }


  //If we set it to NULL, then use localhost
  if (!address || !*address)
    {
      address="127.0.0.1";
    }

  if (data->address)
    free(data->address);

  //Set the value into the client
  data->address=(char *)malloc(strlen(address)+1);
  strcpy(data->address,address);

  //OK
  return GRAPPLE_OK;
}


//Set the port number to connect to
int grapple_client_port_set(grapple_client client,int port)
{
  internal_client_data *data;

  //Get the client data
  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->sock)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Set the port
  data->port=port;

  return GRAPPLE_OK;
}

//Set the protocol this connection must use
int grapple_client_protocol_set(grapple_client client,
				grapple_protocol protocol)
{
  internal_client_data *data;

  //Get the client
  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->sock)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Set the protocol
  data->protocol=protocol;

  return GRAPPLE_OK;
}

//Set the password that the client must use to connect to the server
int grapple_client_password_set(grapple_client client,const char *password)
{
  internal_client_data *data;

  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->sock)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  if (data->password)
    free(data->password);

  data->password=(char *)malloc(strlen(password)+1);
  strcpy(data->password,password);

  return GRAPPLE_OK;
}

int grapple_client_start(grapple_client client,int flags)
{
  internal_client_data *data;

  //Find the client data struct
  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  //Already connected?
  if (data->sock)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_CLIENT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Check all required values are initialised
  if (!data->address)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_ADDRESS_NOT_SET);
      return GRAPPLE_FAILED;
    }

  if (!data->port)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_PORT_NOT_SET);
      return GRAPPLE_FAILED;
    }

  if (!data->protocol)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_PROTOCOL_NOT_SET);
      return GRAPPLE_FAILED;
    }

  //Start a network connection - either 2 way UDP or TCP
  switch (data->protocol)
    {
    case GRAPPLE_PROTOCOL_TCP:
      data->sock=socket_create_inet_tcp_wait(data->address,data->port,1);
      break;
    case GRAPPLE_PROTOCOL_UDP:
      data->sock=socket_create_inet_udp2way_wait(data->address,data->port,1);
      data->connecting=1;
      break;
    }

  //The connection couldnt be created.
  if (!data->sock)
    {
      grapple_client_error_set(data,GRAPPLE_ERROR_CANNOT_CONNECT);
      return GRAPPLE_FAILED;
    }

  //Set this to be sequential for the moment, to ensure the handshake
  //goes in properly
  socket_mode_set(data->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);

  //Start up the wakeup socket. This is a socket that can break into the
  //long timeout incoming loop, tell it that there is something to do locally
  data->wakesock=socket_create_interrupt();

  //Start the client thread. This thread handles the sockets, processes the
  //data, and passes data back to the main thread in the form of a message
  //queue
  grapple_client_thread_start(data);
  return GRAPPLE_OK;
}

//report whether the client is connected to the server
int grapple_client_connected(grapple_client client)
{
  internal_client_data *data;

  data=internal_client_get(client);

  if (!data)
    return 0;

  if (!data->sock)
    return 0;

  if (socket_dead(data->sock))
    return 0;

  if (data->serverid)
    return 1;

  return 0;
}

//Set the name
int grapple_client_name_set(grapple_client client,const char *name)
{
  internal_client_data *data;

  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&data->internal_mutex);

  if (data->name_provisional)
    free(data->name_provisional);

  //The value is 'provisional' cos we havent been told by the server we can
  //use this name yet
  data->name_provisional=(char *)malloc(strlen(name)+1);
  strcpy(data->name_provisional,name);

  pthread_mutex_unlock(&data->internal_mutex);

  //Tell the server this is the name we want - as long as the server is
  //connected
  if (data->sock)
    c2s_set_name(data,name);

  return GRAPPLE_OK;

}

//Get the name of a client
char *grapple_client_name_get(grapple_client client,grapple_user serverid)
{
  internal_client_data *data;
  char *returnval;
  grapple_connection *user;

  //Find the client
  data=internal_client_get(client);

  if (!data)
    {
      return NULL;
    }

  //We are getting ourown pre-auth name
  if (serverid==GRAPPLE_USER_UNKNOWN)
    {

      pthread_mutex_lock(&data->internal_mutex);

      //So check if it has a proivisional name
      if (!data->name_provisional)
	{
	  pthread_mutex_unlock(&data->internal_mutex);
	  return NULL;
	}

      //Make a copy of the provisional name - as this can be deleted at any
      //moment
      returnval=(char *)malloc(strlen(data->name_provisional)+1);
      strcpy(returnval,data->name_provisional);
      pthread_mutex_unlock(&data->internal_mutex);
      return returnval;
    }

  pthread_mutex_lock(&data->connection_mutex);

  //Look for the ID that matches the request
  user=connection_from_serverid(data->userlist,serverid);
  if (!user)
    {
      //No such ID
      pthread_mutex_unlock(&data->connection_mutex);
      return NULL;
    }

  //Copy this ID's name
  if (user->name && *user->name)
    {
      returnval=(char *)malloc(strlen(user->name)+1);
      strcpy(returnval,user->name);
    }
  else
    {
      returnval=NULL;
    }

  pthread_mutex_unlock(&data->connection_mutex);

  //return it
  return returnval;
}

//Count the number of outstanding messages in the users incoming queue
int grapple_client_messagecount_get(grapple_client client)
{
  internal_client_data *data;
  int returnval;

  //Find the client data
  data=internal_client_get(client);

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
int grapple_client_messages_waiting(grapple_client client)
{
  internal_client_data *data;

  data=internal_client_get(client);

  if (!data)
    {
      return GRAPPLE_FAILED;
    }

  if (data->message_in_queue)
    return 1;
  else
    return 0;
}

//Pull the oldest message
grapple_message *grapple_client_message_pull(grapple_client client)
{
  internal_client_data *data;
  grapple_queue *queuedata;
  grapple_message *returnval=NULL;

  //Find the client data
  data=internal_client_get(client);

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
      returnval=client_convert_message_for_user(queuedata);

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

//This is the function used to send messages by the client to either
//the server or to other clients
grapple_confirmid grapple_client_send(grapple_client client,
				      grapple_user target,
				      int flags,void *data,int datalen)
{
  internal_client_data *clientdata;
  grapple_confirmid thismessageid=0;
  static int staticmessageid=1; /*This gets incrimented for each message
				  that is requiring confirmation*/

  //Find the data
  clientdata=internal_client_get(client);

  if (!clientdata)
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

  switch (target)
    {
    case GRAPPLE_USER_UNKNOWN:
      //The target was the unknown user - cant send to this one
      break;
    case GRAPPLE_SERVER:
      //Sending a message to the server
      c2s_message(clientdata,flags,thismessageid,data,datalen);
      break;
    case GRAPPLE_EVERYONE:
      //Sending a message to ALL players
      c2s_relayallmessage(clientdata,flags,thismessageid,data,datalen);
      break;
    case GRAPPLE_EVERYONEELSE:
      //Sending a message to all OTHER players
      c2s_relayallbutselfmessage(clientdata,flags,thismessageid,data,datalen);
      break;
    default:
      //Sending a message to a specific player
      c2s_relaymessage(clientdata,target,flags,thismessageid,data,datalen);
      break;
    }

  if (flags & GRAPPLE_WAIT)
    {
      clientdata->sendwait=thismessageid;

      while (clientdata->sendwait==thismessageid)
	microsleep(1000);
    }
  //Return the message ID - will be 0 if no confirmation was requested
  return thismessageid;
}

//Destroy the client
int grapple_client_destroy(grapple_client client)
{
  internal_client_data *clientdata;
  grapple_queue *target;

  //Find the client to kill
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      //There is no client to kill
      return GRAPPLE_FAILED;
    }

  //Disconnect the client from the server
  if (clientdata->thread)
    c2s_disconnect(clientdata);

  //Unlink the client from the list of clients
  internal_client_unlink(clientdata);

  //Kill the thread
  if (clientdata->thread)
    {
      clientdata->threaddestroy=1;

      pthread_mutex_lock(&clientdata->internal_mutex);
      if (clientdata->wakesock)
	socket_interrupt(clientdata->wakesock);
      pthread_mutex_unlock(&clientdata->internal_mutex);

      //Wait for the thread to go.
      while (clientdata->threaddestroy==1 && clientdata->thread)
	microsleep(1000);
    }

  //Free memory
  if (clientdata->address)
    free(clientdata->address);
  if (clientdata->name_provisional)
    free(clientdata->name_provisional);
  if (clientdata->name)
    free(clientdata->name);
  if (clientdata->session)
    free(clientdata->session);
  if (clientdata->password)
    free(clientdata->password);
  if (clientdata->productname)
    free(clientdata->productname);
  if (clientdata->productversion)
    free(clientdata->productversion);

  //Delete the thread mutexes
  pthread_mutex_destroy(&clientdata->message_in_mutex);
  pthread_mutex_destroy(&clientdata->message_out_mutex);
  pthread_mutex_destroy(&clientdata->connection_mutex);
  pthread_mutex_destroy(&clientdata->group_mutex);
  pthread_mutex_destroy(&clientdata->failover_mutex);
  pthread_mutex_destroy(&clientdata->callback_mutex);
  pthread_mutex_destroy(&clientdata->internal_mutex);

  //Remove messages in the queue
  while (clientdata->message_in_queue)
    {
      target=clientdata->message_in_queue;
      clientdata->message_in_queue=queue_unlink(clientdata->message_in_queue,
						clientdata->message_in_queue);
      queue_struct_dispose(target);
    }

  //Thats it, done.
  free(clientdata);

  return GRAPPLE_OK;
}


//Get an array of connected users
grapple_user *grapple_client_userlist_get(grapple_client client)
{
  internal_client_data *clientdata;

  //Get this client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return NULL;
    }

  //Return the array
  return connection_client_intarray_get(clientdata);
}

//Set a callback. Callbacks are so that instead of needing to poll for
//messages, a callback can be set so that the messages are handled immediately
int grapple_client_callback_set(grapple_client client,
				grapple_messagetype message,
				grapple_callback callback,
				void *context)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&clientdata->callback_mutex);

  //Add the callback to the list of callbacks
  clientdata->callbackanchor=grapple_callback_add(clientdata->callbackanchor,
						  message,
						  callback,context);

  pthread_mutex_unlock(&clientdata->callback_mutex);

  return GRAPPLE_OK;
}

//Set ALL callbacks to the function requested
int grapple_client_callback_setall(grapple_client client,
				   grapple_callback callback,
				   void *context)
{
  //Set one using the function above
  if (grapple_client_callback_set(client,GRAPPLE_MSG_NEW_USER,callback,
				  context)==GRAPPLE_FAILED)
    return GRAPPLE_FAILED;

  //if one is ok, they all should be
  grapple_client_callback_set(client,GRAPPLE_MSG_NEW_USER_ME,callback,context);
  grapple_client_callback_set(client,GRAPPLE_MSG_USER_MSG,callback,context);
  grapple_client_callback_set(client,GRAPPLE_MSG_USER_NAME,callback,context);
  grapple_client_callback_set(client,GRAPPLE_MSG_USER_MSG,callback,context);
  grapple_client_callback_set(client,GRAPPLE_MSG_SESSION_NAME,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_USER_DISCONNECTED,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_SERVER_DISCONNECTED,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_CONNECTION_REFUSED,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_PING,callback,context);
  grapple_client_callback_set(client,GRAPPLE_MSG_GROUP_CREATE,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_GROUP_ADD,callback,context);
  grapple_client_callback_set(client,GRAPPLE_MSG_GROUP_REMOVE,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_GROUP_DELETE,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_YOU_ARE_HOST,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_CONFIRM_RECEIVED,callback,
			      context);
  grapple_client_callback_set(client,GRAPPLE_MSG_CONFIRM_TIMEOUT,callback,
			      context);

  return GRAPPLE_OK;
}

//Remove a callback
int grapple_client_callback_unset(grapple_client client,
				   grapple_messagetype message)
{
  internal_client_data *clientdata;

  //Get the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&clientdata->callback_mutex);

  //Remove the callback
  clientdata->callbackanchor=grapple_callback_remove(clientdata->callbackanchor,
						     message);

  pthread_mutex_unlock(&clientdata->callback_mutex);

  return GRAPPLE_OK;
}

//Get the ID of the default client
grapple_client grapple_client_default_get()
{
  internal_client_data *clientdata;

  //Get the default client
  clientdata=internal_client_get(0);

  if (clientdata)
    //Return its ID if we have it
    return clientdata->clientnum;
  else
    //return 0 (the default anyway) if we dont
    return 0;
}


//Enumerate the users. Effectively this means run the passed callback
//function for each user
int grapple_client_enumusers(grapple_client client,
			     grapple_user_enum_callback callback,
			     void *context)
{
  internal_client_data *clientdata;
  int *userarray;
  grapple_user serverid;
  int loopa;
  grapple_connection *user;
  char *tmpname;

  //Find the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  //Get the user array
  userarray=grapple_client_userlist_get(client);

  loopa=0;

  //Loop for each user
  while (userarray[loopa])
    {
      pthread_mutex_lock(&clientdata->connection_mutex);

      //Find the user
      user=connection_from_serverid(clientdata->userlist,userarray[loopa]);
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
	  pthread_mutex_unlock(&clientdata->connection_mutex);

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
	  pthread_mutex_unlock(&clientdata->connection_mutex);
	}

      loopa++;
    }

  return GRAPPLE_OK;
}

//Get the name of the current session
char *grapple_client_session_get(grapple_client client)
{
  internal_client_data *clientdata;
  char *returnval;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return NULL;
    }

  //If no session name has been set, return null
  if (!clientdata->session)
    return NULL;

  //Allocate memory for the session name, and return it
  returnval=(char *)malloc(strlen(clientdata->session)+1);
  strcpy(returnval,clientdata->session);

  return returnval;
}


//Stop (but dont destroy) the client
int grapple_client_stop(grapple_client client)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  //Disconnect from the server
  if (clientdata->thread)
    {
      c2s_disconnect(clientdata);
      clientdata->threaddestroy=1;

      pthread_mutex_lock(&clientdata->internal_mutex);
      if (clientdata->wakesock)
	socket_interrupt(clientdata->wakesock);
      pthread_mutex_unlock(&clientdata->internal_mutex);

      //Wait for the thread to be destroyed
      while (clientdata->threaddestroy==1 && clientdata->thread)
	microsleep(1000);

      clientdata->threaddestroy=0;
    }
  //Leave the rest of the data intact
  return GRAPPLE_OK;
}

//Ping the server, find the round trip time
int grapple_client_ping(grapple_client client)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  //Send the ping to the server
  c2s_ping(clientdata,++clientdata->pingnumber);

  gettimeofday(&clientdata->pingstart,NULL);

  //In the end a ping reply will come back, this will be passed to the user

  return GRAPPLE_OK;
}

//Get the last recorded ping time for a specific user
double grapple_client_ping_get(grapple_client client,grapple_user serverid)
{
  internal_client_data *clientdata;
  double returnval=0;
  grapple_connection *user;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return 0;
    }

  //If we dont know the user, find ourown ping time
  if (serverid==GRAPPLE_USER_UNKNOWN)
    serverid=clientdata->serverid;

  pthread_mutex_lock(&clientdata->connection_mutex);

  //Get the user
  user=connection_from_serverid(clientdata->userlist,serverid);
  if (user)
    {
      //Find that users pingtime
      returnval=user->pingtime;
    }

  pthread_mutex_unlock(&clientdata->connection_mutex);

  return returnval;
}

//Get the server ID of the client
grapple_user grapple_client_serverid_get(grapple_client client)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return 0;
    }

  if (!clientdata->sock)
    {
      grapple_client_error_set(clientdata,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return 0;
    }

  //This can only remain USER_UNKNOWN for so long, in the end, it has to change
  while (clientdata->sock && !socket_dead(clientdata->sock) &&
	 clientdata->serverid==GRAPPLE_USER_UNKNOWN)
    {
      microsleep(1000);
    }

  return clientdata->serverid;
}

//Set that the client is requiring all data to be received sequentially. For
//TCP this doesnt matter. For UDP it forces the client to hold out-of-order
//network packets until earlier ones come in.
int grapple_client_sequential_set(grapple_client client,int value)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  if (value)
    {
      //Set sequential on
      clientdata->sequential=1;

      //Set it low level to the socket
      if (clientdata->sock)
	socket_mode_set(clientdata->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);
    }
  else
    {
      //Set sequential off
      clientdata->sequential=0;

      //And low level on the socket
      if (clientdata->sock)
	socket_mode_unset(clientdata->sock,SOCKET_MODE_UDP2W_SEQUENTIAL);
    }

  return GRAPPLE_OK;
}

//Get the current state of sequential or non-sequential
int grapple_client_sequential_get(grapple_client client)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  return clientdata->sequential;
}

//Messages can be sent to groups, not just to users. This function
//returns the ID of a group from the name
int grapple_client_group_from_name(grapple_client client,const char *name)
{
  internal_client_data *clientdata;
  int returnval;
  internal_grapple_group *scan;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  pthread_mutex_lock(&clientdata->group_mutex);

  //Loop through all groups
  scan=clientdata->groups;

  while (scan)
    {
      //If the name matches
      if (scan->name && *scan->name && !strcmp(scan->name,name))
	{
	  //return this groups ID
	  returnval=scan->id;
	  pthread_mutex_unlock(&clientdata->group_mutex);
	  return returnval;
	}

      scan=scan->next;
      if (scan==clientdata->groups)
	scan=NULL;
    }

  pthread_mutex_unlock(&clientdata->group_mutex);

  //No ID to find

  return 0;
}

//create a group. The group is always assigned by the server. To speed things
//up the server pre-assigns each user a group
grapple_user grapple_client_group_create(grapple_client client,
					 const char *name)
{

  internal_client_data *clientdata;
  int returnval;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return 0;
    }

  if (!clientdata->sock)
    {
      grapple_client_error_set(clientdata,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return 0;
    }

  //If the server hasnt pre-assigned the group, it will shortly. Wait for
  //it.
  while (clientdata->sock && clientdata->next_group==0)
    microsleep(1000);

  //Note the group ID
  returnval=clientdata->next_group;

  //Remove it from the client
  clientdata->next_group=0;
  //Request a new group ID from the server
  c2s_request_group(clientdata);

  //Tell the server to create a new group based on the ID we have just obtained
  c2s_group_create(clientdata,returnval,name);

  //Now create a group locally
  create_client_group(clientdata,returnval,name);

  //Return the group ID
  return returnval;
}

//Adding a user to a group. This will mean that any messages sent to the
//group will also be sent to that user
int grapple_client_group_add(grapple_client client,grapple_user group,
			     grapple_user add)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  if (!clientdata->sock)
    {
      grapple_client_error_set(clientdata,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Add a user to the group
  if (client_group_add(clientdata,group,add))
    {
      //Tell the server about it, if it was successful
      c2s_group_add(clientdata,group,add);

      return GRAPPLE_OK;
    }

  return GRAPPLE_FAILED;
}

//Remove a user from a group
int grapple_client_group_remove(grapple_client client,grapple_user group,
				grapple_user removeid)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  if (!clientdata->sock)
    {
      grapple_client_error_set(clientdata,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Now remove the user locally
  if (client_group_remove(clientdata,group,removeid))
    {
      //If successful, remove from the server
      c2s_group_remove(clientdata,group,removeid);

      return GRAPPLE_OK;
    }

  return GRAPPLE_FAILED;
}

//Delete a group entirely
int grapple_client_group_delete(grapple_client client,grapple_user group)
{
  internal_client_data *clientdata;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  if (!clientdata->sock)
    {
      grapple_client_error_set(clientdata,GRAPPLE_ERROR_CLIENT_NOT_CONNECTED);
      return GRAPPLE_FAILED;
    }

  //Delete the group locally
  if (delete_client_group(clientdata,group))
    {
      //If successful, tell the server about it
      c2s_group_delete(clientdata,group);

      return GRAPPLE_OK;
    }

  return GRAPPLE_FAILED;
}

//Enumerate the users. Effectively this means run the passed callback
//function for each user in the group
int grapple_client_enumgroup(grapple_client client,
			     grapple_user groupid,
			     grapple_user_enum_callback callback,
			     void *context)
{
  internal_client_data *clientdata;
  int *userarray;
  grapple_user serverid;
  int loopa;
  grapple_connection *user;
  char *tmpname;

  //Find the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }

  //Get the user array
  userarray=client_group_unroll(clientdata,groupid);

  loopa=0;

  //Loop for each user
  while (userarray[loopa])
    {
      pthread_mutex_lock(&clientdata->connection_mutex);

      //Find the user
      user=connection_from_serverid(clientdata->userlist,userarray[loopa]);
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
	  pthread_mutex_unlock(&clientdata->connection_mutex);

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
	  pthread_mutex_unlock(&clientdata->connection_mutex);
	}

      loopa++;
    }

  return GRAPPLE_OK;
}

grapple_user *grapple_client_groupusers_get(grapple_client client,
					    grapple_user groupid)
{
  internal_client_data *clientdata;
  grapple_user *userarray;

  //Find the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return NULL;
    }

  //Get the user array
  userarray=client_group_unroll(clientdata,groupid);

  return userarray;
}

//Enumerate the list of groups, running a user function for each group
int grapple_client_enumgrouplist(grapple_client client,
				 grapple_user_enum_callback callback,
				 void *context)
{
  internal_client_data *clientdata;
  int *grouplist;
  grapple_user groupid;
  int count;
  char *tmpname;
  internal_grapple_group *scan;

  //Find the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_FAILED;
    }


  //The rest of this is pretty inefficient, but it is done this way for a
  //reason. It is done to minimise the lock time on the group mutex,
  //as calling a user function with that mutex locked could be disasterous for
  //performance.

  //Get the group list into an array
  count=0;
  scan=clientdata->groups;

  pthread_mutex_lock(&clientdata->group_mutex);

  //Count them first so we can size the array
  while (scan)
    {
      count++;
      scan=scan->next;
      if (scan==clientdata->groups)
	scan=NULL;
    }

  if (!count)
    {
      pthread_mutex_unlock(&clientdata->group_mutex);
      return GRAPPLE_OK;
    }

  //The array allocation
  grouplist=(int *)malloc(count * (sizeof(int)));

  scan=clientdata->groups;
  count=0;

  //Insert the groups into it
  while (scan)
    {
      grouplist[count++]=scan->id;
      scan=scan->next;
      if (scan==clientdata->groups)
	scan=NULL;
    }

  pthread_mutex_unlock(&clientdata->group_mutex);

  //We now have the list of groups
  while (count>0)
    {
      //Loop backwards through the groups. We make no guarentee of enumeration
      //order
      groupid=grouplist[--count];
      pthread_mutex_lock(&clientdata->group_mutex);
      scan=group_locate(clientdata->groups,groupid);
      tmpname=NULL;
      if (scan)
	{
	  //If the group has a name, note that
	  if (scan->name && *scan->name)
	    {
	      tmpname=(char *)malloc(strlen(scan->name)+1);
	      strcpy(tmpname,scan->name);
	    }
	}
      //We're finished with the mutex, unlock it
      pthread_mutex_unlock(&clientdata->group_mutex);

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
grapple_user *grapple_client_grouplist_get(grapple_client client)
{
  internal_client_data *clientdata;
  int *grouplist;
  int count;
  internal_grapple_group *scan;

  //Find the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return NULL;
    }

  //Get the group list into an array
  count=0;
  scan=clientdata->groups;

  pthread_mutex_lock(&clientdata->group_mutex);

  //Count them first so we can size the array
  while (scan)
    {
      count++;
      scan=scan->next;
      if (scan==clientdata->groups)
	scan=NULL;
    }

  if (!count)
    {
      pthread_mutex_unlock(&clientdata->group_mutex);
      return NULL;
    }

  //The array allocation
  grouplist=(int *)malloc((count+1) * (sizeof(int)));

  scan=clientdata->groups;
  count=0;

  //Insert the groups into it
  while (scan)
    {
      grouplist[count++]=scan->id;
      scan=scan->next;
      if (scan==clientdata->groups)
	scan=NULL;
    }

  pthread_mutex_unlock(&clientdata->group_mutex);

  grouplist[count]=0;

  //We now have the list of groups

  return grouplist;
}

char *grapple_client_groupname_get(grapple_client client,grapple_user groupid)
{
  internal_client_data *clientdata;
  internal_grapple_group *group;
  char *groupname;

  //Find the client
  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return NULL;
    }

  pthread_mutex_lock(&clientdata->group_mutex);
  group=group_locate(clientdata->groups,groupid);

  if (!group)
    return NULL;

  groupname=(char *)malloc(strlen(group->name)+1);
  strcpy(groupname,group->name);

  return groupname;
}

//Get the last error
grapple_error grapple_client_error_get(grapple_client client)
{
  internal_client_data *clientdata;
  grapple_error returnval;

  clientdata=internal_client_get(client);

  if (!clientdata)
    {
      return GRAPPLE_ERROR_NOT_INITIALISED;
    }

  returnval=clientdata->last_error;

  //Now wipe the last error
  clientdata->last_error=GRAPPLE_NO_ERROR;

  return returnval;
}

