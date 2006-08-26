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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include <linux/limits.h>
#ifdef SOCK_SSL
#include <openssl/ssl.h>
#endif

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif

#include "dynstring.h"
#include "socket.h"

extern int gethostname (char *, size_t);

static int socket_udp2way_connectmessage(socketbuf *);
static int socket_udp2way_listener_data_process(socketbuf *,
						struct sockaddr_in *,
						size_t,signed char *,int);
static int socket_udp2way_reader_data_process(socketbuf *sock,
					      signed char *buf,int datalen);

#ifdef SOCK_SSL

//Handle the SSL errors

static int ssl_process_error(SSL *ssl,int rv)
{
  switch (SSL_get_error(ssl,rv))
    {
    case SSL_ERROR_NONE:
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_CONNECT:
    case SSL_ERROR_WANT_ACCEPT:
      rv=0;      //Set rv (the retutn value) to 0 for each of these errors
      break;
    case SSL_ERROR_ZERO_RETURN:
    case SSL_ERROR_WANT_X509_LOOKUP:
    case SSL_ERROR_SYSCALL:
    case SSL_ERROR_SSL:
      break;
    }
  return rv;
}

//Function to initialise the socket to be encrypted
static void socket_set_listener_encrypted(socketbuf *sock)
{
  int rv;

  SSL_METHOD *ssl_meth=0;

  ssl_meth=SSLv23_server_method();

  sock->ctx = SSL_CTX_new(ssl_meth);
  if (!sock->ctx)
    {
      sock->encrypted=0;
      sock->flags |= SOCKET_DEAD;
      return;
    }

  if (!sock->server_cert_file || !*sock->server_cert_file ||
      !sock->server_key_file || !*sock->server_key_file)
    {
      sock->encrypted=0;
      sock->flags |= SOCKET_DEAD;
      return;
    }

  rv=SSL_CTX_use_certificate_file(sock->ctx,sock->server_cert_file,
				  SSL_FILETYPE_PEM);

  rv=SSL_CTX_use_RSAPrivateKey_file(sock->ctx,sock->server_key_file,
				    SSL_FILETYPE_PEM);

  if (!SSL_CTX_check_private_key(sock->ctx)) 
    {
      printf("Private key does not match the certificate public key\n");
      sock->encrypted=0;
      sock->flags |= SOCKET_DEAD;
      return;
    }

  //It has all initialised correctly, set it as encrypted
  sock->encrypted=1;
}

//Set the socket to be a host type encrypted socket - so other sockets will
//verify against it.
static void socket_set_server_encrypted(socketbuf *sock)
{
  int rv;

  if (sock->encrypted==2)
    {
      sock->ctx = sock->parent->ctx;
      if (!sock->ctx)
	{
	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}

      sock->ssl = SSL_new(sock->ctx);
      if (!sock->ssl)
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}

      SSL_set_fd(sock->ssl,sock->fd);

      sock->encrypted=3;
    }
  
  if (sock->encrypted==3)
    {
      rv=SSL_accept(sock->ssl);

      if (rv<0)
	{
	  rv=ssl_process_error(sock->ssl,rv);
	}

      if (rv<0)
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  
	  SSL_free(sock->ssl);
	  sock->ssl=0;

	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}

      if (rv==0)
	return;

      sock->encrypted=4;
    }

  if (sock->encrypted==4)
    {
      if (!strcmp(SSL_get_cipher(sock->ssl),"(NONE)"))
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  
	  SSL_free(sock->ssl);
	  sock->ssl=0;

	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}
    }

  SSL_set_mode(sock->ssl,
	       SSL_get_mode(sock->ssl)|SSL_MODE_ENABLE_PARTIAL_WRITE);

  //It worked, note it as an encrypted socket
  sock->encrypted=1;

  return;
}

//Set this socket up to be an encryption client that will verift against the
//host
static void socket_set_client_encrypted(socketbuf *sock)
{
  SSL_METHOD *ssl_meth=0;
  int rv;

  if (sock->encrypted==2)
    {
      ssl_meth=SSLv23_client_method();

      sock->ctx = SSL_CTX_new(ssl_meth);
      if (!sock->ctx)
	{
	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}

      sock->ssl=SSL_new(sock->ctx);
      if (!sock->ssl)
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}

      if (sock->client_ca_file)
	{
	  rv=SSL_CTX_load_verify_locations(sock->ctx,sock->client_ca_file,NULL);
	  
	  if (!rv)
	    {
	      SSL_CTX_free(sock->ctx);
	      sock->ctx=0;
	      sock->encrypted=0;
	      sock->flags |= SOCKET_DEAD;
	      return;
	    }
	}
      
      SSL_set_fd(sock->ssl,sock->fd);

      sock->encrypted=3;
    }
  
  if (sock->encrypted==3)
    {
      rv=SSL_connect(sock->ssl);

      if (rv<0)
	{
	  rv=ssl_process_error(sock->ssl,rv);
	}

      if (rv<0)
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  
	  SSL_free(sock->ssl);
	  sock->ssl=0;

	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}

      if (rv==0)
	return;

      sock->encrypted=4;
    }

  if (sock->encrypted==4)
    {
      if (!strcmp(SSL_get_cipher(sock->ssl),"(NONE)"))
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  
	  SSL_free(sock->ssl);
	  sock->ssl=0;

	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;
	  return;
	}
    }

  if (sock->client_ca_file)
    {
      //Now we verify that the cert we have is good
      rv=SSL_get_verify_result(sock->ssl);

      if (rv!=X509_V_OK)
	{
	  SSL_CTX_free(sock->ctx);
	  sock->ctx=0;
	  
	  SSL_free(sock->ssl);
	  sock->ssl=0;

	  sock->encrypted=0;
	  sock->flags |= SOCKET_DEAD;

	  return;
	}
    }

  SSL_set_mode(sock->ssl,
	       SSL_get_mode(sock->ssl)|SSL_MODE_ENABLE_PARTIAL_WRITE);

  //Successful
  sock->encrypted=1;

  return;
}

static void socket_process_ssl(socketbuf *sock)
{
  if (sock->flags & SOCKET_INCOMING)
    socket_set_server_encrypted(sock);
  else
    socket_set_client_encrypted(sock);
}  
#endif //SOCK_SSL

#ifdef DEBUG
//Simple debug function that reports all socket data to a file, the filename
//based on the fd number
static void socket_data_debug(socketbuf *sock,char *buf,int len,int writer)
{
  FILE *fp;
  int loopa;
  char filename[PATH_MAX];

  //Set the filename  
  if (writer)
    sprintf(filename,"/tmp/socket_%d.write",sock->fd);
  else
    sprintf(filename,"/tmp/socket_%d.read",sock->fd);

  //Open the file for appending
  fp=fopen(filename,"a");
  if (fp)
    {
      //Write the bytes into the file, on oneline. If the value is printable
      //also write the character, as this can help debugging some streams
      for (loopa=0;loopa<len;loopa++)
	{
	  if (isprint(buf[loopa]))
	    fprintf(fp,"%d(%c) ",buf[loopa],buf[loopa]);
	  else
	    fprintf(fp,"%d ",buf[loopa]);
	}

      //Finish off with a newline
      fprintf(fp,"\n");

      //Close the file, we're done
      fclose(fp);
    }
  return;
}
#endif

//Generic function to write data to the socket. This is called from
//outside. We do NOT actually write the data to the socket at this stage, we
//just add it to a buffer
void socket_write(socketbuf *sock,
		  const char *data,size_t len)
{
  socket_intchar udplen,udpdata;
  int newlen;

  //If we are using UDP we need to do it differently, as UDP sends discrete 
  //packets not a stream
  if (sock->protocol==SOCKET_UDP)
    {
      //Calculate how long the length will be of the UDP packet
      newlen=len;
      if (sock->udp2w)
	{
	  //It will be 4 extra bytes if it is a 2 way UDP

	  if (sock->udp2w_infd)
	    //And an extra 4 if we have the incoming socket ready
	    newlen+=8;
	  else
	    //Just that 4
	    newlen+=4;
	}

      //So, the first data goes in, this is the length of the following data
      //This happens for all UDP packets, so the buffer knows how long to send
      //as the data packet
      udplen.i=newlen;
      dynstringRawappend(sock->outdata,udplen.c,4);

      if (sock->udp2w)
	{
	  //Then for 2 way UDP, we send the protocol - we are sending user
	  //data not a low level protocol packet
	  udpdata.i=htonl(SOCKET_UDP2W_PROTOCOL_DATA);
	  dynstringRawappend(sock->outdata,udpdata.c,4);
	  if (sock->udp2w_infd)
	    {
	      //We then send the port number of the return port, if we have one
	      udpdata.i=htonl(sock->udp2w_port);
	      dynstringRawappend(sock->outdata,udpdata.c,4);
	    }
	}
    }


  //Now we simply append the data itself. If this is TCP thats all we need
  //to do, as TCP sends a whole stream, its up to the client to rebuild
  //it, with UDP we have made and sent a header
  dynstringRawappend(sock->outdata,data,len);
  sock->bytes_out+=len;

  return;
}

//rdata is the resend data, used on reliable UDP packets to resend
//packets that may have gone missing. Here we delete one from a
//linked list. Any linked list, we dont care
static socket_udp_rdata *socket_rdata_delete(socket_udp_rdata *list,
					     socket_udp_rdata *target)
{
  if (target->next==target)
    {
      list=NULL;
    }
  else
    {
      target->next->prev=target->prev;
      target->prev->next=target->next;
      if (target==list)
	list=target->next;
    }

  if (target->data)
    free(target->data);
  free(target);
  
  return list;
}

//This function locates a rdata packet by its ID from a list
static socket_udp_rdata *socket_rdata_locate_packetnum(socket_udp_rdata *list,
						       int packetnum)
{
  socket_udp_rdata *scan;

  scan=list;

  //Scan through the list
  while (scan)
    {
      if (scan->packetnum==packetnum)
	//We have a match, return it
	return scan;

      scan=scan->next;
      if (scan==list)
	//Come to the end of the list (it is circular)
	scan=NULL;
    }
  
  //No match, return NULL
  return NULL;
}

//Allocate an rdata packet and put it into a list
static socket_udp_rdata *rdata_allocate(socket_udp_rdata *list,
					int packetnum,
					const char *data,int len,int sent)
{
  socket_udp_rdata *newpacket;

  //Allocate the memory
  newpacket=(socket_udp_rdata *)calloc(1,sizeof(socket_udp_rdata));

  //Allocate the data segment memory
  newpacket->data=(char *)malloc(len);
  memcpy(newpacket->data,data,len);
  
  newpacket->length=len;
  newpacket->sent=sent;

  //Set the send time
  gettimeofday(&newpacket->sendtime,NULL);
  
  newpacket->packetnum=packetnum;

  //Link this into the list we have supplied
  if (list)
    {
      newpacket->next=list;
      newpacket->prev=list->prev;
      newpacket->prev->next=newpacket;
      newpacket->next->prev=newpacket;
      
      return list;
    }

  newpacket->next=newpacket;
  newpacket->prev=newpacket;

  return newpacket;
}


//Write a data packet in reliable mode
void socket_write_reliable(socketbuf *sock,
			   const char *data,size_t len)
{
  socket_intchar udplen,udpdata;
  int newlen,packetnum;

  //If we arent using 2 way UDP, we just send, as we cant have reliable one way
  //UDP and UDP is the only protocol we support that is unreliable
  if (sock->protocol!=SOCKET_UDP || !sock->udp2w)
    {
      socket_write(sock,
		   data,len);
      return;
    }

  //Incriment the outbound packet number
  packetnum=sock->udp2w_routpacket++;

  //Calculate the length of the data
  newlen=len;
  if (sock->udp2w_infd)
    newlen+=12;
  else
    newlen+=8;

  //Send the length first //This does NOT get htonl'd as it gets stripped
  //before actually sending it
  udplen.i=newlen;
  dynstringRawappend(sock->outdata,udplen.c,4);

  //Then the protocol
  udpdata.i=htonl(SOCKET_UDP2W_PROTOCOL_RDATA);

  dynstringRawappend(sock->outdata,udpdata.c,4);

  if (sock->udp2w_infd)
    {
      //Then the port number
      udpdata.i=htonl(sock->udp2w_port);
      dynstringRawappend(sock->outdata,udpdata.c,4);
    }

  //Then the packet number, so the other end keeps in sync
  udpdata.i=htonl(packetnum);
  dynstringRawappend(sock->outdata,udpdata.c,4);

  //Then the data itself
  dynstringRawappend(sock->outdata,data,len);
  sock->bytes_out+=len;

  //Add this packet to the RDATA out list, so we know to resend it if we
  //dont get a confirmation of the receipt
  sock->udp2w_rdata_out=rdata_allocate(sock->udp2w_rdata_out,
				       packetnum,
				       data,len,0);

  return;
}

//Just a user accessible function to return the number of bytes received
size_t socket_bytes_in(socketbuf *sock)
{
  return sock->bytes_in;
}

//Just a user accessible function to return the number of bytes sent
size_t socket_bytes_out(socketbuf *sock)
{
  return sock->bytes_out;
}

//Sockets are processed out of a 'processlist' - which is a linked list
//of socketbuf's. This function adds a socketbuf to a processlist. It creates
//a processlist object to hold the socketbuf
socket_processlist *socket_link(socket_processlist *list,socketbuf *sock)
{
  socket_processlist *newitem;
  newitem=(socket_processlist *)malloc(sizeof(socket_processlist));
  newitem->sock=sock;

  if (!list)
    {
      newitem->next=newitem;
      newitem->prev=newitem;

      return newitem;
    }

  newitem->next=list;
  newitem->prev=list->prev;

  newitem->next->prev=newitem;
  newitem->prev->next=newitem;
  
  return list;
}

