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

#ifndef DYNSTRING_H
#define DYNSTRING_H

typedef struct
{
  char *buf;
  int len;
  int maxlen;
} dynstring;

typedef struct
{
  unsigned char *buf;
  int len;
  int maxlen;
} udynstring;

typedef struct
{
  signed char *buf;
  int len;
  int maxlen;
} sdynstring;

extern void dynstringCheckAvailableLength(dynstring *,int);
extern dynstring *dynstringInit(int);
extern void dynstringAppend(dynstring *,const char *);
extern void dynstringUninit(dynstring *);
extern void dynstringRawappend(dynstring *,const char *,size_t);

extern void dynstringUCheckAvailableLength(udynstring *,int);
extern udynstring *dynstringUInit(int);
extern void dynstringUAppend(udynstring *,const unsigned char *);
extern void dynstringUUninit(udynstring *);
extern void dynstringURawappend(udynstring *,const unsigned char *,size_t);

extern void dynstringSCheckAvailableLength(sdynstring *,int);
extern sdynstring *dynstringSInit(int);
extern void dynstringSAppend(sdynstring *,const signed char *);
extern void dynstringSUninit(sdynstring *);
extern void dynstringSRawappend(sdynstring *,const signed char *,size_t);

#endif
