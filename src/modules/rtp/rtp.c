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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <polypcore/log.h>

#include "rtp.h"

pa_rtp_context* pa_rtp_context_init_send(pa_rtp_context *c, int fd, uint32_t ssrc, uint8_t payload, size_t frame_size) {
    assert(c);
    assert(fd >= 0);

    c->fd = fd;
    c->sequence = (uint16_t) (rand()*rand());
    c->timestamp = 0;
    c->ssrc = ssrc ? ssrc : (uint32_t) (rand()*rand());
    c->payload = payload & 127;
    c->frame_size = frame_size;
    
    return c;
}

#define MAX_IOVECS 16

int pa_rtp_send(pa_rtp_context *c, size_t size, pa_memblockq *q) {
    struct iovec iov[MAX_IOVECS];
    pa_memblock* mb[MAX_IOVECS];
    int iov_idx = 1;
    size_t n = 0, skip = 0;
    
    assert(c);
    assert(size > 0);
    assert(q);

    if (pa_memblockq_get_length(q) < size)
        return 0;
    
    for (;;) {
        int r;
        pa_memchunk chunk;

        if ((r = pa_memblockq_peek(q, &chunk)) >= 0) {

            size_t k = n + chunk.length > size ? size - n : chunk.length;

            if (chunk.memblock) {
                iov[iov_idx].iov_base = (uint8_t*) chunk.memblock->data + chunk.index;
                iov[iov_idx].iov_len = k;
                mb[iov_idx] = chunk.memblock;
                iov_idx ++;

                n += k;
            }

            skip += k;
            pa_memblockq_drop(q, &chunk, k);
        }

        if (r < 0 || !chunk.memblock || n >= size || iov_idx >= MAX_IOVECS) {
            uint32_t header[3];
            struct msghdr m;
            int k, i;

            if (n > 0) {
                header[0] = htonl(((uint32_t) 2 << 30) | ((uint32_t) c->payload << 16) | ((uint32_t) c->sequence));
                header[1] = htonl(c->timestamp);
                header[2] = htonl(c->ssrc);

                iov[0].iov_base = header;
                iov[0].iov_len = sizeof(header);
                
                m.msg_name = NULL;
                m.msg_namelen = 0;
                m.msg_iov = iov;
                m.msg_iovlen = iov_idx;
                m.msg_control = NULL;
                m.msg_controllen = 0;
                m.msg_flags = 0;
                
                k = sendmsg(c->fd, &m, MSG_DONTWAIT);

                for (i = 1; i < iov_idx; i++)
                    pa_memblock_unref(mb[i]);

                c->sequence++;
            } else
                k = 0;

            c->timestamp += skip/c->frame_size;
            
            if (k < 0) {
                if (errno != EAGAIN) /* If the queue is full, just ignore it */
                    pa_log(__FILE__": sendmsg() failed: %s", strerror(errno));
                return -1;
            }
            
            if (r < 0 || pa_memblockq_get_length(q) < size)
                break;

            n = 0;
            skip = 0;
            iov_idx = 1;
        }
    }

    return 0;
}

pa_rtp_context* pa_rtp_context_init_recv(pa_rtp_context *c, int fd, size_t frame_size) {
    assert(c);

    c->fd = fd;
    c->frame_size = frame_size;
    return c;
}