//And this function unlinks a socketbuf from a processlist. It also frees the
//processlist container that held the socketbuf
socket_processlist *socket_unlink(socket_processlist *list,socketbuf *sock)
{
  socket_processlist *scan;

  if (list->next==list)
    {
      if (list->sock!=sock)
	return list;

      free(list);
      return NULL;
    }
  
  scan=list;

  while (scan)
    {
      if (scan->sock==sock)
	{
	  scan->prev->next=scan->next;
	  scan->next->prev=scan->prev;
	  if (scan==list)
	    list=scan->next;
	  free(scan);
	  return list;
	}
      scan=scan->next;
      if (scan==list)
	return list;
    }

  return list;
}

//This is the basic function for creating a socketbuf object around a
//file descriptor
static socketbuf *socket_create(int fd)
{
  socketbuf *returnval;

  //Allocate the memory for the socket
  returnval=(socketbuf *)calloc(1,sizeof(socketbuf));

  //Give it a small in and out buffer - these resize dynamically so it doesnt
  //really matter what size we give it. 128 is a fairly small number in case
  //the socket only sends small bits of data, this saves over-allocating.
  returnval->indata=dynstringInit(128);
  returnval->outdata=dynstringInit(128);

  //Set the file descriptor into the structure
  returnval->fd=fd;

  //Thats it, we have our socketbuf. Much more wil happen to this depending
  //on what the type of socket being made is, this data will be filled in
  //by the function that calls this one.
  return returnval;
}

//We are destroying a socket. We are also however needing to be careful that
//we destroy any connecting sockets if this is a listener.
void socket_destroy(socketbuf *sock)
{
  socketbuf *scan;

  while (sock->new_children)
    {
      //Now we MUST destroy this, they are connecting sockets who have
      //no parent, if we dont destroy now we will leak memory
      //This will, incidentally, cascade down, destroying always the last
      //one in the tree, and then roll back up to here
      socket_destroy(sock->new_children);
    }

  //Here we must now also disconnect all connected children. These have been
  //accessed by the parent program and so it is not our responsibility to
  //keep track of them. Why did we keep them in a list in the first place?
  //Well, things like UDP, all data comes in on the listener, not on a
  //socket dedicated to the other end, so we need to have a list of all
  //who have connected, so we know who to send the data to/
  scan=sock->connected_children;
  while (scan)
    {
      if (scan->parent==sock)
	scan->parent=NULL;
      
      scan=scan->connected_child_next;
      if (scan==sock->connected_children)
	scan=NULL;
    }

  //If we have a parent, then unlink ourselves from the parent, so that
  //this socket is no longer in contention for any data received by the
  //parent socket, if it is UDP
  if (sock->parent)
    {
      if (sock->new_child_next)
	{
	  if (sock->parent->new_children==sock)
	    sock->parent->new_children=sock->new_child_next;
	  if (sock->parent->new_children==sock)
	    sock->parent->new_children=NULL;
	}
      
      if (sock->connected_child_next)
	{
	  if (sock->parent->connected_children==sock)
	    sock->parent->connected_children=sock->connected_child_next;
	  if (sock->parent->connected_children==sock)
	    sock->parent->connected_children=NULL;
	}
    }

  //Unlink ourselves from the list of sockets connected to the same
  //parent, which can be intact even if the parent is gone.
  if (sock->new_child_next)
    {
      sock->new_child_next->new_child_prev=sock->new_child_prev;
      sock->new_child_prev->new_child_next=sock->new_child_next;
    }
  
  if (sock->connected_child_next)
    {
      sock->connected_child_next->connected_child_prev=sock->connected_child_prev;
      sock->connected_child_prev->connected_child_next=sock->connected_child_next;
    }

  //Finally we have done the internal management, we need to actually
  //destroy the socket!
  if (sock->fd)
    {
      //If we have the socket, kill it

      if (sock->flags & SOCKET_CONNECTED)
	//shutdown, if its connected
	shutdown(sock->fd,0);

      //Then close the socket
      close(sock->fd);
    }
  //The data socket itself is now disconnected.

  //On a 2 way UDP, we need to also close the other socket
  if (sock->udp2w_infd)
    {
      //If we have the socket, kill it

      //shutdown, if its connected
      shutdown(sock->udp2w_infd,0);

      //Then close the socket
      close(sock->udp2w_infd);
    }
  //The udp2w_infd socket itself is now disconnected.

  //On an interrupt docket we have a different one
  if (sock->interrupt_fd)
    {
      //If we have the socket, kill it

      //shutdown, if its connected
      shutdown(sock->interrupt_fd,0);

      //Then close the socket
      close(sock->interrupt_fd);
    }
  
  //Free the memory used in the transmit and receive queues
  if (sock->indata)
    dynstringUninit(sock->indata);

  if (sock->outdata)
    dynstringUninit(sock->outdata);

  //Free the resend data queues, we dont need them any more, any data that
  //still hasnt made it isnt going to now.
  while (sock->udp2w_rdata_out)
    sock->udp2w_rdata_out=socket_rdata_delete(sock->udp2w_rdata_out,
					      sock->udp2w_rdata_out);
  while (sock->udp2w_rdata_in)
    sock->udp2w_rdata_in=socket_rdata_delete(sock->udp2w_rdata_in,
					     sock->udp2w_rdata_in);

  //Free the hostname
  if (sock->host)
    free(sock->host);

  //Free the pathname (applies to unix sockets only)
  if (sock->path)
    {
      if (sock->flags & SOCKET_LISTENER)
	unlink(sock->path);
      free(sock->path);
    }

  //Free the socket data, we are done
  free(sock);

  return;
}

//Create the listener socket for a unix connection
socketbuf *socket_create_unix_wait(const char *path,int wait)
{
  int fd;
  socketbuf *returnval;
  struct sockaddr_un sa;
  int dummy,selectnum;
  fd_set writer;
#ifndef FIONBIO
# ifdef O_NONBLOCK
  int flags;
# endif
#endif

  //We must specify a path in the filesystem, that is where the socket lives.
  //Without it, no way to create it.
  if (!path || !*path)
    return 0;

  //create the sockets file descriptor
  fd=socket(PF_UNIX,SOCK_STREAM,0);

  if (fd<1)
    {
      //Socket creation failed.
      return 0;
    }

  memset(&sa,0,sizeof(struct sockaddr_in));

  //Set the fd as a UNIX socket
  sa.sun_family = AF_UNIX;
  strcpy(sa.sun_path,path);

  //Set non-blocking, so we can check for a data without freezing. If we
  //fail to set non-blocking we must abort, we require it.
#ifdef FIONBIO
  dummy=1;

  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      close(fd);
      return 0;
    }
#else
# ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);

  if (flags<0)
    {
      close(fd);
      return 0;
    }

  if (fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)
    {
      close(fd);
      return 0;
    }

# else
#  error No valid non-blocking method - cannot build;
# endif // O_NONBLOCK
#endif //FIONBIO


  //We have the good file descriptor, set it into a socketbuf structure
  returnval=socket_create(fd);

  //Note the protocol
  returnval->protocol=SOCKET_UNIX;

  //And store the path
  returnval->path=(char *)malloc(strlen(path)+1);
  strcpy(returnval->path,path);

  //Up to now this has all been preparation, now we actually connect to the
  //socket
  if (connect(fd,(struct sockaddr *)&sa,sizeof(sa))==0)
    {
      //Connect was successful, we can finish this here
      returnval->flags |= SOCKET_CONNECTED;
      returnval->connect_time=time(NULL);

      return returnval;
    }
  
  //The connection is 'in progress'
  if (errno==EINPROGRESS)
    {
      //Connect was possibly OK, but we havent finished, come back
      //and check later with select
      returnval->flags|=SOCKET_CONNECTING;

      if (!wait)
	{
	  //We were called with the option NOT to wait for it to connect, so
	  //we return here. It is now the responsibility of the caller to
	  //process this socket occasionally and see if it has now connected
	  //or if the connection failed.

	  return returnval;
	}
      else
	{
	  //We were asked to keep waiting for the socket to connect
	  while (returnval->flags & SOCKET_CONNECTING)
	    {
	      //To test if we have connected yet, we select on the socket,
	      //to see if its writer returns
	      FD_ZERO(&writer);
	      FD_SET(returnval->fd,&writer);

	      //We need to wait, as long as it takes, so we set no timeout
	      selectnum=select(FD_SETSIZE,0,&writer,0,NULL);

	      //The select failed, this means an error, we couldnt connect
	      if (selectnum<0)
		{
		  socket_destroy(returnval);

		  return 0;
		}
	      if (selectnum>0)
		{
		  //At least one socket (it has to be us) returned data
		  if (FD_ISSET(returnval->fd,&writer))
		    {

		      //We have connected
		      returnval->flags &=~ SOCKET_CONNECTING;
		      returnval->flags |= SOCKET_CONNECTED;
		      returnval->connect_time=time(NULL);
		      return returnval;
		    }
		}
	    }
	}
    }

  //It was an error, and a bad one, close this
  socket_destroy(returnval);

  return 0;
}

//Create a wakeup socket
socketbuf *socket_create_interrupt(void)
{
  int fd[2];
  socketbuf *returnval;
  int dummy;
#ifndef FIONBIO
# ifdef O_NONBLOCK
  int flags;
# endif
#endif

  //create the sockets file descriptor
  if (pipe(fd)==-1)
    return 0;

  //Set non-blocking, so we can check for a data without freezing. If we
  //fail to set non-blocking we must abort, we require it.
#ifdef FIONBIO
  dummy=1;

  if (ioctl(fd[0],FIONBIO,&dummy)<0)
    {
      close(fd[0]);
      close(fd[1]);
      return 0;
    }

  dummy=1;

  if (ioctl(fd[1],FIONBIO,&dummy)<0)
    {
      close(fd[0]);
      close(fd[1]);
      return 0;
    }
#else
# ifdef O_NONBLOCK

  flags=fcntl(fd[0],F_GETFL,0);

  if (flags[0]<0)
    {
      close(fd[0]);
      close(fd[1]);
      return 0;
    }

  if (fcntl(fd[0],F_SETFL,flags|O_NONBLOCK)<0)
    {
      close(fd[0]);
      close(fd[1]);
      return 0;
    }

  flags=fcntl(fd[1],F_GETFL,0);

  if (flags[1]<0)
    {
      close(fd[0]);
      close(fd[1]);
      return 0;
    }

  if (fcntl(fd[1],F_SETFL,flags|O_NONBLOCK)<0)
    {
      close(fd[0]);
      close(fd[1]);
      return 0;
    }

# else
#  error No valid non-blocking method - cannot build;
# endif // O_NONBLOCK
#endif //FIONBIO


  //We have the good file descriptor, set it into a socketbuf structure
  returnval=socket_create(fd[0]);
  returnval->interrupt_fd=fd[1];

  //Note the protocol
  returnval->protocol=SOCKET_INTERRUPT;

  returnval->flags |= SOCKET_CONNECTED;

  returnval->connect_time=time(NULL);

  return returnval;
}

int socket_interrupt(socketbuf *sock)
{
  if (sock->protocol==SOCKET_INTERRUPT)
    {
      write(sock->interrupt_fd,"0",1);
      sock->bytes_out++;
    }

  return 0;
}

//Create a TCPIP connection to a remote socket
socketbuf *socket_create_inet_tcp_wait(const char *host,int port,int wait)
{
  int fd;
  socketbuf *returnval;
  struct sockaddr_in sa;
  int dummy,selectnum;
  struct in_addr inet_address;
  struct hostent *hp;
  fd_set writer;
  struct sockaddr_in peername;
  socklen_t peersize;
#ifndef FIONBIO
# ifdef O_NONBLOCK
  int flags;
# endif
#endif

  //We need the hostname, where will we connect without one
  if (!host || !*host)
    return 0;

  //Create the socket
  fd=socket(AF_INET,SOCK_STREAM,0);

  if (fd<1)
    {
      //Basic socket connection failed, this really shouldnt happen
      return 0;
    }

  memset(&sa,0,sizeof(struct sockaddr_in));

  //Find the hostname
  hp=gethostbyname(host);
  if (!hp)
    //We cant resolve the hostname
    inet_address.s_addr=-1;
  else
    //We have the hostname
    memcpy((char *)&inet_address,hp->h_addr,sizeof(struct in_addr));

  //The hostname was unresolvable, we cant connect to it
  if (inet_address.s_addr==-1)
    {
      close(fd);
      return 0;
    }

  //Set the socket data
  sa.sin_family=AF_INET;
  sa.sin_port=htons(port);
  sa.sin_addr=inet_address;

  //Set reuseaddr
  dummy=1;
  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(char *)&dummy,sizeof(dummy));


  //Set non-blocking, so we can check for a data without freezing

#ifdef FIONBIO
  dummy=1;

  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      close(fd);
      return 0;
    }
#else
#  ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);

  if (flags<0)
    {
      close(fd);
      return 0;
    }

  if (fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)
    {
      close(fd);
      return 0;
    }

