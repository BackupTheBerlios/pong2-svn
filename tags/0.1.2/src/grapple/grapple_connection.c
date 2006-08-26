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
#include <stdlib.h>
#include <string.h>

#include "grapple_defines.h"
#include "grapple_queue.h"
#include "grapple_confirm.h"
#include "grapple_connection.h"

//Wrapper function for creating a new connection. Coded this way to allow
//the later implimentation of queues or stacks or something, if that turns
//out to be a timesaver
static grapple_connection *connection_struct_aquire(void)
{
  return (grapple_connection *)calloc(1,sizeof(grapple_connection));
}

//Dispose of a connection. This wrapper frees all memory associated with a
//connection and closes any open sockets
void connection_struct_dispose(grapple_connection *connection)
{
  grapple_queue *target;
  grapple_confirm *conf;

  if (connection->sock)
    {
      //destroy the socket if it is present
      socket_destroy(connection->sock);
    }
  
  if (connection->failoversock)
    {
      //Also the failover socket, if we happened to disconnect in the
      //middle of a failover test
      socket_destroy(connection->failoversock);
    }
  
  if (connection->name)
    {
      //Free the name
      free(connection->name);
      connection->name=NULL;
    }
  
  //Wipe the message queues
  while (connection->message_in_queue)
    {
      target=connection->message_in_queue;
      connection->message_in_queue=queue_unlink(connection->message_in_queue,
					      connection->message_in_queue);
      queue_struct_dispose(target);
    }

  while (connection->message_out_queue)
    {
      target=connection->message_out_queue;
      connection->message_out_queue=queue_unlink(connection->message_out_queue,
					       connection->message_out_queue);
      queue_struct_dispose(target);
    }

  //Wipe the confirm queue
  while (connection->confirm)
    {
      conf=connection->confirm;
      connection->confirm=grapple_confirm_unlink(connection->confirm,
					       connection->confirm);
      grapple_confirm_dispose(conf);
    }

  //Destroy the mutexes
  pthread_mutex_destroy(&connection->confirm_mutex);
  pthread_mutex_destroy(&connection->message_out_mutex);
  pthread_mutex_destroy(&connection->message_in_mutex);

  //Free the data
  free(connection);

  return;
}

//Find a connection somewhere in a list, by its ID number
grapple_connection *connection_from_serverid(grapple_connection *list,
					   int serverid)
{
  grapple_connection *scan;

  scan=list;

  //Loop through the list
  while (scan)
    {
      if (scan->serverid==serverid)
	//Found a match
	return scan;

      scan=scan->next;
      if (scan==list)
	scan=NULL;
    }
  
  //No match
  return NULL;
}

//Link a connection into a linked list
static grapple_connection *connection_link(grapple_connection *connection,
					   grapple_connection *item)
{
  if (!connection)
    {
      item->next=item;
      item->prev=item;
      return item;
    }

  item->next=connection;
  item->prev=connection->prev;

  item->next->prev=item;
  item->prev->next=item;

  return connection;
}

//Remove a connection from a linked list
grapple_connection *connection_unlink(grapple_connection *connection,
				    grapple_connection *item)
{
  if (connection->next==connection)
    {
      return NULL;
    }

  item->next->prev=item->prev;
  item->prev->next=item->next;

  if (item==connection)
    connection=item->next;

  return connection;
}

//Add a new connection to the client
int connection_client_add(internal_client_data *client,int serverid,int me)
{
  grapple_connection *newitem,*scan;

  //Check it doesnt already exist
  scan=client->userlist;

  while (scan)
    {
      if (scan->serverid==serverid)
	{
	  scan->me=me;
	  return 1;
	}

      scan=scan->next;
      if (scan==client->userlist)
	scan=NULL;
    }

  //Create a new connection struct
  newitem=connection_struct_aquire();

  //Set its values
  newitem->serverid=serverid;
  newitem->me=me;

  //link it into the clients list of users
  pthread_mutex_lock(&client->connection_mutex);
  client->userlist=connection_link(client->userlist,newitem);
  pthread_mutex_unlock(&client->connection_mutex);

  return 1;
}

//Remove a connection from the cient by its ID number
int connection_client_remove_by_id(internal_client_data *client,int serverid)
{
  grapple_connection *target;

  //Find the connection
  target=connection_from_serverid(client->userlist,serverid);

  if (target)
    {
      //Unlink it
      pthread_mutex_lock(&client->connection_mutex);
      client->userlist=connection_unlink(client->userlist,target);
      pthread_mutex_unlock(&client->connection_mutex);

      //Destroy it
      connection_struct_dispose(target);
    }

  return 1;
}

