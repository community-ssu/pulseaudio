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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <signal.h>
#include <stropts.h>
#include <sys/conf.h>
#include <sys/audio.h>

#include <polyp/mainloop-signal.h>

#include <polypcore/iochannel.h>
#include <polypcore/sink.h>
#include <polypcore/source.h>
#include <polypcore/module.h>
#include <polypcore/sample-util.h>
#include <polypcore/util.h>
#include <polypcore/modargs.h>
#include <polypcore/xmalloc.h>
#include <polypcore/log.h>

#include "module-solaris-symdef.h"

PA_MODULE_AUTHOR("Pierre Ossman")
PA_MODULE_DESCRIPTION("Solaris Sink/Source")
PA_MODULE_VERSION(PACKAGE_VERSION)
PA_MODULE_USAGE("sink_name=<name for the sink> source_name=<name for the source> device=<OSS device> record=<enable source?> playback=<enable sink?> format=<sample format> channels=<number of channels> rate=<sample rate> buffer_size=<record buffer size>")

struct userdata {
    pa_sink *sink;
    pa_source *source;
    pa_iochannel *io;
    pa_core *core;
    pa_signal_event *sig;

    pa_memchunk memchunk, silence;

    uint32_t sample_size;
    uint32_t buffer_size;
    unsigned int written_bytes, read_bytes;

    int fd;
    pa_module *module;
};

static const char* const valid_modargs[] = {
    "sink_name",
    "source_name",
    "device",
    "record",
    "playback",
    "buffer_size",
    "format",
    "rate",
    "channels",
    NULL
};

#define DEFAULT_SINK_NAME "solaris_output"
#define DEFAULT_SOURCE_NAME "solaris_input"
#define DEFAULT_DEVICE "/dev/audio"

#define CHUNK_SIZE 2048

static void update_usage(struct userdata *u) {
   pa_module_set_used(u->module,
                      (u->sink ? pa_idxset_size(u->sink->inputs) : 0) +
                      (u->sink ? pa_idxset_size(u->sink->monitor_source->outputs) : 0) +
                      (u->source ? pa_idxset_size(u->source->outputs) : 0));
}

static void do_write(struct userdata *u) {
    audio_info_t info;
    int err;
    pa_memchunk *memchunk;
    size_t len;
    ssize_t r;
    
    assert(u);

    if (!u->sink || !pa_iochannel_is_writable(u->io))
        return;

    update_usage(u);

    err = ioctl(u->fd, AUDIO_GETINFO, &info);
    assert(err >= 0);

    /*
     * Since we cannot modify the size of the output buffer we fake it
     * by not filling it more than u->buffer_size.
     */
    len = u->buffer_size;
    len -= u->written_bytes - (info.play.samples * u->sample_size);

    /*
     * Do not fill more than half the buffer in one chunk since we only
     * get notifications upon completion of entire chunks.
     */
    if (len > (u->buffer_size / 2))
        len = u->buffer_size / 2;

    if (len < u->sample_size)
        return;

    memchunk = &u->memchunk;
    
    if (!memchunk->length)
        if (pa_sink_render(u->sink, len, memchunk) < 0)
            memchunk = &u->silence;
    
    assert(memchunk->memblock);
    assert(memchunk->memblock->data);
    assert(memchunk->length);

    if (memchunk->length < len)
        len = memchunk->length;
    
    if ((r = pa_iochannel_write(u->io, (uint8_t*) memchunk->memblock->data + memchunk->index, len)) < 0) {
        pa_log(__FILE__": write() failed: %s\n", strerror(errno));
        return;
    }
    
    if (memchunk == &u->silence)
        assert(r % u->sample_size == 0);
    else {
        u->memchunk.index += r;
        u->memchunk.length -= r;
        
        if (u->memchunk.length <= 0) {
            pa_memblock_unref(u->memchunk.memblock);
            u->memchunk.memblock = NULL;
        }
    }

    u->written_bytes += r;

    /*
     * Write 0 bytes which will generate a SIGPOLL when "played".
     */
    if (write(u->fd, NULL, 0) < 0) {
        pa_log(__FILE__": write() failed: %s\n", strerror(errno));
        return;
    }
}

