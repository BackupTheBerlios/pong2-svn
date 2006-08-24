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
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

#include "tools.h"


//This is a replacement for usleep which is not in any way ANSI, it does
//the same job.
void microsleep(int usec)
{
  fd_set fds;
  struct timeval tv;
  
  tv.tv_sec=0;
  tv.tv_usec=usec;
  
  FD_ZERO(&fds);

  //Select on no file descriptors, which means it will just wait, until that
  //time is up. Thus sleeping for a microsecond exact time.
  select(FD_SETSIZE,&fds,0,0,&tv);
  
  return;
}
