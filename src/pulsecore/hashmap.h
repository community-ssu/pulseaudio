#ifndef foopulsecorehashmaphfoo
#define foopulsecorehashmaphfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <pulsecore/idxset.h>

/* Simple Implementation of a hash table. Memory management is the
 * user's job. It's a good idea to have the key pointer point to a
 * string in the value data. */

typedef struct pa_hashmap pa_hashmap;

/* Create a new hashmap. Use the specified functions for hashing and comparing objects in the map */
pa_hashmap *pa_hashmap_new(pa_hash_func_t hash_func, pa_compare_func_t compare_func);

/* Free the hash table. Calls the specified function for every value in the table. The function may be NULL */
void pa_hashmap_free(pa_hashmap*, pa_free2_cb_t free_cb, void *userdata);

/* Add an entry to the hashmap. Returns non-zero when the entry already exists */
int pa_hashmap_put(pa_hashmap *h, const void *key, void *value);

/* Return an entry from the hashmap */
void* pa_hashmap_get(pa_hashmap *h, const void *key);

/* Returns the data of the entry while removing */
void* pa_hashmap_remove(pa_hashmap *h, const void *key);

/* Return the current number of entries of the hashmap */
unsigned pa_hashmap_size(pa_hashmap *h);

/* Return TRUE if the hashmap is empty */
pa_bool_t pa_hashmap_isempty(pa_hashmap *h);

/* May be used to iterate through the hashmap. Initially the opaque
   pointer *state has to be set to NULL. The hashmap may not be
   modified during iteration -- except for deleting the current entry
   via pa_hashmap_remove(). The key of the entry is returned in *key,
   if key is non-NULL. After the last entry in the hashmap NULL is
   returned. */
void *pa_hashmap_iterate(pa_hashmap *h, void **state, const void**key);

/* Remove the oldest entry in the hashmap and return it */
void *pa_hashmap_steal_first(pa_hashmap *h);

/* Return the oldest entry in the hashmap */
void* pa_hashmap_first(pa_hashmap *h);

#endif