static void do_read(struct userdata *u) {
    pa_memchunk memchunk;
    int err, l;
    ssize_t r;
    assert(u);
    
    if (!u->source || !pa_iochannel_is_readable(u->io))
        return;

    update_usage(u);

    err = ioctl(u->fd, I_NREAD, &l);
    assert(err >= 0);

    memchunk.memblock = pa_memblock_new(l, u->core->memblock_stat);
    assert(memchunk.memblock);
    if ((r = pa_iochannel_read(u->io, memchunk.memblock->data, memchunk.memblock->length)) < 0) {
        pa_memblock_unref(memchunk.memblock);
        if (errno != EAGAIN)
            pa_log(__FILE__": read() failed: %s\n", strerror(errno));
        return;
    }
    
    assert(r <= (ssize_t) memchunk.memblock->length);
    memchunk.length = memchunk.memblock->length = r;
    memchunk.index = 0;
    
    pa_source_post(u->source, &memchunk);
    pa_memblock_unref(memchunk.memblock);

    u->read_bytes += r;
}

static void io_callback(pa_iochannel *io, void*userdata) {
    struct userdata *u = userdata;
    assert(u);
    do_write(u);
    do_read(u);
}

void sig_callback(pa_mainloop_api *api, pa_signal_event*e, int sig, void *userdata) {
    struct userdata *u = userdata;
    assert(u);
    do_write(u);
}

static pa_usec_t sink_get_latency_cb(pa_sink *s) {
    pa_usec_t r = 0;
    audio_info_t info;
    int err;
    struct userdata *u = s->userdata;
    assert(s && u && u->sink);

    err = ioctl(u->fd, AUDIO_GETINFO, &info);
    assert(err >= 0);

    r += pa_bytes_to_usec(u->written_bytes, &s->sample_spec);
    r -= pa_bytes_to_usec(info.play.samples * u->sample_size, &s->sample_spec);

    if (u->memchunk.memblock)
        r += pa_bytes_to_usec(u->memchunk.length, &s->sample_spec);

    return r;
}

static pa_usec_t source_get_latency_cb(pa_source *s) {
    pa_usec_t r = 0;
    struct userdata *u = s->userdata;
    audio_info_t info;
    int err;
    assert(s && u && u->source);

    err = ioctl(u->fd, AUDIO_GETINFO, &info);
    assert(err >= 0);

    r += pa_bytes_to_usec(info.record.samples * u->sample_size, &s->sample_spec);
    r -= pa_bytes_to_usec(u->read_bytes, &s->sample_spec);

    return r;
}