# else
#  error No valid non-blocking method - cannot build;
# endif // O_NONBLOCK
#endif //FIONBIO


  //We have a valid socket, now we wrap a socketbuf around it
  returnval=socket_create(fd);

  //Note the protocol
  returnval->protocol=SOCKET_TCP;

  //Note the hostname and the portnumber in the structure
  returnval->host=(char *)malloc(strlen(host)+1);
  strcpy(returnval->host,host);
  returnval->port=port;

  //Now try and actually connect to the remote address
  if (connect(fd,(struct sockaddr *)&sa,sizeof(sa))==0)
    {
      //Connect was successful, we can finish this here
      returnval->flags |= SOCKET_CONNECTED;
      returnval->connect_time=time(NULL);

      return returnval;
    }

  //We have an in-progress connection
  if (errno==EINPROGRESS)
    {
      //Connect was possibly OK, but we havent finished, come back
      //and check later with select
      returnval->flags|=SOCKET_CONNECTING;

      if (!wait)
	{
	  //The caller requested we do not wait for the connection to finish, 
	  //it will now be the callers responsibility to check this using
	  //process_socket
	  return returnval;
	}
      else
	{
	  //We have been requested to keep on waiting for the connection
	  while (returnval->flags & SOCKET_CONNECTING)
	    {
	      //We do this by selecting on the socket, see what the
	      //writer returns
	      FD_ZERO(&writer);
	      FD_SET(returnval->fd,&writer);

	      //Wait forever if needbe
	      selectnum=select(FD_SETSIZE,0,&writer,0,NULL);

	      if (selectnum<0)
		{
		  //There was an error on the select, this means the connection
		  //has definitely died.
		  socket_destroy(returnval);
		  return 0;
		}
	      if (selectnum>0)
		{
		  if (FD_ISSET(returnval->fd,&writer))
		    {
		      //We have a writer, but is it ok or has it failed
		      //to connect, check with getpeername()

		      peersize=sizeof(struct sockaddr_in);

		      if (!getpeername(returnval->fd,
				       (struct sockaddr *)&peername,
				       &peersize))
			{
			  //Connected ok!
			  returnval->flags &=~ SOCKET_CONNECTING;
			  returnval->flags |= SOCKET_CONNECTED;
			  returnval->connect_time=time(NULL);
			  return returnval;
			}
		      else
			{
			  //Connection failed
			  socket_destroy(returnval);
			  return 0;
			}
		    }
		}
	    }
	}
    }

  //It was an error, and a bad one, close this
  socket_destroy(returnval);

  return 0;
}

//Create a UDP socket. Actually this never connects so the
//wait parameter is ignored. It just sets up a route where data can be thrown
//to. With UDP you dont know if it has reached its target or not.
socketbuf *socket_create_inet_udp_wait(const char *host,int port,int wait)
{
  int fd,dummy;
  socketbuf *returnval;
  struct in_addr inet_address;
  struct hostent *hp;
#ifndef FIONBIO
# ifdef O_NONBLOCK
  int flags;
# endif
#endif

  //We need to know where to connect to.
  if (!host || !*host)
    return 0;

  //Create the socket
  fd=socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
  if (fd<1)
    return 0;

  //Now create the data structure around the socket
  returnval=socket_create(fd);

  memset(&returnval->udp_sa,0,sizeof(struct sockaddr_in));

  //Lookup the hostname we are sending to
  hp=gethostbyname(host);
  if (!hp)
    inet_address.s_addr=-1;
  else
    memcpy((char *)&inet_address,hp->h_addr,sizeof(struct in_addr));

  if (inet_address.s_addr==-1)
    {
      //We couldnt resolve the address, destroy the socket
      socket_destroy(returnval);
      close(fd);
      return 0;
    }

  //Save the data for later use in the datastruct
  returnval->udp_sa.sin_family=AF_INET;
  returnval->udp_sa.sin_port=htons(port);
  returnval->udp_sa.sin_addr.s_addr=inet_address.s_addr;

  //Note the protocol
  returnval->protocol=SOCKET_UDP;

  //Save the text representation of the address
  returnval->host=(char *)malloc(strlen(host)+1);
  strcpy(returnval->host,host);
  returnval->port=port;


  //Set non-blocking, so we can check for a data without freezing

#ifdef FIONBIO
  dummy=1;

  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      close(fd);
      return 0;
    }
#else
#  ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);

  if (flags<0)
    {
      close(fd);
      return 0;
    }

  if (fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)
    {
      close(fd);
      return 0;
    }

# else
#  error No valid non-blocking method - cannot build;
# endif // O_NONBLOCK
#endif //FIONBIO

  //While we technically havent connected, we are ready to send data, and thats
  //what is important
  returnval->flags |= SOCKET_CONNECTED;
  returnval->connect_time=time(NULL);

  return returnval;
}

//Test to see of a socket is connected
int socket_connected(socketbuf *sock)
{
  if (sock->flags & SOCKET_DEAD)
    //A dead socket is never connected
    return 0;

  if (sock->flags & SOCKET_CONNECTED)
    //It has connected
    return 1;
  
  return 0;
}

//Test to see if a socket is dead
int socket_dead(socketbuf *sock)
{
  if (sock->flags & SOCKET_DEAD)
    //It is {:-(
    return 1;

  //Its alive! yay!
  return 0;
}

//This function drops a set length of data from the socket This is handy
//For it we have already peeked it, so we HAVE the data, we dont want to
//reallocate. Or if we have a set of data we KNOW is useless
void socket_indata_drop(socketbuf *sock,int len)
{
  //memmove freaks out at a zero length memory move
  if (len==0)
    return;
  
  //Decrease the recorded amount of data stored
  sock->indata->len-=len;

  if (sock->indata->len<1)
    {
      sock->indata->len=0;
      return;
    }

  //move the later data to the start of the buffer
  memmove(sock->indata->buf,sock->indata->buf+len,sock->indata->len);
  
  return;
}

//This function drops a set length of data from the socket OUTBUFFER
//This is dangerous and is only an internal function, dont let the end user
//do it or the whole socket could break, especially in UDP
static void socket_outdata_drop(socketbuf *sock,int len)
{
  //memmove freaks out at a zero length memory move
  if (len==0)
    return;
  
  //Decriment the recorded amount of data
  sock->outdata->len-=len;

  if (sock->outdata->len<1)
    {
      sock->outdata->len=0;
      return;
    }

  //Move the rest to the start
  memmove(sock->outdata->buf,sock->outdata->buf+len,sock->outdata->len);
  
  return;
}

//Free a UDP data packet Fairly obvious
int socket_udp_data_free(socket_udp_data *data)
{
  if (data->data)
    free(data->data);
  free(data);

  return 1;
}

//A 2 way UDP socket has received some data on its return socket
static socket_udp_data *socket_udp2way_indata_action(socketbuf *sock,int pull)
{
  socket_udp_data *returnval;
  socket_intchar len;
  int datalen;

  //All data must be at least 4 bytes - this is the length of the data in the
  //packet
  if (sock->indata->len<4)
    return NULL;

  //get the length of the data
  memcpy(len.c,sock->indata->buf,4);

  datalen=len.i;

  //The packet isnt big enough to hold all the data we expected - this is a
  //corrupted packet, ABORT!
  if (datalen+4 > sock->indata->len)
    return NULL;

  //Create an internal UDP packet to handle the data we have received
  returnval=(socket_udp_data *)calloc(1,sizeof(socket_udp_data));

  //Allocate enough buffer for the incoming data
  returnval->data=(char *)malloc(datalen);
  memcpy(returnval->data,sock->indata->buf+4,datalen);

  returnval->length=datalen;

  //If we are deleting the data - then do so
  if (pull)
    socket_indata_drop(sock,datalen+4);

  //return the UDP data block
  return returnval;
}

//Receive a packet on a basic UDP socket
static socket_udp_data *socket_udp_indata_action(socketbuf *sock,int pull)
{
  socket_udp_data *returnval;
  socket_intchar len;
  int sa_len;
  int datalen;

  //We need to have at least 4 bytes as the length of the data in the packet
  if (sock->indata->len<4)
    return NULL;

  //If this is a 2 way UDP socket, process it using 2 way UDP handlers
  if (sock->udp2w)
    return socket_udp2way_indata_action(sock,pull);

  //Note the length of the sa structure - this is written wholesale
  //into the buffer, this is actually OK
  memcpy(len.c,sock->indata->buf,4);
  sa_len=len.i;


  //Check we have enough space
  if (sa_len+8  > sock->indata->len)
    return NULL;

  //Find the length of the data now.
  memcpy(len.c,sock->indata->buf+4+sa_len,4);
  datalen=len.i;

  //Check we have the whole data packet
  if (sa_len+datalen+8 > sock->indata->len)
    //We dont, its corrupt
    return NULL;

  //Allocate a data structure for the packet
  returnval=(socket_udp_data *)calloc(1,sizeof(socket_udp_data));

  //Store the sa in the data structure.
  memcpy(&returnval->sa,sock->indata->buf+4,sa_len);

  //And the data itself
  returnval->data=(char *)malloc(datalen);
  memcpy(returnval->data,sock->indata->buf+8+sa_len,datalen);
  
  returnval->length=datalen;

  //If we are pulling instead of just looking, delete the data from the buffer
  if (pull)
    socket_indata_drop(sock,8+sa_len+datalen);

  //Return the UDP data packet
  return returnval;
}


//Wrapper function for the user to pull UDP data from the buffer
socket_udp_data *socket_udp_indata_pull(socketbuf *sock)
{
  return socket_udp_indata_action(sock,1);
}

//Wrapper function for the user to look at UDP data without removing it
//from the buffer
socket_udp_data *socket_udp_indata_view(socketbuf *sock)
{
  return socket_udp_indata_action(sock,0);
}

//This is a user function to pull data from any non-UDP socket
char *socket_indata_pull(socketbuf *sock,int len)
{
  char *returnval;

  //Ensure we dont overrun
  if (len > sock->indata->len)
    len=sock->indata->len;

  //Allocate the return buffer
  returnval=(char *)calloc(1,len+1);

  //copy the data
  memcpy(returnval,sock->indata->buf,len);

  //Drop the data from the buffer
  socket_indata_drop(sock,len);

  return returnval;
}

//Allows the user to view the buffer
const char *socket_indata_view(socketbuf *sock)
{
  //Just return the buffer. It is returned const so the user cant mess
  //it up
  return (char *)sock->indata->buf;
}

//Find the length of the incoming data
size_t socket_indata_length(socketbuf *sock)
{
  return sock->indata->len;
}

size_t socket_outdata_length(socketbuf *sock)
{
  //find the length of data still to send
  return sock->outdata->len;
}

//Read a unix listener socket. Not a user function, this is an internal
//function filtered down to when we know what kind of
//socket we have.
static int socket_read_listener_unix(socketbuf *sock)
{
  socketbuf *newsock;
  socklen_t socklen;
  struct sockaddr_un sa;
  int fd,dummy=0;
  struct linger lingerval;
#ifndef FIONBIO
  int flags;
#endif

  //The length of the data passed into accept
  socklen=(socklen_t)sizeof(sa);

  //Accept the new connection on this socket
  fd = accept(sock->fd,(struct sockaddr *) &sa, &socklen);

  if (fd<1)
    {
      //The connection was bad, forget it
      return 0;
    }

  //Set non-blocking on the new socket
#ifdef FIONBIO
  dummy=1;
  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      shutdown(fd,2);
      close(fd);
      return 0;
    }
#else
# ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);
  if (flags < 0)
    {
      shutdown(fd,2);
      close(fd);
      return 0;
    }
  else
    if (fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0)
      {
	shutdown(fd,2);
	close(fd);
	return 0;
      }
# else
#  error no valid non-blocking method
# endif
#endif /*FIONBIO*/

  //We have a new non-blocking socket
  dummy=1;
  
  //Set linger on this, to make sure all data possible is sent when the
  //socket closes
  lingerval.l_onoff=0;
  lingerval.l_linger=0;
  setsockopt(fd,SOL_SOCKET,SO_LINGER,(char *)&lingerval,
             sizeof(struct linger));
  
  //Create the socketbuf to hold the fd
  newsock=socket_create(fd);
  newsock->protocol=SOCKET_UNIX;
  newsock->path=(char *)malloc(strlen(sock->path)+1);
  strcpy(newsock->path,sock->path);

  //This socket is automatically connected (thats what we've been doing)
  newsock->flags |= (SOCKET_CONNECTED|SOCKET_INCOMING);

  //Set the mode to be the same as the socket that accepts (mode is things like
  //sequential data and the like...
  newsock->mode=sock->mode;

  //Set the parent.
  newsock->parent=sock;

  //This is a new child, it is NOT acknowledged by the parent, so we simply
  //put it into a queue waiting for the calling process to acknowledge we 
  //exist
  if (sock->new_children)
    {
      newsock->new_child_next=sock->new_children;
      newsock->new_child_prev=newsock->new_child_next->new_child_prev;
      newsock->new_child_next->new_child_prev=newsock;
      newsock->new_child_prev->new_child_next=newsock;
    }
  else
    {
      newsock->new_child_next=newsock;
      newsock->new_child_prev=newsock;
      sock->new_children=newsock;
    }

  newsock->connect_time=time(NULL);

#ifdef SOCK_SSL
  if (sock->encrypted)
    {
      socket_set_server_key(newsock,sock->server_key_file);
      socket_set_server_cert(newsock,sock->server_cert_file);
      socket_set_encrypted(newsock);
    }
#endif

  return 1;
}

//Some data has been received on the UDP socket. As UDP doesnt have connections
//it just gets data thrown at it, this is unlike other listeners, as we 
//dont just create a new socket here, we have to process the data we receive

