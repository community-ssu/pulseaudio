#ifndef soundfilehfoo
#define soundfilehfoo

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

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulsecore/memchunk.h>

int pa_sound_file_load(pa_mempool *pool, const char *fname, pa_sample_spec *ss, pa_channel_map *map, pa_memchunk *chunk);

int pa_sound_file_too_big_to_cache(const char *fname);

#endif
