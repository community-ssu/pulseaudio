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
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <polypcore/iochannel.h>
#include <polypcore/sink.h>
#include <polypcore/module.h>
#include <polypcore/util.h>
#include <polypcore/modargs.h>
#include <polypcore/xmalloc.h>
#include <polypcore/log.h>

#include "module-pipe-sink-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering")
PA_MODULE_DESCRIPTION("UNIX pipe sink")
PA_MODULE_VERSION(PACKAGE_VERSION)
PA_MODULE_USAGE("sink_name=<name for the sink> file=<path of the FIFO> format=<sample format> channels=<number of channels> rate=<sample rate>")

#define DEFAULT_FIFO_NAME "/tmp/music.output"
#define DEFAULT_SINK_NAME "fifo_output"

struct userdata {
    pa_core *core;

    char *filename;
    
    pa_sink *sink;
    pa_iochannel *io;
    pa_defer_event *defer_event;

    pa_memchunk memchunk;
    pa_module *module;
};

static const char* const valid_modargs[] = {
    "file",
    "rate",
    "format",
    "channels",
    "sink_name",
    NULL
};

static void do_write(struct userdata *u) {
    ssize_t r;
    assert(u);

    u->core->mainloop->defer_enable(u->defer_event, 0);
        
    if (!pa_iochannel_is_writable(u->io))
        return;

    pa_module_set_used(u->module, pa_idxset_size(u->sink->inputs) + pa_idxset_size(u->sink->monitor_source->outputs));
    
    if (!u->memchunk.length)
        if (pa_sink_render(u->sink, PIPE_BUF, &u->memchunk) < 0)
            return;

    assert(u->memchunk.memblock && u->memchunk.length);
    
    if ((r = pa_iochannel_write(u->io, (uint8_t*) u->memchunk.memblock->data + u->memchunk.index, u->memchunk.length)) < 0) {
        pa_log(__FILE__": write() failed: %s\n", strerror(errno));
        return;
    }

    u->memchunk.index += r;
    u->memchunk.length -= r;
        
    if (u->memchunk.length <= 0) {
        pa_memblock_unref(u->memchunk.memblock);
        u->memchunk.memblock = NULL;
    }
}

static void notify_cb(pa_sink*s) {
    struct userdata *u = s->userdata;
    assert(s && u);

    if (pa_iochannel_is_writable(u->io))
        u->core->mainloop->defer_enable(u->defer_event, 1);
}

static pa_usec_t get_latency_cb(pa_sink *s) {
    struct userdata *u = s->userdata;
    assert(s && u);

    return u->memchunk.memblock ? pa_bytes_to_usec(u->memchunk.length, &s->sample_spec) : 0;
}

static void defer_callback(PA_GCC_UNUSED pa_mainloop_api *m, PA_GCC_UNUSED pa_defer_event*e, void *userdata) {
    struct userdata *u = userdata;
    assert(u);
    do_write(u);
}

static void io_callback(PA_GCC_UNUSED pa_iochannel *io, void*userdata) {
    struct userdata *u = userdata;
    assert(u);
    do_write(u);
}

int pa__init(pa_core *c, pa_module*m) {
    struct userdata *u = NULL;
    struct stat st;
    const char *p;
    int fd = -1;
    pa_sample_spec ss;
    pa_modargs *ma = NULL;
    assert(c && m);
    
    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log(__FILE__": failed to parse module arguments\n");
        goto fail;
    }

    ss = c->default_sample_spec;
    if (pa_modargs_get_sample_spec(ma, &ss) < 0) {
        pa_log(__FILE__": invalid sample format specification\n");
        goto fail;
    }
    
    mkfifo(p = pa_modargs_get_value(ma, "file", DEFAULT_FIFO_NAME), 0777);

    if ((fd = open(p, O_RDWR)) < 0) {
        pa_log(__FILE__": open('%s'): %s\n", p, strerror(errno));
        goto fail;
    }

    pa_fd_set_cloexec(fd, 1);
    
    if (fstat(fd, &st) < 0) {
        pa_log(__FILE__": fstat('%s'): %s\n", p, strerror(errno));
        goto fail;
    }

    if (!S_ISFIFO(st.st_mode)) {
        pa_log(__FILE__": '%s' is not a FIFO.\n", p);
        goto fail;
    }

    u = pa_xmalloc0(sizeof(struct userdata));
    u->filename = pa_xstrdup(p);
    u->core = c;
    u->module = m;
    m->userdata = u;
    
    if (!(u->sink = pa_sink_new(c, __FILE__, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME), 0, &ss, NULL))) {
        pa_log(__FILE__": failed to create sink.\n");
        goto fail;
    }
    u->sink->notify = notify_cb;
    u->sink->get_latency = get_latency_cb;
    u->sink->userdata = u;
    pa_sink_set_owner(u->sink, m);
    u->sink->description = pa_sprintf_malloc("Unix FIFO sink '%s'", p);
    assert(u->sink->description);

    u->io = pa_iochannel_new(c->mainloop, -1, fd);
    assert(u->io);
    pa_iochannel_set_callback(u->io, io_callback, u);

    u->memchunk.memblock = NULL;
    u->memchunk.length = 0;

    u->defer_event = c->mainloop->defer_new(c->mainloop, defer_callback, u);
    assert(u->defer_event);
    c->mainloop->defer_enable(u->defer_event, 0);

    pa_modargs_free(ma);
    
    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);
        
    if (fd >= 0)
        close(fd);

    pa__done(c, m);

    return -1;
}

void pa__done(pa_core *c, pa_module*m) {
    struct userdata *u;
    assert(c && m);

    if (!(u = m->userdata))
        return;
    
    if (u->memchunk.memblock)
        pa_memblock_unref(u->memchunk.memblock);
        
    pa_sink_disconnect(u->sink);
    pa_sink_unref(u->sink);
    pa_iochannel_free(u->io);
    u->core->mainloop->defer_free(u->defer_event);

    assert(u->filename);
    unlink(u->filename);
    pa_xfree(u->filename);
    
    pa_xfree(u);
}