static int socket_read_listener_inet_udp(socketbuf *sock,int failkill)
{
  int chars_left,chars_read,total_read;
  void *buf;
  char quickbuf[1024];
  struct sockaddr_in sa;
  socket_intchar len;
  size_t sa_len;

  //Check how much data is there to read
#ifdef FIONREAD
  if (ioctl(sock->fd,FIONREAD,&chars_left)== -1)
#else
# ifdef I_NREAD
  if (ioctl(sock->fd,I_NREAD,&chars_left)== -1)
# else
# error no valid read length method
# endif
#endif
    {
      if (failkill)
	{
	  //The socket had no data, but it was supposed to, that means its
	  //dead
	  sock->flags|=SOCKET_DEAD;
	}
      return 0;
    }

  /*Linkdeath*/
  if (!chars_left)
    {
      if (failkill)
	{
	  sock->flags|=SOCKET_DEAD;
	}
      return 0;
    }

  //The buffer to store the data in. This is allocated statically as it gets
  //used and reused and there is NO point in creating it time and time
  //again **change** It wasnt threadsafe - oops
  if (chars_left < 1024)
    buf=quickbuf;
  else
    buf=malloc(chars_left);

  total_read=0;

  //Loop while there is data to read
  while (chars_left>0)
    {
      sa_len=sizeof(struct sockaddr);

      //Actually perfrorm the read from the UDP socket
      chars_read=recvfrom(sock->fd,
			  buf,
			  chars_left,
			  0,
			  (struct sockaddr *)&sa,
			  &sa_len);

      if (chars_read==-1)
	{
	  if (errno!=EAGAIN) /*An EAGAIN simply means that it wasnt quite ready
			       so try again later.*/
	    {
	      //There was an error on the read, dead socket
	      sock->flags|=SOCKET_DEAD;
	    }
	  if (buf!=quickbuf)
	    free(buf);
	  return 0;
	}

      if (chars_read==0)
	{
	  //No chars were read, so nothing was ready, try again next time
	  if (buf!=quickbuf)
	    free(buf);
	  return 0;
	}

      //Note that the socket received data, this is to stop it timing out,
      //as UDP sockets are stateless
      sock->udp2w_lastmsg=time(NULL);

#ifdef DEBUG
      //if we are in debug mode, run that now
      if (sock->debug)
	socket_data_debug(sock,(char *)buf,chars_read,0);
#endif

      //We are a 2 way UDP socket, process the data via the UDP2W data handler
      if (sock->udp2w)
	socket_udp2way_listener_data_process(sock,
					     &sa,sa_len,
					     (signed char *)buf,chars_read);
      else
	{
	  //We are a one way UDP socket

	  //Add the sa to the datastream
	  len.i=sa_len;
	  dynstringRawappend(sock->indata,len.c,4);
	  dynstringRawappend(sock->indata,(char *)&sa,sa_len);

	  //Then the data
	  len.i=chars_read;
	  dynstringRawappend(sock->indata,len.c,4);
	  dynstringRawappend(sock->indata,(char *)buf,chars_read);
	}
      //Note how many chars have been read, and loop back to see if we have
      //another packets worth of data to read
      chars_left-=chars_read;
      sock->bytes_in+=chars_read;
      total_read+=chars_read;
    }

  if (buf!=quickbuf)
    free(buf);

  //Try again for more packets, but bear in mind it is OK if there are none, so
  //we set the failkill parameter to 0
  socket_read_listener_inet_udp(sock,0);

  return total_read;
}

//Read the listener of a TCPIP socket
static int socket_read_listener_inet_tcp(socketbuf *sock)
{
  socketbuf *newsock;
  socklen_t socklen;
  struct sockaddr_in sa;
  int fd,dummy=0;
  struct linger lingerval;
#ifndef FIONBIO
  int flags;
#endif

  //The length of the data passed into accept
  socklen=(socklen_t)sizeof(sa);

  //Get the incoming socket
  fd = accept(sock->fd,(struct sockaddr *) &sa, &socklen);

  if (fd<1)
    {
      //It was a bad socket, drop it
      return 0;
    }

  //Set it to be non-blocking
#ifdef FIONBIO
  dummy=1;
  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      shutdown(fd,2);
      close(fd);
      return 0;
    }
#else
# ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);
  if (flags < 0)
    {
      shutdown(fd,2);
      close(fd);
      return 0;
    }
  else
    if (fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0)
      {
	shutdown(fd,2);
	close(fd);
	return 0;
      }
# else
#  error no valid non-blocking method
# endif  
#endif /*FIONBIO*/


  //Set linger so that the socket will send all its data when it close()s
  lingerval.l_onoff=0;
  lingerval.l_linger=0;
  setsockopt(fd,SOL_SOCKET,SO_LINGER,(char *)&lingerval,
             sizeof(struct linger));
  
  //Create the socketbuf to hold the socket
  newsock=socket_create(fd);
  newsock->protocol=SOCKET_TCP;
  newsock->port=ntohs(sa.sin_port);
  newsock->host=(char *)malloc(strlen(inet_ntoa(sa.sin_addr))+1);
  strcpy(newsock->host,inet_ntoa(sa.sin_addr));

  //This is a connected socket so note it as such
  newsock->flags |= (SOCKET_CONNECTED|SOCKET_INCOMING);
  newsock->mode=sock->mode;

  //Link this into the parent so that the calling program can
  //actually get hold of this socket
  newsock->parent=sock;

  if (sock->new_children)
    {
      newsock->new_child_next=sock->new_children;
      newsock->new_child_prev=newsock->new_child_next->new_child_prev;
      newsock->new_child_next->new_child_prev=newsock;
      newsock->new_child_prev->new_child_next=newsock;
    }
  else
    {
      newsock->new_child_next=newsock;
      newsock->new_child_prev=newsock;
      sock->new_children=newsock;
    }

  newsock->connect_time=time(NULL);


#ifdef SOCK_SSL
  if (sock->encrypted)
    {
      socket_set_encrypted(newsock);
    }
#endif

  return 1;
}


//Generic function to wrap all listener read functions. It simply
//Looks at the protocol and calls the appropriate function
static int socket_read_listener(socketbuf *sock)
{
  switch (sock->protocol)
    {
    case SOCKET_TCP:
      return socket_read_listener_inet_tcp(sock);
      break;
    case SOCKET_UDP:
      return socket_read_listener_inet_udp(sock,1);
      break;
    case SOCKET_UNIX:
      return socket_read_listener_unix(sock);
      break;
    case SOCKET_INTERRUPT:
      return 0;
      break;
    }

  //Couldnt find a listener handler - erm, that cant happen!
  return -1;
}

//Read a 2 way UDP socket. This will be the return socket on the client,
//as the outbound is read on the listener. Technicallt this is also a
//listener but it can only belong to one socketbuf so we can skip a load
//of the ownership tests that happen lower down the line
static int socket_udp2way_read(socketbuf *sock,int failkill)
{
  int chars_left,chars_read,total_read;
  void *buf=0;
  char quickbuf[1024];
  struct sockaddr_in sa;
  size_t sa_len;


  //Check how much data is there to read
#ifdef FIONREAD
  if (ioctl(sock->udp2w_infd,FIONREAD,&chars_left)== -1)
#else
# ifdef I_NREAD
  if (ioctl(sock->udp2w_infd,I_NREAD,&chars_left)== -1)
# else
# error no valid read length method
# endif
#endif
    {
      if (failkill)
	{
	  //Kill the socket, there is no data when we expected there would be
	  sock->flags|=SOCKET_DEAD;
	}
      return 0;
    }

  /*Linkdeath*/
  if (!chars_left)
    {
      if (failkill)
	{
	  sock->flags|=SOCKET_DEAD;
	}
      return 0;
    }

  //The buffer to store the data in. This is allocated statically as it gets
  //used and reused and there is NO point in creating it time and time
  //again
  if (chars_left<1024)
    buf=quickbuf;
  else
    buf=malloc(chars_left+1);

  total_read=0;

  //Loop while there is data to read
  while (chars_left>0)
    {
      sa_len=sizeof(struct sockaddr);

      //Actually perfrorm the read from the UDP socket
      chars_read=recvfrom(sock->udp2w_infd,
			  buf,
			  chars_left,
			  0,
			  (struct sockaddr *)&sa,
			  &sa_len);

      if (chars_read==-1)
	{
	  if (errno!=EAGAIN) /*An EAGAIN simply means that it wasnt quite ready
			       so try again later.*/

	    {
              //There was an error on the read, dead socket
	      sock->flags|=SOCKET_DEAD;
	    }

	  if (buf!=quickbuf)
	    free(buf);

	  return 0;
	}

      if (chars_read==0)
	{
	  //No chars were read, so nothing was ready, try again next time
	  if (buf!=quickbuf)
	    free(buf);

	  return 0;
	}

      //Note that the socket received data, this is to stop it timing out,
      //as UDP sockets are stateless
      sock->udp2w_lastmsg=time(NULL);

#ifdef DEBUG
      //if we are in debug mode, run that now
      if (sock->debug)
	socket_data_debug(sock,(char *)buf,chars_read,0);
#endif

      //We ARE a 2 way UDP socket reader, pass this data off to that
      //handler
      socket_udp2way_reader_data_process(sock,(signed char *)buf,chars_read);

      //Note how many chars have been read, and loop back to see if we have
      //another packets worth of data to read
      chars_left-=chars_read;
      sock->bytes_in+=chars_read;
      total_read+=chars_read;
    }
  
  if (buf!=quickbuf)
    free(buf);

  //Try again for more packets, but bear in mind it is OK if there are none, so
  //we set the failkill parameter to 0
  socket_udp2way_read(sock,0);

  return total_read;
}

//This is the generic function called to read data from the socket into the
//socket buffer. This is not called by the user. The user just looks at the
//buffer. This is called fro any type of socket, and the ones that this is not
//appropriate for it just hands off to other functions. This is THE base read
//functionf or ANY socket
static int socket_read(socketbuf *sock)
{
  int chars_left,chars_read,total_read;
  void *buf;
  char quickbuf[1024];

  //Its a listener, read it differently using accepts
  if (sock->flags & SOCKET_LISTENER)
    return socket_read_listener(sock);

  //Its a UDP socket, all readable UDP sockets are listeners, you cant read
  //an outbound UDP socket
  if (sock->protocol==SOCKET_UDP)
    return 0;

  //Check how much data there is coming in
#ifdef FIONREAD
  if (ioctl(sock->fd,FIONREAD,&chars_left)== -1)
#else
# ifdef I_NREAD
  if (ioctl(sock->fd,I_NREAD,&chars_left)== -1)
# else
#  error no valid read length method
# endif
#endif
    {
      //The ioctl failed, this is a dead-socket case
      sock->flags|=SOCKET_DEAD;
      return 0;
    }

  if (!chars_left)
    {
      /*Linkdeath*/
      sock->flags|=SOCKET_DEAD;
      return 0;
    }


  //The buffer to store the data in. This is allocated statically as it gets
  //used and reused and there is NO point in creating it time and time
  //again
  if (chars_left < 1024)
    buf=quickbuf;
  else
    buf=malloc(chars_left);

  total_read=0;

  //Keep on looping till all data has been read
  while (chars_left>0)
    {
      //actually read the data from the socket
#ifdef SOCK_SSL
      if (sock->encrypted==1)
	chars_read=SSL_read(sock->ssl,buf,chars_left);
      else
#endif
	chars_read=read(sock->fd,buf,chars_left);

      if (chars_read==-1)
	{
	  //there was an error
	  if (errno!=EAGAIN) //EAGAIN isnt bad, it just means try later
	    {
	      //Anything else is bad, the socket is dead
	      sock->flags|=SOCKET_DEAD;
	    }

	  if (buf!=quickbuf)
	    free(buf);

	  return 0;
	}

      if (chars_read==0)
	{
	  //No data was read, it shouldnt happen, if it does, then return from
	  //here.
	  if (buf!=quickbuf)
	    free(buf);

	  return 0;
	}

#ifdef DEBUG
      //If we are in debug mode do that now
      if (sock->debug)
	socket_data_debug(sock,(char *)buf,chars_read,0);
#endif

      //Add the read data into the indata buffer
      if (sock->protocol!=SOCKET_INTERRUPT)
	dynstringRawappend(sock->indata,(char *)buf,chars_read);
      chars_left-=chars_read;
      sock->bytes_in+=chars_read;
      total_read+=chars_read;
    }

  if (buf!=quickbuf)
    free(buf);

  return total_read;
}

//This function actually writes data to the socket. This is NEVER called by
//the user, as the socket could be in any state, and calling from the user
//would just break everything. This is called for stream sockets but not
//for datagram sockets like UDP
static int socket_process_write_stream(socketbuf *sock)
{
  int written;

  //Perform the write. Try and write as much as we can, as fast as we can
  written=write(sock->fd,sock->outdata->buf,sock->outdata->len);
  
  if (written==-1)
    {
      //The write had an error
      if (errno!=EAGAIN) /*EAGAIN simply means that the write buffer is full,
			   try again later, no problem*/
	{
	  //Any other error is fatal
	  sock->flags |= SOCKET_DEAD;
	}
    }
  else if (written > 0)
    {
#ifdef DEBUG
      //In debug mode, run the debug function
      if (sock->debug)
	socket_data_debug(sock,(char *)sock->outdata->buf,written,1);
#endif

      //Drop the written data from the buffer
      socket_outdata_drop(sock,written);
    }

  //Return the number of bytes written, in case they are needed
  return written;
}

