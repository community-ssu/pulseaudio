/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/poll.h>

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/core-error.h>
#include <pulsecore/source.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/rtclock.h>

#include "module-sine-source-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("Sine wave generator source");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        "rate=<sample rate> "
        "source_name=<name for the source> "
        "frequency=<frequency in Hz>");

#define DEFAULT_SOURCE_NAME "sine_input"
#define BLOCK_USEC (PA_USEC_PER_SEC * 2)

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_source *source;

    pa_thread *thread;
    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;

    pa_memchunk memchunk;
    size_t peek_index;

    pa_usec_t block_usec; /* how much to push at once */
    pa_usec_t timestamp;  /* when to push next */
};

static const char* const valid_modargs[] = {
    "rate",
    "source_name",
    "frequency",
    NULL
};

static int source_process_msg(
        pa_msgobject *o,
        int code,
        void *data,
        int64_t offset,
        pa_memchunk *chunk) {

    struct userdata *u = PA_SOURCE(o)->userdata;

    switch (code) {

        case PA_SOURCE_MESSAGE_SET_STATE:

            if (PA_PTR_TO_UINT(data) == PA_SOURCE_RUNNING)
                u->timestamp = pa_rtclock_usec();

            break;

        case PA_SOURCE_MESSAGE_GET_LATENCY: {
            pa_usec_t now, left_to_fill;

            now = pa_rtclock_usec();
            left_to_fill = u->timestamp > now ? u->timestamp - now : 0ULL;

            *((pa_usec_t*) data) = u->block_usec > left_to_fill ? u->block_usec - left_to_fill : 0ULL;

            return 0;
        }
    }

    return pa_source_process_msg(o, code, data, offset, chunk);
}

static void source_update_requested_latency_cb(pa_source *s) {
    struct userdata *u;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    u->block_usec = pa_source_get_requested_latency_within_thread(s);

    if (u->block_usec == (pa_usec_t) -1)
        u->block_usec = s->thread_info.max_latency;

    pa_log_debug("new block msec = %lu", (unsigned long) (u->block_usec / PA_USEC_PER_MSEC));
}

static void process_render(struct userdata *u, pa_usec_t now) {
    pa_assert(u);

    while (u->timestamp < now + u->block_usec) {
        pa_memchunk chunk;
        size_t k;

        k = pa_usec_to_bytes_round_up(now + u->block_usec - u->timestamp, &u->source->sample_spec);

        chunk = u->memchunk;
        chunk.index += u->peek_index;
        chunk.length = PA_MIN(chunk.length - u->peek_index, k);

/*         pa_log_debug("posting %lu", (unsigned long) chunk.length); */
        pa_source_post(u->source, &chunk);

        u->peek_index += chunk.length;
        while (u->peek_index >= u->memchunk.length)
            u->peek_index -= u->memchunk.length;

        u->timestamp += pa_bytes_to_usec(chunk.length, &u->source->sample_spec);
    }
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);

    pa_log_debug("Thread starting up");

    pa_thread_mq_install(&u->thread_mq);
    pa_rtpoll_install(u->rtpoll);

    u->timestamp = pa_rtclock_usec();

    for (;;) {
        int ret;

        if (PA_SOURCE_IS_OPENED(u->source->thread_info.state)) {
            pa_usec_t now;

            now = pa_rtclock_usec();

            if (u->timestamp <= now)
                process_render(u, now);

            pa_rtpoll_set_timer_absolute(u->rtpoll, u->timestamp);
        } else
            pa_rtpoll_set_timer_disabled(u->rtpoll);

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll, TRUE)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

int pa__init(pa_module*m) {
    struct userdata *u;
    pa_modargs *ma;
    pa_source_new_data data;
    uint32_t frequency;
    pa_sample_spec ss;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("failed to parse module arguments.");
        goto fail;
    }

    ss.format = PA_SAMPLE_FLOAT32;
    ss.channels = 1;
    ss.rate = 44100;

    if (pa_modargs_get_value_u32(ma, "rate", &ss.rate) < 0 || ss.rate <= 1) {
        pa_log("Invalid rate specification");
        goto fail;
    }

    frequency = 440;
    if (pa_modargs_get_value_u32(ma, "frequency", &frequency) < 0 || frequency < 1 || frequency > ss.rate/2) {
        pa_log("Invalid frequency specification");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    u->rtpoll = pa_rtpoll_new();
    pa_thread_mq_init(&u->thread_mq, m->core->mainloop, u->rtpoll);

    u->peek_index = 0;
    pa_memchunk_sine(&u->memchunk, m->core->mempool, ss.rate, frequency);

    pa_source_new_data_init(&data);
    data.driver = __FILE__;
    data.module = m;
    pa_source_new_data_set_name(&data, pa_modargs_get_value(ma, "source_name", DEFAULT_SOURCE_NAME));
    pa_proplist_setf(data.proplist, PA_PROP_DEVICE_DESCRIPTION, "Sine source at %u Hz", (unsigned) frequency);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_CLASS, "abstract");
    pa_proplist_setf(data.proplist, "sine.hz", "%u", frequency);
    pa_source_new_data_set_sample_spec(&data, &ss);

    u->source = pa_source_new(m->core, &data, PA_SOURCE_LATENCY);
    pa_source_new_data_done(&data);

    if (!u->source) {
        pa_log("Failed to create source.");
        goto fail;
    }

    u->source->parent.process_msg = source_process_msg;
    u->source->update_requested_latency = source_update_requested_latency_cb;
    u->source->userdata = u;

    u->block_usec = BLOCK_USEC;

    pa_source_set_asyncmsgq(u->source, u->thread_mq.inq);
    pa_source_set_rtpoll(u->source, u->rtpoll);
    u->source->fixed_latency = u->block_usec;


    if (!(u->thread = pa_thread_new(thread_func, u))) {
        pa_log("Failed to create thread.");
        goto fail;
    }

    pa_source_put(u->source);

    pa_modargs_free(ma);

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

int pa__get_n_used(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert_se(u = m->userdata);

    return pa_source_linked_by(u->source);
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->source)
        pa_source_unlink(u->source);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->source)
        pa_source_unref(u->source);

    if (u->memchunk.memblock)
        pa_memblock_unref(u->memchunk.memblock);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    pa_xfree(u);
}
