#ifndef foomodinfohfoo
#define foomodinfohfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

/* Some functions for reading module meta data from PulseAudio modules */
#include <pulsecore/macro.h>

typedef struct pa_modinfo {
    char *author;
    char *description;
    char *usage;
    char *version;
    pa_bool_t load_once;
} pa_modinfo;

/* Read meta data from an libtool handle */
pa_modinfo *pa_modinfo_get_by_handle(lt_dlhandle dl, const char *module_name);

/* Read meta data from a module file */
pa_modinfo *pa_modinfo_get_by_name(const char *name);

/* Free meta data */
void pa_modinfo_free(pa_modinfo *i);

#endif