//This function actually writes data to the socket. This is NEVER called by
//the user, as the socket could be in any state, and calling from the user
//would just break everything. This is called for datagram sockets but not
//for stream sockets like TCP or UNIX
static int socket_process_write_dgram(socketbuf *sock)
{
  int written;
  socket_intchar towrite;

  //The buffer contains one int of length data and then lots of data to
  //indicate a packet that should be sent all at once
  if (sock->outdata->len<4)
    return 0;

  //This is the length
  memcpy(towrite.c,sock->outdata->buf,4);

  //check we have enough data in the buffer to send it all
  if (sock->outdata->len<4+towrite.i)
    return 0;

  //We have enough, send the data. DO NOT send the initial length header,
  //it will get included in the receive data anyway, so we dont have to send
  //it twice
  written=sendto(sock->fd,
		 sock->outdata->buf+4,towrite.i,
		 MSG_DONTWAIT,
		 (struct sockaddr *)&sock->udp_sa,
		 sizeof(struct sockaddr_in));


  if (written==-1) //There was an error
    {
      if (errno==EMSGSIZE)
	{
	  //Data too big, nothing we can do, drop the packet
	  socket_outdata_drop(sock,towrite.i+4);

	  return 0;
	}
      else if (errno!=EAGAIN) //If the error was EAGAIN just try later
	{
	  //The error was something fatal
	  sock->flags |= SOCKET_DEAD;
	  return 0;
	}
    }
  else if (written > 0)
    {
      //There was data sent

#ifdef DEBUG
      //If we are in debug mode, handle that
      if (sock->debug)
	socket_data_debug(sock,(char *)sock->outdata->buf+4,written,1);
#endif

      //Drop the data from the buffer
      socket_outdata_drop(sock,towrite.i+4);

      //Recurse so we send as much as we can now till its empty or we error
      written += socket_process_write_dgram(sock);
    }

  return written;
}

//This is the generic function to handle writes for ALL sockets
static int socket_process_write(socketbuf *sock)
{
  //Only if we are connected and we have something to send
  if (socket_connected(sock) && sock->outdata && sock->outdata->len>0)
    {
#ifdef SOCK_SSL
      if (sock->encrypted)
	return SSL_write(sock->ssl,sock->outdata->buf,sock->outdata->len);
      else
#endif
	{
	  if (sock->protocol==SOCKET_UDP)
	    return socket_process_write_dgram(sock);
	  else
	    return socket_process_write_stream(sock);
	}
    }

  return 0;
}

//2 way UDP sockets will ping each other to keep the socket alive. They ping 
//every 10 seconds. If the sockets go 60 seconds with no ping, then the 
//socket is considered dead.
static int process_pings(socketbuf *sock)
{
  socket_intchar val;
  int written=0;
  char buf[8];
  time_t this_second;

  //Only ping 2 way UDP sockets
  if (!sock->udp2w)
    return 0;

  //Cant ping from a listener, a listener is inbound
  if (sock->flags & SOCKET_LISTENER)
    return 0;

  //Note the time
  this_second=time(NULL);
  
  //Check we need to send a ping
  if (sock->udp2w_nextping < this_second)
    {
      sock->udp2w_nextping = this_second+10;

      //Create the ping packet
      val.i=htonl(SOCKET_UDP2W_PROTOCOL_PING);
      memcpy(buf,val.c,4);
      
      val.i=htonl(sock->udp2w_port);
      memcpy(buf+4,val.c,4);

      //Actually send the ping
      written=sendto(sock->fd,
		     buf,8,
		     MSG_DONTWAIT,
		     (struct sockaddr *)&sock->udp_sa,
		     sizeof(struct sockaddr_in));
      
      if (written==-1)
	{
	  if (errno!=EAGAIN)
	    {
	      //Note the socket as dead if we cant send it
	      sock->flags |= SOCKET_DEAD;
	      return 0;
	    }
	}
    }

  //Now we look at if its expired, over 60 seconds since any communication
  if (sock->flags & SOCKET_CONNECTING)
    {
      //Or a much smaller 8 seconds if we are trying to connect
      if (this_second>sock->udp2w_lastmsg+8)
	{
	  sock->flags |= SOCKET_DEAD;
	}
    }
  else
    {
      if (this_second>sock->udp2w_lastmsg+60)
	{
	  sock->flags |= SOCKET_DEAD;
	}
    }

  return written;
}

//This function handles reliable UDP resending packets. UDP does not
//guarentee transmission, so when we send a reliable packet, we get a repsonse
//from the other end. When that response comes through we can assume the
//packet is ok. Until then, we keep resending it on a time that is based on
//the average round trip packet time. This allows for congested networks
static int process_resends(socketbuf *sock)
{
  struct timeval time_now,target_time;
  long long us;
  socket_udp_rdata *scan;
  int newlen;
  socket_intchar udplen,udpdata;

  //Only do this for 2 way UDP sockets
  if (!sock->udp2w)
    return 0;

  //If there are no outbound packets to confirm, nothing to do
  if (!sock->udp2w_routpacket)
    return 0;


  //Now we need to find the exact time, as well as find which ones need 
  //resending
  gettimeofday(&time_now,NULL);

  //Find how old a packet needs to be
  us=sock->udp2w_averound/5; //Twice as long as average for a resend,/10*2 = /5

  target_time.tv_sec=time_now.tv_sec-(us/1000000);
  target_time.tv_usec=time_now.tv_usec-(us%1000000);

  if (target_time.tv_usec<0)
    {
      target_time.tv_usec+=1000000;
      target_time.tv_sec--;
    }

  scan=sock->udp2w_rdata_out;

  while (scan)
    {
      //Loop through checking each packet

      if (target_time.tv_sec > scan->sendtime.tv_sec ||
	  (target_time.tv_sec == scan->sendtime.tv_sec &&
	   target_time.tv_usec > scan->sendtime.tv_usec))
	{
	  //This packet needs resending

	  //Find the length the packet needs to be
	  newlen=scan->length;
	  newlen+=8;
	  if (sock->udp2w_infd)
	    newlen+=4;

	  //Set this length into the buffer
	  udplen.i=newlen;
	  dynstringRawappend(sock->outdata,udplen.c,4);
	  
	  //Send the protocol
	  udpdata.i=htonl(SOCKET_UDP2W_PROTOCOL_RDATA);

	  dynstringRawappend(sock->outdata,udpdata.c,4);
	  
	  //Send the port
	  if (sock->udp2w_infd)
	    {
	      udpdata.i=htonl(sock->udp2w_port);
	      dynstringRawappend(sock->outdata,udpdata.c,4);
	    }
	  
	  //Send the packet number
	  udpdata.i=htonl(scan->packetnum);
	  dynstringRawappend(sock->outdata,udpdata.c,4);
	  
	  //Send the data
	  dynstringRawappend(sock->outdata,scan->data,scan->length);

	  //note the new send time
	  scan->sendtime.tv_sec=time_now.tv_sec;
	  scan->sendtime.tv_usec=time_now.tv_usec;
	}

      //Next packet
      scan=scan->next;
      if (scan==sock->udp2w_rdata_out)
	scan=NULL;
    }

  return 0;
}

//This is the main function called to process user sockets. It handles
//calls to both input and output as well as processing incoming sockets
//and noting dead sockets as being dead. This is a program-called
//function and should be called often.
//Actual data is NOT returned from this function, this function simply
//calls appropriate subfunctions which update the internal buffers of
//sockets. It is the calling programs job to process this data.
int socket_process_sockets(socket_processlist *list,long int timeout)
{
  socket_processlist *scan;
  socketbuf *sock;
  fd_set readers,writers;
  struct timeval select_timeout;
  int count,selectnum;

  scan=list;

  //Loop through each socket in the list we have been handed
  while (scan)
    {
      sock=scan->sock;

      if (sock->udp2w)
	{
	  //If the socket is a 2 way UDP socket, process resends and pings
	  process_resends(sock);
	  process_pings(sock);
	}

      //Now process outbound writes (that will include any resends that have
      //just been created
#ifdef SOCK_SSL
      if (sock->encrypted>1)
	socket_process_ssl(sock);
      else 
#endif
	socket_process_write(sock);

      scan=scan->next;
      if (scan==list)
	scan=NULL;
    }  

  count=0;
  FD_ZERO(&readers);
  FD_ZERO(&writers);

  scan=list;

  //Loop through all sockets again
  while (scan)
    {
      sock=scan->sock;

      //If the socket is alive
      if (
#ifdef SOCK_SSL
	  sock->encrypted<2 && 
#endif
	  !(sock->flags & SOCKET_DEAD))
	{
	  if (sock->flags & SOCKET_CONNECTING)
	    {
	      //This is a socket in the connecting state. See if it has now
	      //connected
	      if (sock->udp2w)
		{
		  //A connecting 2 way socket is one we need to send a 
		  //connection message to again
		  socket_udp2way_connectmessage(sock);

		  if (sock->udp2w_infd)
		    {
		      //Now set its reader socket to look for a response
		      FD_SET(sock->udp2w_infd,&readers);
		      count++;
		    }
		}
	      else
		{
		  //This socket should be set as a writer, as this will change
		  //when a stream socket connection state changes
		  FD_SET(sock->fd,&writers);
		  count++;
		}
	    }
	  else if (sock->flags & SOCKET_CONNECTED)
	    {
	      //This socket is alredy connected
	      if (sock->udp2w_infd)
		{
		  //If tehre is a UDP reading socket, we must use that to read
		  FD_SET(sock->udp2w_infd,&readers);
		  count++;
		}
	      else
		{
		  //Set the main socket as a reader
		  FD_SET(sock->fd,&readers);
		  count++;
		}
	    }
	}

      scan=scan->next;
      if (scan==list)
	scan=NULL;
    }

  if (!count)
    //No valid sockets were ready to read, no point in reading them
    return 0;

  //Set the timeout to be as requested by the caller
  select_timeout.tv_sec=timeout/1000000;
  select_timeout.tv_usec=timeout%1000000;

  //Now actually run the select
  selectnum=select(FD_SETSIZE,&readers,&writers,0,&select_timeout);

  if (selectnum<1)
    //Select was an error, or had no returns, we have nothing new to do now
    return 0;

  //We have a result


  //Loop through all sockets see what we can see
  scan=list;

  while (scan)
    {
      sock=scan->sock;
      if (
#ifdef SOCK_SSL
	  sock->encrypted<2 && 
#endif
	  !(sock->flags & SOCKET_DEAD))
	{
	  if (sock->flags & SOCKET_CONNECTING)
	    {
	      //A connecting socket
	      if (sock->udp2w_infd)
		{
		  //Its a 2 way UDP - if it had any data
		  if (FD_ISSET(sock->udp2w_infd,&readers))
		    //Then send this to be processed. This will handle the
		    //connection data if that is what is received
		    socket_udp2way_read(sock,1);
		}
	      else
		{
		  //Nothing here for a UDP socket
		  if (sock->protocol!=SOCKET_UDP)
		    {
		      //If it has a writer, we simply assume it is done and 
		      //the first write will fail. Not very efficient but
		      //it works
		      if (FD_ISSET(sock->fd,&writers))
			{
			  sock->flags &=~ SOCKET_CONNECTING;
			  sock->flags |= SOCKET_CONNECTED;
			  sock->flags |= SOCKET_DELAYED_NOW_CONNECTED;
			  sock->connect_time=time(NULL);
			}
		    }
		}
	    }
	  else if (sock->flags & SOCKET_CONNECTED)
	    {
	      //This is a connected socket, handle it

	      if (sock->udp2w_infd)
		{
		  //2 way UDP socket reader
		  if (FD_ISSET(sock->udp2w_infd,&readers))
		    //Handle its special data
		    socket_udp2way_read(sock,1);
		}
	      else
		{
		  if (FD_ISSET(sock->fd,&readers))
		    {
		      //Any other socket, read it using the generic read function
		      socket_read(sock);
		    }
		}
	    }
	}

      scan=scan->next;
      if (scan==list)
	scan=NULL;
    }

  //We're done, return how many sockets were affected
  return count;
}


//This is a wrapper function for processing one single socket. It makes it
//into a socketlist of one entry, and sends it to the previous
//function for handling.
int socket_process(socketbuf *sock,long int timeout)
{
  socket_processlist listofone;

  listofone.next=&listofone;
  listofone.prev=&listofone;
  listofone.sock=sock;

  return socket_process_sockets(&listofone,timeout);
}

//Wrapper function to create a unix socket. It is assumed that wait is the
//required functionality
socketbuf *socket_create_unix(const char *path)
{
  return socket_create_unix_wait(path,1);
}

//Wrapper function to create a TCPIP socket. It is assumed that wait is the
//required functionality
socketbuf *socket_create_inet_tcp(const char *host,int port)
{
  return socket_create_inet_tcp_wait(host,port,1);
}

//Create a tcpip socket on a specific IP address
socketbuf *socket_create_inet_tcp_listener_on_ip(const char *localip,int port)
{
  struct sockaddr_in sa;
  int dummy=0;
  char hostname[HOST_NAME_MAX+1];
  struct hostent *hp;
  int fd;
  socketbuf *sock;
  struct in_addr inet_address;
  struct linger lingerval;
#ifndef FIONBIO
#ifdef O_NONBLOCK
  int flags;
#endif
#endif

  //set the hostname. If this is passed in, use that, otherwise use gethostname
  memset(&sa,0,sizeof(struct sockaddr_in));
  if (localip)
    strcpy(hostname,localip);
  else
    gethostname(hostname,HOST_NAME_MAX);

  //Simply gethostbyname which handles all kinds of addresses
  hp=gethostbyname(hostname);

  if (!hp)
    //We couldnt resolve the host fail
    return 0;

  if (localip)
    {
      //Se specifically requested an IP address, so we use it, thus restricting
      //to just one interface. If we didnt specify the address then we skip
      //this section which in effect means that the socket will bind to all
      //IP addresses on the system
      memcpy((char *)&inet_address,hp->h_addr,sizeof(struct in_addr));
      if (inet_address.s_addr!=-1)
	sa.sin_addr.s_addr=inet_address.s_addr;
    }
  sa.sin_family=hp->h_addrtype;
  sa.sin_port = htons(port);

  //Create the socket
  fd = socket(AF_INET,SOCK_STREAM,0);
  
  if (fd < 0)
    {
      //We couldnt create the socket!
      return 0;
    }

  dummy=1;
  //Set REUSEADDR so that if the system goes down it can go right 
  //back up again. Otherwise it will block until all data is processed
  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(char *)&dummy,sizeof(dummy));

  //Linger so that data is sent when the socket closes
  lingerval.l_onoff=0;
  lingerval.l_linger=0;
  setsockopt(fd,SOL_SOCKET,SO_LINGER,(char *)&lingerval,
	     sizeof(struct linger));

  //set non-blocking
