#ifndef foollistfoo
#define foollistfoo

/* $Id$ */

/***
  This file is part of PulseAudio.
 
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.
 
  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

/* Some macros for maintaining doubly linked lists */

/* The head of the linked list. Use this in the structure that shall
 * contain the head of the linked list */
#define PA_LLIST_HEAD(t,name) t *name

/* The pointers in the linked list's items. Use this in the item structure */
#define PA_LLIST_FIELDS(t) t *next, *prev

/* Initialize the list's head */
#define PA_LLIST_HEAD_INIT(t,item) do { (item) = NULL; } while(0)

/* Initialize a list item */
#define PA_LLIST_INIT(t,item) do { \
                               t *_item = (item); \
                               assert(_item); \
                               _item->prev = _item->next = NULL; \
                               } while(0)

/* Prepend an item to the list */
#define PA_LLIST_PREPEND(t,head,item) do { \
                                        t **_head = &(head), *_item = (item); \
                                        assert(_item); \
                                        if ((_item->next = *_head)) \
                                           _item->next->prev = _item; \
                                        _item->prev = NULL; \
                                        *_head = _item; \
                                        } while (0)

/* Remove an item from the list */
#define PA_LLIST_REMOVE(t,head,item) do { \
                                    t **_head = &(head), *_item = (item); \
                                    assert(_item); \
                                    if (_item->next) \
                                       _item->next->prev = _item->prev; \
                                    if (_item->prev) \
                                       _item->prev->next = _item->next; \
                                    else {\
                                       assert(*_head == _item); \
                                       *_head = _item->next; \
                                    } \
                                    _item->next = _item->prev = NULL; \
                                    } while(0)

#define PA_LLIST_FIND_HEAD(t,item,head) \
do { \
    t **_head = (head), *_item = (item); \
    *_head = _item; \
    assert(_head); \
    while ((*_head)->prev) \
        *_head = (*_head)->prev; \
} while (0) \


#endif