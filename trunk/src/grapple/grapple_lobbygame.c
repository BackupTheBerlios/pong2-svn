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

#include <stdlib.h>
#include <string.h>

#include "grapple_lobby_internal.h"
#include "grapple_lobbygame.h"

//Create a basic internal game structure
grapple_lobbygame_internal *grapple_lobbygame_internal_create()
{
  grapple_lobbygame_internal *returnval;
  
  returnval=
    (grapple_lobbygame_internal *)calloc(1,sizeof(grapple_lobbygame_internal));
  
  return returnval;
}

//Delete a lobbygame_internal and all associated memory
int grapple_lobbygame_internal_dispose(grapple_lobbygame_internal *target)
{
  if (target->session)
    free(target->session);
  if (target->address)
    free(target->address);
  
  free(target);

  return 0;
}

//Link a lobbygame_internal into a linked list
grapple_lobbygame_internal *grapple_lobbygame_internal_link(grapple_lobbygame_internal *game,
							    grapple_lobbygame_internal *item)
{
  if (!game)
    {
      item->next=item;
      item->prev=item;
      return item;
    }

  item->next=game;
  item->prev=game->prev;

  item->next->prev=item;
  item->prev->next=item;

  return game;
}

//Remove a lobbygame_internal from a linked list
grapple_lobbygame_internal *grapple_lobbygame_internal_unlink(grapple_lobbygame_internal *game,
							      grapple_lobbygame_internal *item)
{
  if (game->next==game)
    return NULL;

  item->next->prev=item->prev;
  item->prev->next=item->next;

  if (item==game)
    game=item->next;

  return game;
}


//Locate a game by its ID
grapple_lobbygame_internal *grapple_lobbygame_internal_locate_by_id(grapple_lobbygame_internal *list,
								    grapple_user id)
{
  grapple_lobbygame_internal *scan;
  
  scan=list;

  while (scan)
    {
      if (scan->id==id)
	//Match
	return scan;

      scan=scan->next;
      if (scan==list)
	scan=NULL;
    }

  //No match, return NULL
  return NULL;
}