#ifdef FIONBIO
  dummy=1;
  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      close(fd);
      return 0;
    }
#else
# ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);
  if (flags < 0)
    {
      close(fd);
      return 0;
    }
  else
    if (fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0)
      {
	close(fd);
	return 0;
      }
# else
#  error no valid non-blocking method
# endif
#endif /*FIONBIO*/

  //Now bind the socket to the port
  if (bind(fd,(struct sockaddr *)&sa,sizeof(sa))<0)
    {
      //We failed, maybe something else is already bound, maybe something
      //else, regardless, we're stuffed
      shutdown(fd,2);
      close(fd);
      return 0;
    }

  //Listen with the maximum number - this means we can have as many incoming
  //connections as the kernel is configured to handle (on the machine that
  //builds of course, it wont magically change itself from machine to machine)
  if (listen(fd,SOMAXCONN)<0)
    {
      shutdown(fd,2);
      close(fd);
      return 0;
    }
  
  //Finally create the socket datastruct to hold the socket
  sock=socket_create(fd);
  sock->protocol=SOCKET_TCP;
  sock->port=port;

  sock->flags |= (SOCKET_CONNECTED|SOCKET_LISTENER);
  sock->connect_time=time(NULL);

  return sock;
}

//Create a listener on ALL sockets
socketbuf *socket_create_inet_tcp_listener(int port)
{
  return socket_create_inet_tcp_listener_on_ip(NULL,port);
}

//Create a UDP listener
socketbuf *socket_create_inet_udp_listener_on_ip(const char *localip,int port)
{
  struct sockaddr_in sa;
  int dummy=0;
  char hostname[HOST_NAME_MAX+1];
  struct hostent *hp;
  int fd;
  socketbuf *sock;
  struct in_addr inet_address;
  struct linger lingerval;
#ifndef FIONBIO
#ifdef O_NONBLOCK
  int flags;
#endif
#endif

  memset(&sa,0,sizeof(struct sockaddr_in));

  //set the hostname. If this is passed in, use that, otherwise use gethostname
  if (localip)
    strcpy(hostname,localip);
  else
    gethostname(hostname,HOST_NAME_MAX);

  //Simply gethostbyname which handles all kinds of addresses
  hp=gethostbyname(hostname);

  if (!hp)
    //We couldnt resolve the host fail
    return 0;

  if (localip)
    {
      //We specifically requested an IP address, so we use it, thus restricting
      //to just one interface. If we didnt specify the address then we skip
      //this section which in effect means that the socket will bind to all
      //IP addresses on the system
      memcpy((char *)&inet_address,hp->h_addr,sizeof(struct in_addr));
      if (inet_address.s_addr!=-1)
        sa.sin_addr=inet_address;
    }
  sa.sin_family=hp->h_addrtype;
  sa.sin_port = htons(port);

  //Create the socket
  fd = socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
  
  if (fd < 0)
    {
      //We couldnt create the socket!
      return 0;
    }

  lingerval.l_onoff=0;
  lingerval.l_linger=0;
  setsockopt(fd,SOL_SOCKET,SO_LINGER,(char *)&lingerval,
	     sizeof(struct linger));


  //Set non-blocking
#ifdef FIONBIO
  dummy=1;
  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      close(fd);
      return 0;
    }
#else
# ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);
  if (flags < 0)
    {
      close(fd);
      return 0;
    }
  else
    if (fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0)
      {
	close(fd);
	return 0;
      }
# else
#  error no valid non-blocking method
# endif
#endif /*FIONBIO*/

  //Bind to the port
  if (bind(fd,(struct sockaddr *)&sa,sizeof(sa))<0)
    {
      //We failed, maybe something else is already bound, maybe something
      //else, regardless, we're stuffed
      shutdown(fd,2);
      close(fd);
      return 0;
    }

  //Finally create the socket datastruct to hold the socket
  sock=socket_create(fd);
  sock->protocol=SOCKET_UDP;
  sock->port=port;

  sock->flags |= (SOCKET_CONNECTED|SOCKET_LISTENER);
  sock->connect_time=time(NULL);

  return sock;
}

//A wrapper function to bind a UDP listener on all interfaces
socketbuf *socket_create_inet_udp_listener(int port)
{
  return socket_create_inet_udp_listener_on_ip(NULL,port);
}

//Create a unix socket listener.
socketbuf *socket_create_unix_listener(const char *path)
{
  int fd,dummy;
  socketbuf *sock;
  struct sockaddr_un sa;
  struct linger lingerval;

  //Create the socket
  fd = socket(PF_UNIX,SOCK_STREAM,0);

  if (fd < 1)
    //Socket creation failed
    return 0;

  //Set the socket types
  sa.sun_family = AF_UNIX;
  strcpy(sa.sun_path,path);

  //Set this to linger so any data being processed will be finished
  lingerval.l_onoff=0;
  lingerval.l_linger=0;
  setsockopt(fd,SOL_SOCKET,SO_LINGER,(char *)&lingerval,
	     sizeof(struct linger));


  //Set nonblocking
#ifdef FIONBIO
  dummy=1;
  if (ioctl(fd,FIONBIO,&dummy)<0)
    {
      close(fd);
      return 0;
    }
#else
# ifdef O_NONBLOCK
  flags=fcntl(fd,F_GETFL,0);
  if (flags < 0)
    {
      close(fd);
      return 0;
    }
  else
    if (fcntl(fd,F_SETFL,flags|O_NONBLOCK) < 0)
      {
	close(fd);
	return 0;
      }
# else
#  error no valid non-blocking method
# endif
#endif /*FIONBIO*/


  //Now bind to the location
  if (bind(fd,(struct sockaddr *)&sa,sizeof(sa)) < 0)
    {
      //We failed. Maybe the file is already there...
      close(fd);
      return 0;
    }

  //Listen with the maximum number - this means we can have as many incoming
  //connections as the kernel is configured to handle (on the machine that
  //builds of course, it wont magically change itself from machine to machine)
  if (listen(fd,SOMAXCONN) < 0)
    {
      shutdown(fd,2);
      close(fd);
      unlink(path);
      return 0;
    }

  //Finally create the socket datastruct to hold the socket
  sock=socket_create(fd);
  sock->protocol=SOCKET_UNIX;
  sock->path=(char *)malloc(strlen(path)+1);
  strcpy(sock->path,path);

  sock->flags |= (SOCKET_CONNECTED|SOCKET_LISTENER);
  sock->connect_time=time(NULL);

  return sock;
}

//This is the function used by the calling program to see if any new 
//connections have come in on a listener socket
socketbuf *socket_new(socketbuf *parent)
{
  socketbuf *returnval;

  //Destroy any sockets that have died in the connection process. This doesnt
  //get them all, just the first ones, until the one at the front is a live.
  //This assumes that this function is called often, and so dead connections
  //are cleaned up regularly.
  while (parent->new_children && socket_dead(parent->new_children))
    {
      returnval=parent->new_children;

      //Unlink the dead socket
      parent->new_children=parent->new_children->new_child_next;

      if (parent->new_children==returnval)
	{
	  parent->new_children=NULL;
	  returnval->new_child_next=NULL;
	  returnval->new_child_prev=NULL;
	}
      else
	{
	  returnval->new_child_next->new_child_prev=returnval->new_child_prev;
	  returnval->new_child_prev->new_child_next=returnval->new_child_next;
	}
      //destroy it
      socket_destroy(returnval);
    }

  //Now look for new sockets
  if (parent->new_children)
    {
      returnval=parent->new_children;

      //Unlink the first socket from the list of new connections
      parent->new_children=parent->new_children->new_child_next;
      
      if (parent->new_children==returnval)
	{
	  parent->new_children=NULL;
	  returnval->new_child_next=NULL;
	  returnval->new_child_prev=NULL;
	}
      else
	{
	  returnval->new_child_next->new_child_prev=returnval->new_child_prev;
	  returnval->new_child_prev->new_child_next=returnval->new_child_next;
	}

      //Now link it in to the list of connected children
      if (parent->connected_children)
	{
	  returnval->connected_child_next=parent->connected_children;
	  returnval->connected_child_prev=
	    parent->connected_children->connected_child_prev;
	  returnval->connected_child_prev->connected_child_next=returnval;
	  returnval->connected_child_next->connected_child_prev=returnval;
	}
      else
	{
	  parent->connected_children=returnval;
	  returnval->connected_child_next=returnval;
	  returnval->connected_child_prev=returnval;
	}

      //Return the new socket
      return returnval;
    }

  //No new connection
  return NULL;
}

//If the socket has just connected (fairly useless, legacy now)
int socket_just_connected(socketbuf *sock)
{
  if (sock->flags & SOCKET_DELAYED_NOW_CONNECTED)
    {
      sock->flags &=~ SOCKET_DELAYED_NOW_CONNECTED;
      return 1;
    }

  return 0;
}

//Return the portnumber
int socket_get_port(socketbuf *sock)
{
  return sock->port;
}

#ifdef DEBUG
//Set debug mode on or off
void socket_debug_off(socketbuf *sock)
{
  sock->debug=0;
}
void socket_debug_on(socketbuf *sock)
{
  sock->debug=1;
}
#endif

//Return the time the socket connected
time_t socket_connecttime(socketbuf *sock)
{
  return sock->connect_time;
}

//Set a flag into the sockets mode. The only flag right now is sequential
//and only applies to 2 way UDP sockets
int socket_mode_set(socketbuf *sock,unsigned int mode)
{
  sock->mode|=mode;
  return 1;
}

//Remove a flag from a socket
int socket_mode_unset(socketbuf *sock,unsigned int mode)
{
  sock->mode&=~mode;
  return 1;
}

//Get the sockets mode
unsigned int socket_mode_get(socketbuf *sock)
{
  return sock->mode;
}

const char *socket_host_get(socketbuf *sock)
{
  return sock->host;
}

#ifdef SOCK_SSL

void socket_set_encrypted(socketbuf *sock)
{
  static int ssl_initialised=0;

  if (!ssl_initialised)
    {
      ssl_initialised=1;
      SSL_library_init();
      SSL_load_error_strings();
    }

  sock->encrypted=2;
  if (sock->flags & SOCKET_LISTENER)
    socket_set_listener_encrypted(sock);
  else if (sock->flags & SOCKET_INCOMING)
    socket_set_server_encrypted(sock);
  else
    socket_set_client_encrypted(sock);
}


void socket_set_server_key(socketbuf *sock,const char *keyfile)
{
  sock->server_key_file=(char *)malloc(strlen(keyfile)+1);
  strcpy(sock->server_key_file,keyfile);
}

void socket_set_server_cert(socketbuf *sock,const char *certfile)
{
  sock->server_cert_file=(char *)malloc(strlen(certfile)+1);
  strcpy(sock->server_cert_file,certfile);
}

void socket_set_client_ca(socketbuf *sock,const char *cafile)
{
  sock->client_ca_file=(char *)malloc(strlen(cafile)+1);
  strcpy(sock->client_ca_file,cafile);
}

#endif //SOCK_SSL

//This moves the buffers from one socket to another, MOVING it, not
//copying it. This is not a simple task as it needs to take into
//account all buffers in the UDP resend queue to!
void socket_relocate_data(socketbuf *from,socketbuf *to)
{
  socket_udp_rdata *scan,*target;

  //Transfer data in the resend queue to the new out queue. Do this first so it
  //goes back out in order
  while (from->udp2w_rdata_out)
    {
      scan=from->udp2w_rdata_out;
      target=scan;
      while (scan)
	{
	  if (scan->packetnum < target->packetnum)
	    target=scan;
	  scan=scan->next;
	  if (scan==from->udp2w_rdata_out)
	    scan=NULL;
	}

      //Add this data to the out queue
      socket_write_reliable(to,
			    target->data,target->length);

      //Now unlink that target
      from->udp2w_rdata_out=socket_rdata_delete(from->udp2w_rdata_out,target);
    }


  //Transfer the old outqueue to the new outqueue
  dynstringRawappend(to->outdata,(char *)(from->outdata->buf),from->outdata->len);
  from->outdata->len=0;

  //Transfer any unread inqueue to the new inqueue
  dynstringRawappend(to->indata,(char *)(from->indata->buf),from->indata->len);
  from->indata->len=0;

  //We're done
  return;
}


/****************************************************************************
 **                Extention for 2-way and reliable UDP                    **
 ** This is NOT generally compatable, you need to run this socket code     **
 ** at both ends                                                           **
 ****************************************************************************/

