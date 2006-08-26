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

#ifndef SOCKET_H
#define SOCKET_H

#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <linux/limits.h>

#ifndef HOST_NAME_MAX
# define HOST_NAME_MAX 255
#endif

#ifdef SOCK_SSL
#include <openssl/ssl.h>
#endif

#include "dynstring.h"

#define SOCKET_LISTENER (1<<0)
#define SOCKET_CONNECTING (1<<1)
#define SOCKET_CONNECTED (1<<2)

#define SOCKET_DELAYED_NOW_CONNECTED (1<<4)
#define SOCKET_DEAD (1<<5)
#define SOCKET_INCOMING (1<<6)

#define SOCKET_TCP (0)
#define SOCKET_UDP (1)
#define SOCKET_UNIX (2)
#define SOCKET_INTERRUPT (3)

#define SOCKET_MODE_UDP2W_SEQUENTIAL (1<<0)

//Internal
#define SOCKET_UDP2W_PROTOCOL_CONNECTION 0
#define SOCKET_UDP2W_PROTOCOL_DATA 1
#define SOCKET_UDP2W_PROTOCOL_RDATA 3
#define SOCKET_UDP2W_PROTOCOL_RCONFIRM 5
#define SOCKET_UDP2W_PROTOCOL_PING 7
//

typedef struct _socketbuf
{
  int fd;

  int debug;

  time_t connect_time;

  size_t bytes_in;
  size_t bytes_out;

  dynstring *indata;
  dynstring *outdata;

  int flags;

  int protocol;

  char *host;
  int port;
  char *path;

  int mode;

  struct sockaddr_in udp_sa;

  int interrupt_fd;

  //2 way UDP extras
  
  int udp2w;
  int udp2w_infd;
  int udp2w_port;
  int udp2w_routpacket;
  int udp2w_rinpacket;
  long long udp2w_averound;
  time_t udp2w_nextping;
  time_t udp2w_lastmsg;
  char udp2w_unique[HOST_NAME_MAX+60+1];
  
  struct _socket_udp_rdata *udp2w_rdata_out;
  struct _socket_udp_rdata *udp2w_rdata_in;

#ifdef SOCK_SSL
  //Encryption stuff
  int encrypted;
  SSL *ssl;
  SSL_CTX *ctx;
  X509 *server_cert;

  char *server_key_file;
  char *server_cert_file;
  char *client_ca_file;
#endif

  struct _socketbuf *parent;

  struct _socketbuf *new_children;
  struct _socketbuf *connected_children;


  struct _socketbuf *new_child_next;
  struct _socketbuf *new_child_prev;
  struct _socketbuf *connected_child_next;
  struct _socketbuf *connected_child_prev;
} socketbuf;  

typedef struct _socket_processlist
{
  socketbuf *sock;
  struct _socket_processlist *next;
  struct _socket_processlist *prev;
} socket_processlist;

typedef struct _socket_udp_data
{
  struct sockaddr_in sa;
  char *data;
  int length;
} socket_udp_data;

typedef struct _socket_udp_rdata
{
  char *data;
  int length;
  int packetnum;
  int sent;
  struct timeval sendtime;
  struct _socket_udp_rdata *next;
  struct _socket_udp_rdata *prev;
} socket_udp_rdata;

typedef union 
{
  int i;
  char c[4];
} socket_intchar;

extern size_t        socket_bytes_out(socketbuf *);
extern size_t        socket_bytes_in(socketbuf *);
extern int           socket_connected(socketbuf *);
extern socketbuf    *socket_create_inet_tcp(const char *,int);
extern socketbuf    *socket_create_inet_tcp_listener_on_ip(const char *,int);
extern socketbuf    *socket_create_inet_tcp_listener(int);
extern socketbuf    *socket_create_inet_udp_listener_on_ip(const char *,int);
extern socketbuf    *socket_create_inet_udp_listener(int);
extern socketbuf    *socket_create_inet_udp2way_listener_on_ip(const char *,int);
extern socketbuf    *socket_create_inet_udp2way_listener(int);
extern socketbuf    *socket_create_inet_tcp_wait(const char *,int,int);
extern socketbuf    *socket_create_inet_udp_wait(const char *,int,int);
extern socketbuf    *socket_create_inet_udp2way_wait(const char *,int,int);
extern socketbuf    *socket_create_unix(const char *);
extern socketbuf    *socket_create_unix_wait(const char *,int);
extern socketbuf    *socket_create_unix_listener(const char *);
extern socketbuf    *socket_create_interrupt(void);
extern int          socket_interrupt(socketbuf *);
extern int           socket_dead(socketbuf *);
extern void          socket_destroy(socketbuf *);
extern int           socket_get_port(socketbuf *);
extern void          socket_indata_drop(socketbuf *,int);
extern size_t        socket_indata_length(socketbuf *);
extern size_t        socket_outdata_length(socketbuf *);
extern time_t        socket_connecttime(socketbuf *);
extern char         *socket_indata_pull(socketbuf *,int);
extern const char   *socket_indata_view(socketbuf *);
extern socket_udp_data *socket_udp_indata_pull(socketbuf *);
extern socket_udp_data *socket_udp_indata_view(socketbuf *);
extern int           socket_just_connected(socketbuf *);
extern socketbuf    *socket_new(socketbuf *);
extern int           socket_process(socketbuf *,long int);
extern int           socket_process_sockets(socket_processlist *,long int);
extern void          socket_debug_off(socketbuf *);
extern void          socket_debug_on(socketbuf *);
extern void          socket_write(socketbuf *,const char *,size_t);
extern void          socket_write_reliable(socketbuf *,const char *,size_t);

extern int           socket_udp_data_free(socket_udp_data *);

extern int           socket_mode_set(socketbuf *sock,unsigned int mode);
extern int           socket_mode_unset(socketbuf *sock,unsigned int mode);
extern unsigned int  socket_mode_get(socketbuf *sock);

extern const char   *socket_host_get(socketbuf *sock);

extern void          socket_relocate_data(socketbuf *from,socketbuf *to);

#ifdef SOCK_SSL
extern void          socket_set_encrypted(socketbuf *);
extern void          socket_set_server_key(socketbuf *,const char *);
extern void          socket_set_server_cert(socketbuf *,const char *);
extern void          socket_set_client_ca(socketbuf *,const char *);
#endif

extern socket_processlist *socket_link(socket_processlist *,socketbuf *);
extern socket_processlist *socket_unlink(socket_processlist *,socketbuf *);

#endif
