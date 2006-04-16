#ifndef foosdphfoo
#define foosdphfoo

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

#include <polyp/sample.h>

#define PA_SDP_HEADER "v=0\n"

typedef struct pa_sdp_info {
    char *origin;
    char *session_name;

    struct sockaddr_storage sa;
    socklen_t salen;

    pa_sample_spec sample_spec;
    uint8_t payload;
} pa_sdp_info;

char *pa_sdp_build(int af, const void *src, const void *dst, const char *name, uint16_t port, uint8_t payload, const pa_sample_spec *ss);

pa_sdp_info *pa_sdp_parse(const char *t, pa_sdp_info *info, int is_goodbye);

void pa_sdp_info_destroy(pa_sdp_info *i);

#endif