//Send the connection message to a remote listener. This initialises the
//session
int socket_udp2way_connectmessage(socketbuf *sock)
{
  int written;
  int datalen;
  socket_intchar intval;
  char buf[HOST_NAME_MAX+60+1+12];

  //Now we construct new data to send
  //Data is as follows:

  //4 bytes: Protocol
  //4 bytes: Local portnumber
  //4 bytes: The length of the unique identifier string
  //       : The unique identifier string
  //

  intval.i=htonl(SOCKET_UDP2W_PROTOCOL_CONNECTION);
  memcpy(buf,intval.c,4);

  intval.i=htonl(sock->udp2w_port);
  memcpy(buf+4,intval.c,4);

  datalen=strlen(sock->udp2w_unique);
  intval.i=htonl(datalen);
  memcpy(buf+8,intval.c,4);

  memcpy(buf+12,sock->udp2w_unique,datalen);

  //Send the data to the socket
  written=sendto(sock->fd,
                 buf,12+datalen,
                 MSG_DONTWAIT,
                 (struct sockaddr *)&sock->udp_sa,
                 sizeof(struct sockaddr_in));

  if (written==-1)
    {
      if (errno!=EAGAIN)
        {
	  //If the error is not EAGAIN, its dead
          sock->flags |= SOCKET_DEAD;
          return 0;
        }
    }

  //Return the number of bytes sent
  return written;
}


//This is sent as a reply, the connectmessage has been received, now we need to
//acknowledge it
static int socket_udp2way_connectmessage_reply(socketbuf *sock)
{
  int written;
  socket_intchar val;
  char buf[4];

  //Now we construct new data to send
  //4 bytes : protocol

  val.i=htonl(SOCKET_UDP2W_PROTOCOL_CONNECTION);
  memcpy(buf,val.c,4);

  //Send the data
  written=sendto(sock->fd,
                 buf,4,
                 MSG_DONTWAIT,
                 (struct sockaddr *)&sock->udp_sa,
                 sizeof(struct sockaddr_in));

  if (written==-1)
    {
      if (errno!=EAGAIN)
        {
	  //This is a fatal error, kill the socket
          sock->flags |= SOCKET_DEAD;
          return 0;
        }
    }

  return written;
}


//This function creates a 2 way UDP socket connection to a remote host
socketbuf *socket_create_inet_udp2way_wait(const char *host,int port,int wait)
{
  socketbuf *sock,*insock;
  int inport;
  struct timeval time_now;
  char hostname[HOST_NAME_MAX+1];

  //Simply - we create a socket outbound
  sock=socket_create_inet_udp_wait(host,port,wait);

  if (!sock)
    return 0;

  //Now create a listener, NOT on the same port but on a port that is
  //as close as we can make it. We dont do it on the same port as it is
  //possible that in a failover condition, the main socket may be bound
  //later, causing inability to failover servers
  insock=NULL;
  inport=port;
  while (!insock)
    {
      inport++;
      //Create a listener on the new socket
      insock=socket_create_inet_udp_listener(inport);
    }


  //We now have 2 socketbufs, but we really only want one - lets take the
  //actual inbound socket and put it in the first sockets data, then kill 
  //the second set of data

  sock->udp2w_infd=insock->fd;
  sock->udp2w_port=inport;

  sock->udp2w=1;
  sock->udp2w_averound=2500000; /*Set it for a sloooooow network, it will 
				  modify itself if the network shows it is
				  faster*/
  sock->udp2w_lastmsg=time(NULL);

  insock->fd=0;

  //Get rid of the unused listener data struct
  socket_destroy(insock);

  sock->flags|=SOCKET_CONNECTING;
  sock->flags&=~SOCKET_CONNECTED;
  //Send the init data

  //We CANNOT wait for the response, or it will never get there if the
  //client and server run on the same thread

  //Now we set a unique value, as we use UDP, so the other side knows WHO
  //is connecting
  gethostname(hostname,HOST_NAME_MAX);

  gettimeofday(&time_now,NULL);

  //Create a unique name for this client. This will be unique anywhere unless
  //you have 2 connections on the same machine in the same microsecond. Pretty
  //fullproof I reckon
  sprintf(sock->udp2w_unique,"%s-%ld.%ld",hostname,
	  time_now.tv_sec,time_now.tv_usec);

  //Send the connect protocol message to the remote server
  socket_udp2way_connectmessage(sock);

  return sock;
}

//Create the 2 way listener on a specific IP address
socketbuf *socket_create_inet_udp2way_listener_on_ip(const char *localip,
						     int port)
{
  socketbuf *sock;

  //Create the basic UDP listener
  sock=socket_create_inet_udp_listener_on_ip(localip,port);

  //All we do extra is the 2 way UDP specific values
  if (sock)
    {
      sock->udp2w=1;
      sock->udp2w_averound=2500000;
      sock->udp2w_lastmsg=time(NULL);
    }

  return sock;
}

//Wrapper function, to set a 2 way UDP listener bound to all interfaces
socketbuf *socket_create_inet_udp2way_listener(int port)
{
  return socket_create_inet_udp2way_listener_on_ip(NULL,port);
}

//This function takes a bit of explaining.
//UDP always sends data to the 'listener' socket, not to a socket specific to
//the user. This means that all data comes in to the one socket and then needs
//to be associated with a socket for THAT user
//So, each packet contains the IP address and the portnumber of the sender
//which allows unique identification. This function looks at all sockets
//that are children of the listener, and finds the one that matches the host
//and the portnumber of the sender.
static socketbuf *socket_get_child_socketbuf(socketbuf *sock,
					     char *host,int port)
{
  socketbuf *scan;

  scan=sock->new_children;
  while (scan)
    {
      if (!strcmp(scan->host,host) && scan->port==port && !socket_dead(scan))
	return scan;
      scan=scan->new_child_next;
      if (scan==sock->new_children)
	scan=NULL;
    }

  scan=sock->connected_children;
  while (scan)
    {
      if (!strcmp(scan->host,host) && scan->port==port && !socket_dead(scan))
	return scan;
      scan=scan->connected_child_next;
      if (scan==sock->connected_children)
	scan=NULL;
    }

  return NULL;
}

//This works in the same way as above, but instead of using the
//hostname it uses the sockaddr_in function as its host comparison. It
//works in the same way but is slightly quicker as it uses an integer
//== operation instead of a strcmp
static socketbuf *socket_get_child_socketbuf_sa(socketbuf *sock,
						struct sockaddr_in *sa,
						int port)
{
  socketbuf *scan;

  scan=sock->new_children;
  while (scan)
    {
      if (scan->udp_sa.sin_addr.s_addr==sa->sin_addr.s_addr &&
	  scan->port==port && !socket_dead(scan))
	return scan;

      scan=scan->new_child_next;
      if (scan==sock->new_children)
	scan=NULL;
    }

  scan=sock->connected_children;
  while (scan)
    {
      if (scan->udp_sa.sin_addr.s_addr==sa->sin_addr.s_addr &&
	  scan->port==port && !socket_dead(scan))
	return scan;
      scan=scan->connected_child_next;
      if (scan==sock->connected_children)
	scan=NULL;
    }

  return NULL;
}

//This function is called when a 2 way UDP connection is received. This
//means that we need to find out if we are already connected, and then
//connect back to them if we arent. We must also allow for the fact that
//someone may reconnect when we THINK they are already connected
static socketbuf *socket_udp2way_listener_create_connection(socketbuf *sock,
							    struct sockaddr_in *sa,
							    size_t sa_len,
							    int port,
							    char *unique)
{
  int fd,dummy;
  socketbuf *returnval,*oldreturnval;
  char host[20];
#ifndef FIONBIO
# ifdef O_NONBLOCK
  int flags;
# endif
#endif

  //Create an outgoing socket back to the originator

  //Find their IP address
  inet_ntop(AF_INET,(void *)(&sa->sin_addr),host,19);

  //Find if anyone else is connected to this listener from that port
  returnval=socket_get_child_socketbuf(sock,host,port);


  //Note if we have no match already connected, this whole loop will not
  //start
  oldreturnval=0;
  while (returnval && returnval!=oldreturnval)
    {
      //We loop here onthe highly unlikely chance we already have 2
      //connections from the same port. This is impossible but just in case,
      //its a negligable overhead to be sure

      oldreturnval=returnval;
      /*First, check if we are already connected to THIS one*/

      if (returnval->udp2w_unique && strcmp(returnval->udp2w_unique,unique))
	{
	  //This is a different one, mark THAT one as dead, cos we cant 
	  //have 2 sockets coming from the same place, its impossible. The old
	  //one must be dead
	  returnval->flags |= SOCKET_DEAD;
	}
      returnval=socket_get_child_socketbuf(sock,host,port);
    }

  //We have no match, so we create a new outbound. NOTE: this means if the same
  //connection was made more than once, the socket is not duplicated
  if (!returnval)
    {
      //Create the socket
      fd=socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
      if (fd<1)
	//No socket, no way to procede
	return 0;

      //Create the socketbuf around the fd
      returnval=socket_create(fd);
      
      //Set the udp_sa so we have an identifier for the other end
      memset(&returnval->udp_sa,0,sizeof(struct sockaddr_in));
      
      //Set the destination
      returnval->udp_sa.sin_family=AF_INET;
      returnval->udp_sa.sin_port=htons(port);
      returnval->udp_sa.sin_addr.s_addr=sa->sin_addr.s_addr;
      
      //Set the 2 way UDP stuff
      returnval->protocol=SOCKET_UDP;
      returnval->udp2w=1;
      returnval->udp2w_averound=2500000;
      returnval->udp2w_lastmsg=time(NULL);
      strcpy(returnval->udp2w_unique,unique);
      
      returnval->mode=sock->mode;

      //Record the hostname too
      returnval->host=(char *)malloc(strlen(host)+1);
      strcpy(returnval->host,host);
      
      returnval->port=port;
      
      //Set non-blocking, so we can check for a data without freezing
      
#ifdef FIONBIO
      dummy=1;
      
      if (ioctl(fd,FIONBIO,&dummy)<0)
	{
	  close(fd);
      return 0;
	}
#else
#  ifdef O_NONBLOCK
      flags=fcntl(fd,F_GETFL,0);
      
      if (flags<0)
	{
	  close(fd);
	  return 0;
	}
      
      if (fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)
	{
	  close(fd);
	  return 0;
	}
      
# else
#  error No valid non-blocking method - cannot build;
# endif // O_NONBLOCK
#endif //FIONBIO
      

      //Set the flags to connected
      returnval->flags |= (SOCKET_CONNECTED|SOCKET_INCOMING);
      
      returnval->parent=sock;

      //Link this in to the new children list as it needs to be acknowledged
      //by the calling program
      if (sock->new_children)
	{
	  returnval->new_child_next=sock->new_children;
	  returnval->new_child_prev=returnval->new_child_next->new_child_prev;
	  returnval->new_child_next->new_child_prev=returnval;
	  returnval->new_child_prev->new_child_next=returnval;
	}
      else
	{
	  returnval->new_child_next=returnval;
	  returnval->new_child_prev=returnval;
	  sock->new_children=returnval;
	}

      returnval->connect_time=time(NULL);
    }

  //Send the reply to acknowledge the connection
  socket_udp2way_connectmessage_reply(returnval);
  
  return returnval;
}

//This function acknowledges the sending of a reliable data packet. This will
//tell the sender that the packet has been accepted and can now be dropped.
//If the sender does not receive this by the time the packet resend comes
//around, the packet will be resent.
static int socket_acknowledge_listener_udp_rpacket(socketbuf *sock,
						   int packetnumber)
{
  int written;
  socket_intchar intval;
  char buf[8];

  //Now we construct new data to send

  //4 bytes: Protocol
  //4 bytes: Packet number

  intval.i=htonl(SOCKET_UDP2W_PROTOCOL_RCONFIRM);
  memcpy(buf,intval.c,4);

  intval.i=htonl(packetnumber);
  memcpy(buf+4,intval.c,4);

  //Send to the socket
  written=sendto(sock->fd,
                 buf,8,
                 MSG_DONTWAIT,
                 (struct sockaddr *)&sock->udp_sa,
                 sizeof(struct sockaddr_in));

  if (written==-1)
    {
      if (errno!=EAGAIN)
        {
	  //If the send fails, the socket dies
          sock->flags |= SOCKET_DEAD;
          return 0;
        }
    }

  return written;
}