//Add a new connection to the server
int connection_server_add(internal_server_data *server,socketbuf *sock)
{
  grapple_connection *newitem;
  pthread_mutexattr_t attr;

  //Create the struct to hold the data
  newitem=connection_struct_aquire();

  //Link this socket in
  newitem->sock=sock;

  //Asign a new server ID
  newitem->serverid=server->user_serverid++;

  //Create the required thread mutexes
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  pthread_mutex_init(&newitem->message_in_mutex,&attr);
  pthread_mutex_init(&newitem->message_out_mutex,&attr);
  pthread_mutex_init(&newitem->confirm_mutex,&attr);

  //Set the protocol
  newitem->protocol=server->protocol;

  //Link this into the server
  pthread_mutex_lock(&server->connection_mutex);
  server->userlist=connection_link(server->userlist,newitem);
  pthread_mutex_unlock(&server->connection_mutex);

  return newitem->serverid;
}

//Count the number of users connected
int connection_server_count(internal_server_data *server)
{
  int count=0;
  grapple_connection *scan;

  //Lock the mutex
  pthread_mutex_lock(&server->connection_mutex);

  scan=server->userlist;

  while (scan)
    {
      //Count one user
      count++;

      scan=scan->next;
      if (scan==server->userlist)
	scan=NULL;
    }
  
  //Unlock
  pthread_mutex_unlock(&server->connection_mutex);

  //Return the count
  return count;
}

//get an array of the users connected, as a list of integers
int *connection_server_intarray_get(internal_server_data *server)
{
  int count;
  int *returnval;
  grapple_connection *scan;

  //the list returned will be 0 terminated

  pthread_mutex_lock(&server->connection_mutex);

  //First count the number of users
  count=connection_server_count(server);

  //Allocate memory for the array
  returnval=(int *)calloc(1,sizeof(int)*(count+1));


  scan=server->userlist;

  count=0;

  while (scan)
    {
      //Add all known users to the list
      if (scan->serverid!=GRAPPLE_USER_UNKNOWN)
	returnval[count++]=scan->serverid;

      scan=scan->next;
      if (scan==server->userlist)
	scan=NULL;
    }
  
  pthread_mutex_unlock(&server->connection_mutex);

  //Return the list
  return returnval;
}


//Rename a users connection
int connection_client_rename(internal_client_data *client,
			   int serverid,
			   char *name)
{
  grapple_connection *user;

  pthread_mutex_lock(&client->connection_mutex);

  //Find the user
  user=connection_from_serverid(client->userlist,serverid);

  if (user)
    {
      //Delete the old name if they have one
      if (user->name)
	free(user->name);
      
      //New name
      user->name=(char *)malloc(strlen(name)+1);
      strcpy(user->name,name);

      //If it is 'me' change this name on the client also
      if (user->me)
	{
	  if (client->name)
	    free(client->name);
	  client->name=(char *)malloc(strlen(name)+1);
	  strcpy(client->name,name);
	}
    }

  pthread_mutex_unlock(&client->connection_mutex);

  return 1;
}

//Count the users connected to the client
int connection_client_count(internal_client_data *client)
{
  int count=0;
  grapple_connection *scan;

  pthread_mutex_lock(&client->connection_mutex);

  scan=client->userlist;

  while (scan)
    {
      //Count this user
      count++;

      scan=scan->next;
      if (scan==client->userlist)
	scan=NULL;
    }
  
  pthread_mutex_unlock(&client->connection_mutex);

  return count;
}

//Return the array of users connected to the server
int *connection_client_intarray_get(internal_client_data *client)
{
  int count;
  int *returnval;
  grapple_connection *scan;

  pthread_mutex_lock(&client->connection_mutex);

  //First count the number of users
  count=connection_client_count(client);

  //Allocate thememory for the array
  returnval=(int *)calloc(1,sizeof(int)*(count+1));

  scan=client->userlist;

  count=0;

  while (scan)
    {
      if (scan->serverid!=GRAPPLE_USER_UNKNOWN)
	//Add this valid user to the array
	returnval[count++]=scan->serverid;

      scan=scan->next;
      if (scan==client->userlist)
	scan=NULL;
    }
  
  pthread_mutex_unlock(&client->connection_mutex);

  //return the array
  return returnval;
}