int pa_rtp_recv(pa_rtp_context *c, pa_memchunk *chunk, pa_memblock_stat *st) {
    int size;
    struct msghdr m;
    struct iovec iov;
    uint32_t header;
    int cc;
    ssize_t r;
    
    assert(c);
    assert(chunk);

    chunk->memblock = NULL;

    if (ioctl(c->fd, FIONREAD, &size) < 0) {
        pa_log(__FILE__": FIONREAD failed: %s", strerror(errno));
        goto fail;
    }

    if (!size)
        return 0;

    chunk->memblock = pa_memblock_new(size, st);

    iov.iov_base = chunk->memblock->data;
    iov.iov_len = size;

    m.msg_name = NULL;
    m.msg_namelen = 0;
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = NULL;
    m.msg_controllen = 0;
    m.msg_flags = 0;
    
    if ((r = recvmsg(c->fd, &m, 0)) != size) {
        pa_log(__FILE__": recvmsg() failed: %s", r < 0 ? strerror(errno) : "size mismatch");
        goto fail;
    }

    if (size < 12) {
        pa_log(__FILE__": RTP packet too short.");
        goto fail;
    }

    memcpy(&header, chunk->memblock->data, sizeof(uint32_t));
    memcpy(&c->timestamp, (uint8_t*) chunk->memblock->data + 4, sizeof(uint32_t));
    memcpy(&c->ssrc, (uint8_t*) chunk->memblock->data + 8, sizeof(uint32_t));
    
    header = ntohl(header);
    c->timestamp = ntohl(c->timestamp);
    c->ssrc = ntohl(c->ssrc);

    if ((header >> 30) != 2) {
        pa_log(__FILE__": Unsupported RTP version.");
        goto fail;
    }

    if ((header >> 29) & 1) {
        pa_log(__FILE__": RTP padding not supported.");
        goto fail;
    }

    if ((header >> 28) & 1) {
        pa_log(__FILE__": RTP header extensions not supported.");
        goto fail;
    }

    cc = (header >> 24) & 0xF;
    c->payload = (header >> 16) & 127;
    c->sequence = header & 0xFFFF;

    if (12 + cc*4 > size) {
        pa_log(__FILE__": RTP packet too short. (CSRC)");
        goto fail;
    }

    chunk->index = 12 + cc*4;
    chunk->length = size - chunk->index;

    if (chunk->length % c->frame_size != 0) {
        pa_log(__FILE__": Vad RTP packet size.");
        goto fail;
    }
    
    return 0;

fail:
    if (chunk->memblock)
        pa_memblock_unref(chunk->memblock);

    return -1;
}

uint8_t pa_rtp_payload_from_sample_spec(const pa_sample_spec *ss) {
    assert(ss);

    if (ss->format == PA_SAMPLE_ULAW && ss->rate == 8000 && ss->channels == 1)
        return 0;
    if (ss->format == PA_SAMPLE_ALAW && ss->rate == 8000 && ss->channels == 1)
        return 8;
    if (ss->format == PA_SAMPLE_S16BE && ss->rate == 44100 && ss->channels == 2)
        return 10;
    if (ss->format == PA_SAMPLE_S16BE && ss->rate == 44100 && ss->channels == 1)
        return 11;
    
    return 127;
}

pa_sample_spec *pa_rtp_sample_spec_from_payload(uint8_t payload, pa_sample_spec *ss) {
    assert(ss);

    switch (payload) {
        case 0:
            ss->channels = 1;
            ss->format = PA_SAMPLE_ULAW;
            ss->rate = 8000;
            break;

        case 8:
            ss->channels = 1;
            ss->format = PA_SAMPLE_ALAW;
            ss->rate = 8000;
            break;

        case 10:
            ss->channels = 2;
            ss->format = PA_SAMPLE_S16BE;
            ss->rate = 44100;
            break;
            
        case 11:
            ss->channels = 1;
            ss->format = PA_SAMPLE_S16BE;
            ss->rate = 44100;
            break;

        default:
            return NULL;
    }

    return ss;
}

pa_sample_spec *pa_rtp_sample_spec_fixup(pa_sample_spec * ss) {
    assert(ss);

    if (!pa_rtp_sample_spec_valid(ss))
        ss->format = PA_SAMPLE_S16BE;

    assert(pa_rtp_sample_spec_valid(ss));
    return ss;
}

int pa_rtp_sample_spec_valid(const pa_sample_spec *ss) {
    assert(ss);

    if (!pa_sample_spec_valid(ss))
        return 0;

    return
        ss->format == PA_SAMPLE_U8 ||
        ss->format == PA_SAMPLE_ALAW ||
        ss->format == PA_SAMPLE_ULAW ||
        ss->format == PA_SAMPLE_S16BE;
}

void pa_rtp_context_destroy(pa_rtp_context *c) {
    assert(c);

    close(c->fd);
}

const char* pa_rtp_format_to_string(pa_sample_format_t f) {
    switch (f) {
        case PA_SAMPLE_S16BE:
            return "L16";
        case PA_SAMPLE_U8:
            return "L8";
        case PA_SAMPLE_ALAW:
            return "PCMA";
        case PA_SAMPLE_ULAW:
            return "PCMU";
        default:
            return NULL;
    }
}

pa_sample_format_t pa_rtp_string_to_format(const char *s) {
    assert(s);
    
    if (!(strcmp(s, "L16")))
        return PA_SAMPLE_S16BE;
    else if (!strcmp(s, "L8"))
        return PA_SAMPLE_U8;
    else if (!strcmp(s, "PCMA"))
        return PA_SAMPLE_ALAW;
    else if (!strcmp(s, "PCMU"))
        return PA_SAMPLE_ULAW;
    else
        return PA_SAMPLE_INVALID;
}