static int pa_solaris_auto_format(int fd, int mode, pa_sample_spec *ss) {
    audio_info_t info;

    AUDIO_INITINFO(&info);

    if (mode != O_RDONLY) {
        info.play.sample_rate = ss->rate;
        info.play.channels = ss->channels;
        switch (ss->format) {
        case PA_SAMPLE_U8:
            info.play.precision = 8;
            info.play.encoding = AUDIO_ENCODING_LINEAR;
            break;
        case PA_SAMPLE_ALAW:
            info.play.precision = 8;
            info.play.encoding = AUDIO_ENCODING_ALAW;
            break;
        case PA_SAMPLE_ULAW:
            info.play.precision = 8;
            info.play.encoding = AUDIO_ENCODING_ULAW;
            break;
        case PA_SAMPLE_S16NE:
            info.play.precision = 16;
            info.play.encoding = AUDIO_ENCODING_LINEAR;
            break;
        default:
            return -1;
        }
    }

    if (mode != O_WRONLY) {
        info.record.sample_rate = ss->rate;
        info.record.channels = ss->channels;
        switch (ss->format) {
        case PA_SAMPLE_U8:
            info.record.precision = 8;
            info.record.encoding = AUDIO_ENCODING_LINEAR;
            break;
        case PA_SAMPLE_ALAW:
            info.record.precision = 8;
            info.record.encoding = AUDIO_ENCODING_ALAW;
            break;
        case PA_SAMPLE_ULAW:
            info.record.precision = 8;
            info.record.encoding = AUDIO_ENCODING_ULAW;
            break;
        case PA_SAMPLE_S16NE:
            info.record.precision = 16;
            info.record.encoding = AUDIO_ENCODING_LINEAR;
            break;
        default:
            return -1;
        }
    }

    if (ioctl(fd, AUDIO_SETINFO, &info) < 0) {
        if (errno == EINVAL)
            pa_log(__FILE__": AUDIO_SETINFO: Unsupported sample format.\n");
        else
            pa_log(__FILE__": AUDIO_SETINFO: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int pa_solaris_set_buffer(int fd, int buffer_size) {
    audio_info_t info;

    AUDIO_INITINFO(&info);

    info.record.buffer_size = buffer_size;

    if (ioctl(fd, AUDIO_SETINFO, &info) < 0) {
        if (errno == EINVAL)
            pa_log(__FILE__": AUDIO_SETINFO: Unsupported buffer size.\n");
        else
            pa_log(__FILE__": AUDIO_SETINFO: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int pa__init(pa_core *c, pa_module*m) {
    struct userdata *u = NULL;
    const char *p;
    int fd = -1;
    int buffer_size;
    int mode;
    int record = 1, playback = 1;
    pa_sample_spec ss;
    pa_modargs *ma = NULL;
    assert(c && m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log(__FILE__": failed to parse module arguments.\n");
        goto fail;
    }
    
    if (pa_modargs_get_value_boolean(ma, "record", &record) < 0 || pa_modargs_get_value_boolean(ma, "playback", &playback) < 0) {
        pa_log(__FILE__": record= and playback= expect numeric argument.\n");
        goto fail;
    }

    if (!playback && !record) {
        pa_log(__FILE__": neither playback nor record enabled for device.\n");
        goto fail;
    }

    mode = (playback&&record) ? O_RDWR : (playback ? O_WRONLY : (record ? O_RDONLY : 0));

    buffer_size = 16384;    
    if (pa_modargs_get_value_s32(ma, "buffer_size", &buffer_size) < 0) {
        pa_log(__FILE__": failed to parse buffer size argument\n");
        goto fail;
    }

    ss = c->default_sample_spec;
    if (pa_modargs_get_sample_spec(ma, &ss) < 0) {
        pa_log(__FILE__": failed to parse sample specification\n");
        goto fail;
    }
    
    if ((fd = open(p = pa_modargs_get_value(ma, "device", DEFAULT_DEVICE), mode | O_NONBLOCK)) < 0)
        goto fail;

    pa_log_info(__FILE__": device opened in %s mode.\n", mode == O_WRONLY ? "O_WRONLY" : (mode == O_RDONLY ? "O_RDONLY" : "O_RDWR"));

    if (pa_solaris_auto_format(fd, mode, &ss) < 0)
        goto fail;

    if ((mode != O_WRONLY) && (buffer_size >= 1))
        if (pa_solaris_set_buffer(fd, buffer_size) < 0)
            goto fail;

    u = pa_xmalloc(sizeof(struct userdata));
    u->core = c;

    if (mode != O_WRONLY) {
        u->source = pa_source_new(c, __FILE__, pa_modargs_get_value(ma, "source_name", DEFAULT_SOURCE_NAME), 0, &ss, NULL);
        assert(u->source);
        u->source->userdata = u;
        u->source->get_latency = source_get_latency_cb;
        pa_source_set_owner(u->source, m);
        u->source->description = pa_sprintf_malloc("Solaris PCM on '%s'", p);
    } else
        u->source = NULL;

    if (mode != O_RDONLY) {
        u->sink = pa_sink_new(c, __FILE__, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME), 0, &ss, NULL);
        assert(u->sink);
        u->sink->get_latency = sink_get_latency_cb;
        u->sink->userdata = u;
        pa_sink_set_owner(u->sink, m);
        u->sink->description = pa_sprintf_malloc("Solaris PCM on '%s'", p);
    } else
        u->sink = NULL;

    assert(u->source || u->sink);

    u->io = pa_iochannel_new(c->mainloop, u->source ? fd : -1, u->sink ? fd : 0);
    assert(u->io);
    pa_iochannel_set_callback(u->io, io_callback, u);
    u->fd = fd;

    u->memchunk.memblock = NULL;
    u->memchunk.length = 0;
    u->sample_size = pa_frame_size(&ss);
    u->buffer_size = buffer_size;

    u->silence.memblock = pa_memblock_new(u->silence.length = CHUNK_SIZE, u->core->memblock_stat);
    assert(u->silence.memblock);
    pa_silence_memblock(u->silence.memblock, &ss);
    u->silence.index = 0;

    u->written_bytes = 0;
    u->read_bytes = 0;

    u->module = m;
    m->userdata = u;

    u->sig = pa_signal_new(SIGPOLL, sig_callback, u);
    assert(u->sig);
    ioctl(u->fd, I_SETSIG, S_MSG);

    pa_modargs_free(ma);

    return 0;

fail:
    if (fd >= 0)
        close(fd);

    if (ma)
        pa_modargs_free(ma);
    
    return -1;
}

void pa__done(pa_core *c, pa_module*m) {
    struct userdata *u;
    assert(c && m);

    if (!(u = m->userdata))
        return;

    ioctl(u->fd, I_SETSIG, 0);
    pa_signal_free(u->sig);
    
    if (u->memchunk.memblock)
        pa_memblock_unref(u->memchunk.memblock);
    if (u->silence.memblock)
        pa_memblock_unref(u->silence.memblock);

    if (u->sink) {
        pa_sink_disconnect(u->sink);
        pa_sink_unref(u->sink);
    }
    
    if (u->source) {
        pa_source_disconnect(u->source);
        pa_source_unref(u->source);
    }
    
    pa_iochannel_free(u->io);
    pa_xfree(u);
}