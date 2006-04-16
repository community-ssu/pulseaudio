#ifndef foortphfoo
#define foortphfoo

/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <polypcore/memblockq.h>
#include <polypcore/memchunk.h>

typedef struct pa_rtp_context {
    int fd;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
    uint8_t payload;
    size_t frame_size;
} pa_rtp_context;

pa_rtp_context* pa_rtp_context_init_send(pa_rtp_context *c, int fd, uint32_t ssrc, uint8_t payload, size_t frame_size);
int pa_rtp_send(pa_rtp_context *c, size_t size, pa_memblockq *q);

pa_rtp_context* pa_rtp_context_init_recv(pa_rtp_context *c, int fd, size_t frame_size);
int pa_rtp_recv(pa_rtp_context *c, pa_memchunk *chunk, pa_memblock_stat *st);

void pa_rtp_context_destroy(pa_rtp_context *c);

pa_sample_spec* pa_rtp_sample_spec_fixup(pa_sample_spec *ss);
int pa_rtp_sample_spec_valid(const pa_sample_spec *ss);

uint8_t pa_rtp_payload_from_sample_spec(const pa_sample_spec *ss);
pa_sample_spec *pa_rtp_sample_spec_from_payload(uint8_t payload, pa_sample_spec *ss);

const char* pa_rtp_format_to_string(pa_sample_format_t f);
pa_sample_format_t pa_rtp_string_to_format(const char *s);

#endif