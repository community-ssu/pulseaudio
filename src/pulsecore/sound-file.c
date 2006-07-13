/* $Id$ */

/***
  This file is part of PulseAudio.
 
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <assert.h>

#include <sndfile.h>

#include <pulse/sample.h>
#include <pulsecore/log.h>

#include "sound-file.h"
#include "core-scache.h"

int pa_sound_file_load(const char *fname, pa_sample_spec *ss, pa_channel_map *map, pa_memchunk *chunk, pa_memblock_stat *s) {
    SNDFILE*sf = NULL;
    SF_INFO sfinfo;
    int ret = -1;
    size_t l;
    sf_count_t (*readf_function)(SNDFILE *sndfile, void *ptr, sf_count_t frames) = NULL;
    assert(fname && ss && chunk);

    chunk->memblock = NULL;
    chunk->index = chunk->length = 0;

    memset(&sfinfo, 0, sizeof(sfinfo));

    if (!(sf = sf_open(fname, SFM_READ, &sfinfo))) {
        pa_log(__FILE__": Failed to open file %s", fname);
        goto finish;
    }

    switch (sfinfo.format & SF_FORMAT_SUBMASK) {
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_U8:
        case SF_FORMAT_PCM_S8:
            ss->format = PA_SAMPLE_S16NE;
            readf_function = (sf_count_t (*)(SNDFILE *sndfile, void *ptr, sf_count_t frames)) sf_readf_short;
            break;

        case SF_FORMAT_ULAW:
            ss->format = PA_SAMPLE_ULAW;
            break;
            
        case SF_FORMAT_ALAW:
            ss->format = PA_SAMPLE_ALAW;
            break;

        case SF_FORMAT_FLOAT:
        case SF_FORMAT_DOUBLE:
        default:
            ss->format = PA_SAMPLE_FLOAT32NE;
            readf_function = (sf_count_t (*)(SNDFILE *sndfile, void *ptr, sf_count_t frames)) sf_readf_float;
            break;
    }

    ss->rate = sfinfo.samplerate;
    ss->channels = sfinfo.channels;

    if (!pa_sample_spec_valid(ss)) {
        pa_log(__FILE__": Unsupported sample format in file %s", fname);
        goto finish;
    }

    if (map)
        pa_channel_map_init_auto(map, ss->channels, PA_CHANNEL_MAP_DEFAULT);
    
    if ((l = pa_frame_size(ss)*sfinfo.frames) > PA_SCACHE_ENTRY_SIZE_MAX) {
        pa_log(__FILE__": File too large");
        goto finish;
    }

    chunk->memblock = pa_memblock_new(l, s);
    assert(chunk->memblock);
    chunk->index = 0;
    chunk->length = l;

    if ((readf_function && readf_function(sf, chunk->memblock->data, sfinfo.frames) != sfinfo.frames) ||
        (!readf_function && sf_read_raw(sf, chunk->memblock->data, l) != l)) {
        pa_log(__FILE__": Premature file end");
        goto finish;
    }
        
    ret = 0;

finish:

    if (sf)
        sf_close(sf);

    if (ret != 0 && chunk->memblock)
        pa_memblock_unref(chunk->memblock);
    
    return ret;
    
}

int pa_sound_file_too_big_to_cache(const char *fname) {
    SNDFILE*sf = NULL;
    SF_INFO sfinfo;
    pa_sample_spec ss;

    if (!(sf = sf_open(fname, SFM_READ, &sfinfo))) {
        pa_log(__FILE__": Failed to open file %s", fname);
        return 0;
    }

    sf_close(sf);

    switch (sfinfo.format & SF_FORMAT_SUBMASK) {
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_U8:
        case SF_FORMAT_PCM_S8:
            ss.format = PA_SAMPLE_S16NE;
            break;

        case SF_FORMAT_ULAW:
            ss.format = PA_SAMPLE_ULAW;
            break;
            
        case SF_FORMAT_ALAW:
            ss.format = PA_SAMPLE_ALAW;
            break;

        case SF_FORMAT_DOUBLE:
        case SF_FORMAT_FLOAT:
        default:
            ss.format = PA_SAMPLE_FLOAT32NE;
            break;
    }

    ss.rate = sfinfo.samplerate;
    ss.channels = sfinfo.channels;

    if ((pa_frame_size(&ss) * sfinfo.frames) > PA_SCACHE_ENTRY_SIZE_MAX) {
        pa_log(__FILE__": File too large %s", fname);
        return 1;
    }

    return 0;
}