//This function handles all incoming data sent to a listener socket
//from a client.
int socket_udp2way_listener_data_process(socketbuf *sock,
					 struct sockaddr_in *sa,
					 size_t sa_len,
					 signed char *buf,int datalen)
{
  socket_intchar len,val;
  socket_udp_rdata *oldpacket,*packet;
  socketbuf *client;
  int type,port,packetnumber,uniquelen;
  char unique[HOST_NAME_MAX+60+1];
  struct timeval time_now;
  
  //There must always be at least 4 bytes, that is a protocol header
  if (datalen<4)
    return 0;

  memcpy(val.c,buf,4);
  type=ntohl(val.i);

  //We have the protocol

  if (type==SOCKET_UDP2W_PROTOCOL_CONNECTION)
    {
      //New connection messages need 12 bytes minimum
      //4 Bytes : Protocol
      //4 Bytes : Port number
      //4 Bytes : Length of the unique identifier
      //        : Unique identifier

      if (datalen<12)
	return 0;
      memcpy(val.c,buf+4,4);

      //Now get the unique connection ID that we have
      memcpy(len.c,buf+8,4);
      uniquelen=ntohl(len.i);
      memcpy(unique,buf+12,uniquelen);
      unique[uniquelen]=0;

      //Now call the creat connection function to handle this message
      socket_udp2way_listener_create_connection(sock,sa,sa_len,ntohl(val.i),
						unique);
      
      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_DATA
      )
    {
      //This is user data, it is NOT reliable so we just take it and use
      //it

      // 4 bytes : protocol
      // 4 bytes : originating port
      //         : data

      //This doesnt go on the listeners inbound, it goes on the clients

      //Get the port as the next byte
      memcpy(val.c,buf+4,4);
      port=ntohl(val.i);

      //Locate the client from the ones connected to this listener
      client=socket_get_child_socketbuf_sa(sock,sa,port);
      if (client)
	{
	  //Note that this client has received a message - helps timeouts
	  client->udp2w_lastmsg=time(NULL);

	  //Store the data in this clients incoming data stream
	  len.i=datalen-8;
	  dynstringRawappend(client->indata,len.c,4);
	  dynstringRawappend(client->indata,(char *)buf+8,datalen-8);
	}
      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_RDATA 
      )
    {
      //This is a RELIABLE data packet and requires handling specially

      // 4 bytes : protocol
      // 4 bytes : originating port
      // 4 bytes : packet number
      //         : data


      //This doesnt go on the listeners inbound, it goes on the clients
      if (datalen<8)
	return 0;

      //Get the port as the next byte
      memcpy(val.c,buf+4,4);
      port=ntohl(val.i);

      //Locate the client associated with this message
      client=socket_get_child_socketbuf_sa(sock,sa,port);
      if (client)
	{
	  //Note that this client has received a message - helps timeouts
	  client->udp2w_lastmsg=time(NULL);

	  //get the packet number
	  memcpy(val.c,buf+8,4);
	  packetnumber=ntohl(val.i);

	  //The packet number is important. The packets may need to be
	  //stored in sequence. It may also be a packet we have had before,
	  //as the acknowledgement does not always make it back.

	  //We have in the socketbuf the next packet number we are expecting,
	  //all packets earlier than this have been processed and forgotten

	  
	  if (packetnumber==client->udp2w_rinpacket)
	    {
	      client->udp2w_rinpacket++;
	      //Its the correct next packet - we can just send it to the buffer
	      len.i=datalen-12;
	      dynstringRawappend(client->indata,len.c,4);
	      dynstringRawappend(client->indata,(char *)buf+12,
				   datalen-12);
	    }
	  else if (packetnumber<client->udp2w_rinpacket)
	    {
	      //We've already got this one, ignore it
	    }
	  else if (socket_rdata_locate_packetnum(client->udp2w_rdata_in,
						 packetnumber))
	    {
	      //This packet is one we have received and wating to be processed
	    }
	  else
	    {
	      //This is one we dont have yet, and we also dont have its
	      //predecessor, 
	      //There are 2 ways to handle this:
	      // 1) Sequential mode: 
	      //      We add it onto a queue and wait for the previous ones
	      // 2) Non-sequential mode:
	      //      We deal with it now, but add it to the list anyway, so
	      //      we know its been dealt with.
	      if (!(client->mode & SOCKET_MODE_UDP2W_SEQUENTIAL))
		{
		  //We store the packet, we note that it HAS been sent, so we
		  //can switch between sequential and non-sequential modes
		  //without losing track of the packets we've already processed
		  client->udp2w_rdata_in=rdata_allocate(client->udp2w_rdata_in,
							packetnumber,
							(char *)(buf+12),
							datalen-12,1);

		  //We arent sequential, so we just send it to the out buffer
		  len.i=datalen-12;
		  dynstringRawappend(client->indata,len.c,4);
		  dynstringRawappend(client->indata,(char *)buf+12,
				       datalen-12);
		}
	      else
		//We are sequential, so all we do is add it to the list for
		//later handling
		client->udp2w_rdata_in=rdata_allocate(client->udp2w_rdata_in,
						      packetnumber,
						      (char *)(buf+12),
						      datalen-12,0);
	    }

	  //we may have now got a series of packets we can send, or at least
	  //get rid of. So, we test this.

	  //Check the next accepted packet is not on the received list
	  oldpacket=socket_rdata_locate_packetnum(client->udp2w_rdata_in,
						  client->udp2w_rinpacket);
	  while (oldpacket)
	    {
	      if (oldpacket->sent)
		{
		  //We are sequential, so this hasnt been sent yet
		  len.i=oldpacket->length;
		  dynstringRawappend(client->indata,len.c,4);
		  dynstringRawappend(client->indata,oldpacket->data,
				       oldpacket->length);
		}

	      //Now its 'in the past' delete it
	      client->udp2w_rdata_in=
		socket_rdata_delete(client->udp2w_rdata_in,
				    oldpacket);
	      
	      //Incriment the packet number
	      client->udp2w_rinpacket++;

	      //try the next!
	      oldpacket=socket_rdata_locate_packetnum(client->udp2w_rdata_in,
						      client->udp2w_rinpacket);
	    }

	  //acknowledge the packet we have just received
	  socket_acknowledge_listener_udp_rpacket(client,packetnumber);
	}
      
      return 1;
    }
  
  if (type==SOCKET_UDP2W_PROTOCOL_RCONFIRM
      )
    {
      //This is the confirmation of a packet we sent in reliable mode

      // 4 bytes : protocol
      // 4 bytes : priginating port
      // 4 bytes : packet number

      //This doesnt confirm on the listeners port, but on the clients
      if (datalen<8)
	return 0;
      
      //Get the port as the next byte
      memcpy(val.c,buf+4,4);
      port=ntohl(val.i);

      //Locate the client associated with this
      client=socket_get_child_socketbuf_sa(sock,sa,port);

      if (client)
	{
	  //Record their link is active
	  client->udp2w_lastmsg=time(NULL);

	  //Comes with a packet number
	  memcpy(val.c,buf+8,4);
	  packetnumber=ntohl(val.i);

	  //Locate this packet in the list of packets we are remembering to
	  //resend
	  packet=socket_rdata_locate_packetnum(client->udp2w_rdata_out,
					       packetnumber);

	  if (packet)
	    {
	      //rebalance the timings for knowing when to resend
	      client->udp2w_averound=(long long)(client->udp2w_averound*0.9);

	      gettimeofday(&time_now,NULL);

	      client->udp2w_averound+=
		((time_now.tv_sec-packet->sendtime.tv_sec)*1000000);
	      client->udp2w_averound+=
		(time_now.tv_usec-packet->sendtime.tv_usec);


	      //We've not already been told of the receipt, so delete it
	      client->udp2w_rdata_out=
		socket_rdata_delete(client->udp2w_rdata_out,
				    packet);


	    }
	}
      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_PING)
    {
      //This is a ping packet, low level 'keep the socket alive' protocol

      // 4 bytes : protocol
      // 4 bytes : originating portnumber

      if (datalen<8)
	return 0;

      //Get the port as the next byte
      memcpy(val.c,buf+4,4);
      port=ntohl(val.i);

      //Find the correct client
      client=socket_get_child_socketbuf_sa(sock,sa,port);

      if (client)
	{
	  //Note we have received something
	  client->udp2w_lastmsg=time(NULL);
	}

      return 1;
    }

  return 1;
}

//This function acknowledges the sending of a reliable data packet. This will
//tell the sender that the packet has been accepted and can now be dropped.
//If the sender does not receive this by the time the packet resend comes
//around, the packet will be resent.
static int socket_acknowledge_reader_udp_rpacket(socketbuf *sock,int packetnumber)
{
  int written;
  socket_intchar intval;
  char buf[12];

  //Now we construct new data to send

  //4 bytes: Protocol
  //4 bytes: Our portnumber
  //4 bytes: Packet number

  intval.i=htonl(SOCKET_UDP2W_PROTOCOL_RCONFIRM);
  memcpy(buf,intval.c,4);

  intval.i=htonl(sock->udp2w_port);
  memcpy(buf+4,intval.c,4);

  intval.i=htonl(packetnumber);
  memcpy(buf+8,intval.c,4);

  //Send to the socket
  written=sendto(sock->fd,
                 buf,12,
                 MSG_DONTWAIT,
                 (struct sockaddr *)&sock->udp_sa,
                 sizeof(struct sockaddr_in));

  if (written==-1)
    {
      if (errno!=EAGAIN)
        {
          //If the send fails, the socket dies
          sock->flags |= SOCKET_DEAD;
          return 0;
        }
    }

  return written;
}

//This is VERY similar to the listener reading, except as the data can ONLY
//come from the other end of THIS socket (we are the client) then we dont
//need the portnumber
int socket_udp2way_reader_data_process(socketbuf *sock,
				       signed char *buf,int datalen)
{
  socket_intchar len,val;
  int type;
  socket_udp_rdata *packet,*oldpacket;
  int packetnumber;
  struct timeval time_now;

  //There must always be at least 4 bytes, that is a protocol header
  if (datalen<4)
    return 0;

  memcpy(val.c,buf,4);
  type=ntohl(val.i);

  //We have the protocol
  
  if (type==SOCKET_UDP2W_PROTOCOL_CONNECTION)
    {
      //The server has acknowledged our connection, no more needed, we are 
      //connected

      sock->flags&=~SOCKET_CONNECTING;
      sock->flags|=SOCKET_CONNECTED;

      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_DATA
      )
    {
      //This is user data, it is NOT reliable so we just take it and use
      //it

      //Add it to the users data buffer
      len.i=datalen-4;
      dynstringRawappend(sock->indata,len.c,4);
      dynstringRawappend(sock->indata,(char *)buf+4,datalen-4);

      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_RDATA 
      )
    {
      //This is a RELIABLE data packet and requires handling specially

      // 4 bytes : protocol
      // 4 bytes : packet number
      //         : data

      //Comes with a packet number
      memcpy(val.c,buf+4,4);
      packetnumber=ntohl(val.i);

      //The packet number is important. The packets may need to be
      //stored in sequence. It may also be a packet we have had before,
      //as the acknowledgement does not always make it back.

      //We have in the socketbuf the next packet number we are expecting,
      //all packets earlier than this have been processed and forgotten


      if (packetnumber==sock->udp2w_rinpacket)
	{
	  //Its the correct next packet - we can just send it to the buffer

	  sock->udp2w_rinpacket++;

	  len.i=datalen-8;
	  dynstringRawappend(sock->indata,len.c,4);
	  dynstringRawappend(sock->indata,(char *)buf+8,datalen-8);
	}
      else if (packetnumber<sock->udp2w_rinpacket)
	{
	  //We've already got this one, do nothing
	}
      else if (socket_rdata_locate_packetnum(sock->udp2w_rdata_in,
					     packetnumber))
	{
	  //This is one we already have in the queue - do nothing
	}
      else
	{
	  //This is one we dont have yet, and we also dont have its
	  //predecessor,
	  //There are 2 ways to handle this:
	  // 1) Sequential mode:
	  //      We add it onto a queue and wait for the previous ones
	  // 2) Non-sequential mode:
	  //      We deal with it now, but add it to the list anyway, so
	  //      we know its been dealt with.

	  if (!(sock->mode & SOCKET_MODE_UDP2W_SEQUENTIAL))
	    {
	      //We store the packet, we note that it HAS been sent, so we
	      //can switch between sequential and non-sequential modes
	      //without losing track of the packets we've already processed
	      sock->udp2w_rdata_in=rdata_allocate(sock->udp2w_rdata_in,
						  packetnumber,
						  (char *)(buf+8),
						  datalen-8,1);
	      
	      //We arent sequential, so we just send it to the out buffer
	      len.i=datalen-8;
	      dynstringRawappend(sock->indata,len.c,4);
	      dynstringRawappend(sock->indata,(char *)buf+8,datalen-8);
	    }
	  else
	    //We are sequential, so all we do is add it to the list for
	    //later handling
	    sock->udp2w_rdata_in=rdata_allocate(sock->udp2w_rdata_in,
						packetnumber,
						(char *)(buf+8),
						datalen-8,0);
	}

      //we may have now got a series of packets we can send, or at least
      //get rid of. So, we test this.

      //Check the next accepted packet is not on the received list
      oldpacket=socket_rdata_locate_packetnum(sock->udp2w_rdata_in,
					      sock->udp2w_rinpacket);
      while (oldpacket)
	{
	  if (!oldpacket->sent)
	    {
	      //We are sequential, so this hasnt been sent yet
	      len.i=oldpacket->length;
	      dynstringRawappend(sock->indata,len.c,4);
	      dynstringRawappend(sock->indata,oldpacket->data,
				   oldpacket->length);
	    }

	  //Now its 'in the past' delete it
	  sock->udp2w_rdata_in=socket_rdata_delete(sock->udp2w_rdata_in,
						   oldpacket);

	  //Incriment the packet number
	  sock->udp2w_rinpacket++;

	  //try the next!
	  oldpacket=socket_rdata_locate_packetnum(sock->udp2w_rdata_in,
						  sock->udp2w_rinpacket);
	}

      //acknowledge the packet we have just received
      socket_acknowledge_reader_udp_rpacket(sock,packetnumber);

      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_RCONFIRM 
      )
    {

      //This is the confirmation of a packet we sent in reliable mode

      // 4 bytes : protocol
      // 4 bytes : packet number

      //Comes with a packet number
      memcpy(val.c,buf+4,4);
      packetnumber=ntohl(val.i);

      //Locate this packet in the list of packets we are remembering to
      //resend
      packet=socket_rdata_locate_packetnum(sock->udp2w_rdata_out,
					   packetnumber);
      if (packet)
	{
	  //rebalance the timings, so we know better when to resend
	  sock->udp2w_averound=(long long)(sock->udp2w_averound*0.9);

	  gettimeofday(&time_now,NULL);

	  sock->udp2w_averound+=
	    ((time_now.tv_sec-packet->sendtime.tv_sec)*1000000);
	  sock->udp2w_averound+=(time_now.tv_usec-packet->sendtime.tv_usec);
	  	  
	  //We've not already been told of the receipt, so delete it
	  sock->udp2w_rdata_out=socket_rdata_delete(sock->udp2w_rdata_out,
						    packet);
	}

      return 1;
    }

  if (type==SOCKET_UDP2W_PROTOCOL_PING)
    {
      //This has already done its job by making something happen on the link

      return 1;
    }

  return 1;
}

