/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/utf8.h>
#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/source-output.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/log.h>
#include <pulsecore/sample-util.h>

#include "source.h"

#define DEFAULT_MIN_LATENCY (4*PA_USEC_PER_MSEC)

static PA_DEFINE_CHECK_TYPE(pa_source, pa_msgobject);

static void source_free(pa_object *o);

pa_source_new_data* pa_source_new_data_init(pa_source_new_data *data) {
    pa_assert(data);

    memset(data, 0, sizeof(*data));
    data->proplist = pa_proplist_new();

    return data;
}

void pa_source_new_data_set_name(pa_source_new_data *data, const char *name) {
    pa_assert(data);

    pa_xfree(data->name);
    data->name = pa_xstrdup(name);
}

void pa_source_new_data_set_sample_spec(pa_source_new_data *data, const pa_sample_spec *spec) {
    pa_assert(data);

    if ((data->sample_spec_is_set = !!spec))
        data->sample_spec = *spec;
}

void pa_source_new_data_set_channel_map(pa_source_new_data *data, const pa_channel_map *map) {
    pa_assert(data);

    if ((data->channel_map_is_set = !!map))
        data->channel_map = *map;
}

void pa_source_new_data_set_volume(pa_source_new_data *data, const pa_cvolume *volume) {
    pa_assert(data);

    if ((data->volume_is_set = !!volume))
        data->volume = *volume;
}

void pa_source_new_data_set_muted(pa_source_new_data *data, pa_bool_t mute) {
    pa_assert(data);

    data->muted_is_set = TRUE;
    data->muted = !!mute;
}

void pa_source_new_data_done(pa_source_new_data *data) {
    pa_assert(data);

    pa_xfree(data->name);
    pa_proplist_free(data->proplist);
}

/* Called from main context */
static void reset_callbacks(pa_source *s) {
    pa_assert(s);

    s->set_state = NULL;
    s->get_volume = NULL;
    s->set_volume = NULL;
    s->get_mute = NULL;
    s->set_mute = NULL;
    s->update_requested_latency = NULL;
}

/* Called from main context */
pa_source* pa_source_new(
        pa_core *core,
        pa_source_new_data *data,
        pa_source_flags_t flags) {

    pa_source *s;
    const char *name;
    char st[PA_SAMPLE_SPEC_SNPRINT_MAX], cm[PA_CHANNEL_MAP_SNPRINT_MAX];

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->name);

    s = pa_msgobject_new(pa_source);

    if (!(name = pa_namereg_register(core, data->name, PA_NAMEREG_SOURCE, s, data->namereg_fail))) {
        pa_xfree(s);
        return NULL;
    }

    pa_source_new_data_set_name(data, name);

    if (pa_hook_fire(&core->hooks[PA_CORE_HOOK_SOURCE_NEW], data) < 0) {
        pa_xfree(s);
        pa_namereg_unregister(core, name);
        return NULL;
    }

    pa_return_null_if_fail(!data->driver || pa_utf8_valid(data->driver));
    pa_return_null_if_fail(data->name && pa_utf8_valid(data->name) && data->name[0]);

    pa_return_null_if_fail(data->sample_spec_is_set && pa_sample_spec_valid(&data->sample_spec));

    if (!data->channel_map_is_set)
        pa_return_null_if_fail(pa_channel_map_init_auto(&data->channel_map, data->sample_spec.channels, PA_CHANNEL_MAP_DEFAULT));

    pa_return_null_if_fail(pa_channel_map_valid(&data->channel_map));
    pa_return_null_if_fail(data->channel_map.channels == data->sample_spec.channels);

    if (!data->volume_is_set)
        pa_cvolume_reset(&data->volume, data->sample_spec.channels);

    pa_return_null_if_fail(pa_cvolume_valid(&data->volume));
    pa_return_null_if_fail(data->volume.channels == data->sample_spec.channels);

    if (!data->muted_is_set)
        data->muted = FALSE;

    if (pa_hook_fire(&core->hooks[PA_CORE_HOOK_SOURCE_FIXATE], data) < 0) {
        pa_xfree(s);
        pa_namereg_unregister(core, name);
        return NULL;
    }

    s->parent.parent.free = source_free;
    s->parent.process_msg = pa_source_process_msg;

    s->core = core;
    s->state = PA_SOURCE_INIT;
    s->flags = flags;
    s->name = pa_xstrdup(name);
    s->proplist = pa_proplist_copy(data->proplist);
    s->driver = pa_xstrdup(data->driver);
    s->module = data->module;

    s->sample_spec = data->sample_spec;
    s->channel_map = data->channel_map;

    s->outputs = pa_idxset_new(NULL, NULL);
    s->n_corked = 0;
    s->monitor_of = NULL;

    s->volume = data->volume;
    s->muted = data->muted;
    s->refresh_volume = s->refresh_muted = FALSE;

    reset_callbacks(s);
    s->userdata = NULL;

    s->asyncmsgq = NULL;
    s->rtpoll = NULL;

    pa_silence_memchunk_get(
            &core->silence_cache,
            core->mempool,
            &s->silence,
            &s->sample_spec,
            0);

    s->thread_info.outputs = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    pa_cvolume_reset(&s->thread_info.soft_volume, s->sample_spec.channels);
    s->thread_info.soft_muted = FALSE;
    s->thread_info.state = s->state;
    s->thread_info.max_rewind = 0;
    s->thread_info.requested_latency_valid = FALSE;
    s->thread_info.requested_latency = 0;
    s->thread_info.min_latency = DEFAULT_MIN_LATENCY;
    s->thread_info.max_latency = 0;

    pa_assert_se(pa_idxset_put(core->sources, s, &s->index) >= 0);

    pa_log_info("Created source %u \"%s\" with sample spec %s and channel map %s",
                s->index,
                s->name,
                pa_sample_spec_snprint(st, sizeof(st), &s->sample_spec),
                pa_channel_map_snprint(cm, sizeof(cm), &s->channel_map));

    return s;
}

/* Called from main context */
static int source_set_state(pa_source *s, pa_source_state_t state) {
    int ret;
    pa_bool_t suspend_change;
    pa_source_state_t original_state;

    pa_assert(s);

    if (s->state == state)
        return 0;

    original_state = s->state;

    suspend_change =
        (original_state == PA_SOURCE_SUSPENDED && PA_SOURCE_IS_OPENED(state)) ||
        (PA_SOURCE_IS_OPENED(original_state) && state == PA_SOURCE_SUSPENDED);

    if (s->set_state)
        if ((ret = s->set_state(s, state)) < 0)
            return ret;

    if (s->asyncmsgq)
        if ((ret = pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_SET_STATE, PA_UINT_TO_PTR(state), 0, NULL)) < 0) {

            if (s->set_state)
                s->set_state(s, original_state);

            return ret;
        }

    s->state = state;

    if (suspend_change) {
        pa_source_output *o;
        uint32_t idx;

        /* We're suspending or resuming, tell everyone about it */

        for (o = PA_SOURCE_OUTPUT(pa_idxset_first(s->outputs, &idx)); o; o = PA_SOURCE_OUTPUT(pa_idxset_next(s->outputs, &idx)))
            if (o->suspend)
                o->suspend(o, state == PA_SINK_SUSPENDED);
    }

    if (state != PA_SOURCE_UNLINKED) /* if we enter UNLINKED state pa_source_unlink() will fire the apropriate events */
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SOURCE_STATE_CHANGED], s);

    return 0;
}

/* Called from main context */
void pa_source_put(pa_source *s) {
    pa_source_assert_ref(s);

    pa_assert(s->state == PA_SINK_INIT);

    /* The following fields must be initialized properly when calling _put() */
    pa_assert(s->asyncmsgq);
    pa_assert(s->rtpoll);
    pa_assert(!s->thread_info.min_latency || !s->thread_info.max_latency ||
              s->thread_info.min_latency <= s->thread_info.max_latency);

    if (!(s->flags & PA_SOURCE_HW_VOLUME_CTRL)) {
        s->flags |= PA_SOURCE_DECIBEL_VOLUME;

        s->thread_info.soft_volume = s->volume;
        s->thread_info.soft_muted = s->muted;
    }

    pa_assert_se(source_set_state(s, PA_SOURCE_IDLE) == 0);

    pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_NEW, s->index);
    pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SOURCE_PUT], s);
}

/* Called from main context */
void pa_source_unlink(pa_source *s) {
    pa_bool_t linked;
    pa_source_output *o, *j = NULL;

    pa_assert(s);

    /* See pa_sink_unlink() for a couple of comments how this function
     * works. */

    linked = PA_SOURCE_IS_LINKED(s->state);

    if (linked)
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK], s);

    if (s->state != PA_SOURCE_UNLINKED)
        pa_namereg_unregister(s->core, s->name);
    pa_idxset_remove_by_data(s->core->sources, s, NULL);

    while ((o = pa_idxset_first(s->outputs, NULL))) {
        pa_assert(o != j);
        pa_source_output_kill(o);
        j = o;
    }

    if (linked)
        source_set_state(s, PA_SOURCE_UNLINKED);
    else
        s->state = PA_SOURCE_UNLINKED;

    reset_callbacks(s);

    if (linked) {
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE | PA_SUBSCRIPTION_EVENT_REMOVE, s->index);
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK_POST], s);
    }
}

/* Called from main context */
static void source_free(pa_object *o) {
    pa_source_output *so;
    pa_source *s = PA_SOURCE(o);

    pa_assert(s);
    pa_assert(pa_source_refcnt(s) == 0);

    if (PA_SOURCE_IS_LINKED(s->state))
        pa_source_unlink(s);

    pa_log_info("Freeing source %u \"%s\"", s->index, s->name);

    pa_idxset_free(s->outputs, NULL, NULL);

    while ((so = pa_hashmap_steal_first(s->thread_info.outputs)))
        pa_source_output_unref(so);

    pa_hashmap_free(s->thread_info.outputs, NULL, NULL);

    if (s->silence.memblock)
        pa_memblock_unref(s->silence.memblock);

    pa_xfree(s->name);
    pa_xfree(s->driver);

    if (s->proplist)
        pa_proplist_free(s->proplist);

    pa_xfree(s);
}

/* Called from main context */
void pa_source_set_asyncmsgq(pa_source *s, pa_asyncmsgq *q) {
    pa_source_assert_ref(s);

    s->asyncmsgq = q;
}

/* Called from main context */
void pa_source_set_rtpoll(pa_source *s, pa_rtpoll *p) {
    pa_source_assert_ref(s);

    s->rtpoll = p;
}

/* Called from main context */
int pa_source_update_status(pa_source*s) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    if (s->state == PA_SOURCE_SUSPENDED)
        return 0;

    return source_set_state(s, pa_source_used_by(s) ? PA_SOURCE_RUNNING : PA_SOURCE_IDLE);
}

/* Called from main context */
int pa_source_suspend(pa_source *s, pa_bool_t suspend) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    if (suspend)
        return source_set_state(s, PA_SOURCE_SUSPENDED);
    else
        return source_set_state(s, pa_source_used_by(s) ? PA_SOURCE_RUNNING : PA_SOURCE_IDLE);
}

/* Called from IO thread context */
void pa_source_process_rewind(pa_source *s, size_t nbytes) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_OPENED(s->thread_info.state));

    if (nbytes <= 0)
        return;

    pa_log_debug("Processing rewind...");

    while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL))) {
        pa_source_output_assert_ref(o);
        pa_source_output_process_rewind(o, nbytes);
    }
}

/* Called from IO thread context */
void pa_source_post(pa_source*s, const pa_memchunk *chunk) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_OPENED(s->thread_info.state));
    pa_assert(chunk);

    if (s->thread_info.state != PA_SOURCE_RUNNING)
        return;

    if (s->thread_info.soft_muted || !pa_cvolume_is_norm(&s->thread_info.soft_volume)) {
        pa_memchunk vchunk = *chunk;

        pa_memblock_ref(vchunk.memblock);
        pa_memchunk_make_writable(&vchunk, 0);

        if (s->thread_info.soft_muted || pa_cvolume_is_muted(&s->thread_info.soft_volume))
            pa_silence_memchunk(&vchunk, &s->sample_spec);
        else
            pa_volume_memchunk(&vchunk, &s->sample_spec, &s->thread_info.soft_volume);

        while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL))) {
            pa_source_output_assert_ref(o);

            if (!o->thread_info.direct_on_input)
                pa_source_output_push(o, &vchunk);
        }

        pa_memblock_unref(vchunk.memblock);
    } else {

        while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL))) {
            pa_source_output_assert_ref(o);

            if (!o->thread_info.direct_on_input)
                pa_source_output_push(o, chunk);
        }
    }
}

/* Called from IO thread context */
void pa_source_post_direct(pa_source*s, pa_source_output *o, const pa_memchunk *chunk) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_OPENED(s->thread_info.state));
    pa_source_output_assert_ref(o);
    pa_assert(o->thread_info.direct_on_input);
    pa_assert(chunk);

    if (s->thread_info.state != PA_SOURCE_RUNNING)
        return;

    if (s->thread_info.soft_muted || !pa_cvolume_is_norm(&s->thread_info.soft_volume)) {
        pa_memchunk vchunk = *chunk;

        pa_memblock_ref(vchunk.memblock);
        pa_memchunk_make_writable(&vchunk, 0);

        if (s->thread_info.soft_muted || pa_cvolume_is_muted(&s->thread_info.soft_volume))
            pa_silence_memchunk(&vchunk, &s->sample_spec);
        else
            pa_volume_memchunk(&vchunk, &s->sample_spec, &s->thread_info.soft_volume);

        pa_source_output_push(o, &vchunk);

        pa_memblock_unref(vchunk.memblock);
    } else
        pa_source_output_push(o, chunk);
}

/* Called from main thread */
pa_usec_t pa_source_get_latency(pa_source *s) {
    pa_usec_t usec;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    if (!PA_SOURCE_IS_OPENED(s->state))
        return 0;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_GET_LATENCY, &usec, 0, NULL) == 0);

    return usec;
}

/* Called from main thread */
void pa_source_set_volume(pa_source *s, const pa_cvolume *volume) {
    pa_bool_t changed;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));
    pa_assert(volume);

    changed = !pa_cvolume_equal(volume, &s->volume);
    s->volume = *volume;

    if (s->set_volume && s->set_volume(s) < 0)
        s->set_volume = NULL;

    if (!s->set_volume)
        pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_SET_VOLUME, volume, 0, NULL);

    if (changed)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

/* Called from main thread */
const pa_cvolume *pa_source_get_volume(pa_source *s) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    if (s->refresh_volume) {
        pa_cvolume old_volume = s->volume;

        if (s->get_volume && s->get_volume(s) < 0)
            s->get_volume = NULL;

        if (!s->get_volume)
            pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_GET_VOLUME, &s->volume, 0, NULL);

        if (!pa_cvolume_equal(&old_volume, &s->volume))
            pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
    }

    return &s->volume;
}

/* Called from main thread */
void pa_source_set_mute(pa_source *s, pa_bool_t mute) {
    pa_bool_t changed;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    changed = s->muted != mute;
    s->muted = mute;

    if (s->set_mute && s->set_mute(s) < 0)
        s->set_mute = NULL;

    if (!s->set_mute)
        pa_asyncmsgq_post(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_SET_MUTE, PA_UINT_TO_PTR(mute), 0, NULL, NULL);

    if (changed)
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
}

/* Called from main thread */
pa_bool_t pa_source_get_mute(pa_source *s) {

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    if (s->refresh_muted) {
        pa_bool_t old_muted = s->muted;

        if (s->get_mute && s->get_mute(s) < 0)
            s->get_mute = NULL;

        if (!s->get_mute)
            pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_GET_MUTE, &s->muted, 0, NULL);

        if (old_muted != s->muted)
            pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
    }

    return s->muted;
}

/* Called from main thread */
void pa_source_set_description(pa_source *s, const char *description) {
    const char *old;
    pa_source_assert_ref(s);

    if (!description && !pa_proplist_contains(s->proplist, PA_PROP_DEVICE_DESCRIPTION))
        return;

    old = pa_proplist_gets(s->proplist, PA_PROP_DEVICE_DESCRIPTION);

    if (old && description && !strcmp(old, description))
        return;

    if (description)
        pa_proplist_sets(s->proplist, PA_PROP_DEVICE_DESCRIPTION, description);
    else
        pa_proplist_unset(s->proplist, PA_PROP_DEVICE_DESCRIPTION);

    if (PA_SOURCE_IS_LINKED(s->state)) {
        pa_subscription_post(s->core, PA_SUBSCRIPTION_EVENT_SOURCE|PA_SUBSCRIPTION_EVENT_CHANGE, s->index);
        pa_hook_fire(&s->core->hooks[PA_CORE_HOOK_SOURCE_PROPLIST_CHANGED], s);
    }
}

/* Called from main thread */
unsigned pa_source_linked_by(pa_source *s) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    return pa_idxset_size(s->outputs);
}

/* Called from main thread */
unsigned pa_source_used_by(pa_source *s) {
    unsigned ret;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    ret = pa_idxset_size(s->outputs);
    pa_assert(ret >= s->n_corked);

    return ret - s->n_corked;
}

/* Called from IO thread, except when it is not */
int pa_source_process_msg(pa_msgobject *object, int code, void *userdata, int64_t offset, pa_memchunk *chunk) {
    pa_source *s = PA_SOURCE(object);
    pa_source_assert_ref(s);

    switch ((pa_source_message_t) code) {

        case PA_SOURCE_MESSAGE_ADD_OUTPUT: {
            pa_source_output *o = PA_SOURCE_OUTPUT(userdata);

            pa_hashmap_put(s->thread_info.outputs, PA_UINT32_TO_PTR(o->index), pa_source_output_ref(o));

            if (o->direct_on_input) {
                o->thread_info.direct_on_input = o->direct_on_input;
                pa_hashmap_put(o->thread_info.direct_on_input->thread_info.direct_outputs, PA_UINT32_TO_PTR(o->index), o);
            }

            pa_assert(!o->thread_info.attached);
            o->thread_info.attached = TRUE;

            if (o->attach)
                o->attach(o);

            pa_source_output_set_state_within_thread(o, o->state);

            pa_source_output_update_max_rewind(o, s->thread_info.max_rewind);

            /* We don't just invalidate the requested latency here,
             * because if we are in a move we might need to fix up the
             * requested latency. */
            pa_source_output_set_requested_latency_within_thread(o, o->thread_info.requested_source_latency);

            return 0;
        }

        case PA_SOURCE_MESSAGE_REMOVE_OUTPUT: {
            pa_source_output *o = PA_SOURCE_OUTPUT(userdata);

            pa_source_output_set_state_within_thread(o, o->state);

            if (o->detach)
                o->detach(o);

            pa_assert(o->thread_info.attached);
            o->thread_info.attached = FALSE;

            if (o->thread_info.direct_on_input) {
                pa_hashmap_remove(o->thread_info.direct_on_input->thread_info.direct_outputs, PA_UINT32_TO_PTR(o->index));
                o->thread_info.direct_on_input = NULL;
            }

            if (pa_hashmap_remove(s->thread_info.outputs, PA_UINT32_TO_PTR(o->index)))
                pa_source_output_unref(o);

            pa_source_invalidate_requested_latency(s);

            return 0;
        }

        case PA_SOURCE_MESSAGE_SET_VOLUME:
            s->thread_info.soft_volume = *((pa_cvolume*) userdata);
            return 0;

        case PA_SOURCE_MESSAGE_SET_MUTE:
            s->thread_info.soft_muted = PA_PTR_TO_UINT(userdata);
            return 0;

        case PA_SOURCE_MESSAGE_GET_VOLUME:
            *((pa_cvolume*) userdata) = s->thread_info.soft_volume;
            return 0;

        case PA_SOURCE_MESSAGE_GET_MUTE:
            *((pa_bool_t*) userdata) = s->thread_info.soft_muted;
            return 0;

        case PA_SOURCE_MESSAGE_SET_STATE:
            s->thread_info.state = PA_PTR_TO_UINT(userdata);
            return 0;

        case PA_SOURCE_MESSAGE_DETACH:

            /* Detach all streams */
            pa_source_detach_within_thread(s);
            return 0;

        case PA_SOURCE_MESSAGE_ATTACH:

            /* Reattach all streams */
            pa_source_attach_within_thread(s);
            return 0;

        case PA_SOURCE_MESSAGE_GET_REQUESTED_LATENCY: {

            pa_usec_t *usec = userdata;
            *usec = pa_source_get_requested_latency_within_thread(s);

            if (*usec == (pa_usec_t) -1)
                *usec = s->thread_info.max_latency;

            return 0;
        }

        case PA_SOURCE_MESSAGE_SET_LATENCY_RANGE: {
            pa_usec_t *r = userdata;

            pa_source_update_latency_range(s, r[0], r[1]);

            return 0;
        }

        case PA_SOURCE_MESSAGE_GET_LATENCY_RANGE: {
            pa_usec_t *r = userdata;

            r[0] = s->thread_info.min_latency;
            r[1] = s->thread_info.max_latency;

            return 0;
        }

        case PA_SOURCE_MESSAGE_GET_MAX_REWIND:

            *((size_t*) userdata) = s->thread_info.max_rewind;
            return 0;

        case PA_SOURCE_MESSAGE_GET_LATENCY:

            if (s->monitor_of) {
                *((pa_usec_t*) userdata) = 0;
                return 0;
            }

            /* Implementors need to overwrite this implementation! */
            return -1;

        case PA_SOURCE_MESSAGE_MAX:
            ;
    }

    return -1;
}

/* Called from main thread */
int pa_source_suspend_all(pa_core *c, pa_bool_t suspend) {
    uint32_t idx;
    pa_source *source;
    int ret = 0;

    pa_core_assert_ref(c);

    for (source = PA_SOURCE(pa_idxset_first(c->sources, &idx)); source; source = PA_SOURCE(pa_idxset_next(c->sources, &idx)))
        ret -= pa_source_suspend(source, suspend) < 0;

    return ret;
}

/* Called from main thread */
void pa_source_detach(pa_source *s) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_DETACH, NULL, 0, NULL) == 0);
}

/* Called from main thread */
void pa_source_attach(pa_source *s) {
    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_ATTACH, NULL, 0, NULL) == 0);
}

/* Called from IO thread */
void pa_source_detach_within_thread(pa_source *s) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->thread_info.state));

    while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL)))
        if (o->detach)
            o->detach(o);
}

/* Called from IO thread */
void pa_source_attach_within_thread(pa_source *s) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->thread_info.state));

    while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL)))
        if (o->attach)
            o->attach(o);
}

/* Called from IO thread */
pa_usec_t pa_source_get_requested_latency_within_thread(pa_source *s) {
    pa_usec_t result = (pa_usec_t) -1;
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);

    if (s->thread_info.requested_latency_valid)
        return s->thread_info.requested_latency;

    while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL)))

        if (o->thread_info.requested_source_latency != (pa_usec_t) -1 &&
            (result == (pa_usec_t) -1 || result > o->thread_info.requested_source_latency))
            result = o->thread_info.requested_source_latency;

    if (result != (pa_usec_t) -1) {
        if (s->thread_info.max_latency > 0 && result > s->thread_info.max_latency)
            result = s->thread_info.max_latency;

        if (s->thread_info.min_latency > 0 && result < s->thread_info.min_latency)
            result = s->thread_info.min_latency;
    }

    s->thread_info.requested_latency = result;
    s->thread_info.requested_latency_valid = TRUE;

    return result;
}

/* Called from main thread */
pa_usec_t pa_source_get_requested_latency(pa_source *s) {
    pa_usec_t usec;

    pa_source_assert_ref(s);
    pa_assert(PA_SOURCE_IS_LINKED(s->state));

    if (!PA_SOURCE_IS_OPENED(s->state))
        return 0;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_GET_REQUESTED_LATENCY, &usec, 0, NULL) == 0);

    return usec;
}

/* Called from IO thread */
void pa_source_set_max_rewind(pa_source *s, size_t max_rewind) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);

    if (max_rewind == s->thread_info.max_rewind)
        return;

    s->thread_info.max_rewind = max_rewind;

    if (PA_SOURCE_IS_LINKED(s->thread_info.state)) {
        while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL)))
            pa_source_output_update_max_rewind(o, s->thread_info.max_rewind);
    }
}

void pa_source_invalidate_requested_latency(pa_source *s) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);

    s->thread_info.requested_latency_valid = FALSE;

    if (s->update_requested_latency)
        s->update_requested_latency(s);

    while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL)))
        if (o->update_source_requested_latency)
            o->update_source_requested_latency(o);

    if (s->monitor_of)
        pa_sink_invalidate_requested_latency(s->monitor_of);
}

void pa_source_set_latency_range(pa_source *s, pa_usec_t min_latency, pa_usec_t max_latency) {
    pa_source_assert_ref(s);

    /* min_latency == 0:           no limit
     * min_latency == (size_t) -1: default limit
     * min_latency anything else:  specified limit
     *
     * Similar for max_latency */

    if (min_latency == (pa_usec_t) -1)
        min_latency = DEFAULT_MIN_LATENCY;

    if (max_latency == (pa_usec_t) -1)
        max_latency = min_latency;

    pa_assert(!min_latency || !max_latency ||
              min_latency <= max_latency);

    if (PA_SINK_IS_LINKED(s->state)) {
        pa_usec_t r[2];

        r[0] = min_latency;
        r[1] = max_latency;

        pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_SET_LATENCY_RANGE, r, 0, NULL) == 0);
    } else {
        s->thread_info.min_latency = min_latency;
        s->thread_info.max_latency = max_latency;

        s->thread_info.requested_latency_valid = FALSE;
    }
}

void pa_source_get_latency_range(pa_source *s, pa_usec_t *min_latency, pa_usec_t *max_latency) {
   pa_source_assert_ref(s);
   pa_assert(min_latency);
   pa_assert(max_latency);

   if (PA_SOURCE_IS_LINKED(s->state)) {
       pa_usec_t r[2] = { 0, 0 };

       pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_GET_LATENCY_RANGE, r, 0, NULL) == 0);

       *min_latency = r[0];
       *max_latency = r[1];
   } else {
       *min_latency = s->thread_info.min_latency;
       *max_latency = s->thread_info.max_latency;
   }
}

/* Called from IO thread */
void pa_source_update_latency_range(pa_source *s, pa_usec_t min_latency, pa_usec_t max_latency) {
    pa_source_output *o;
    void *state = NULL;

    pa_source_assert_ref(s);

    s->thread_info.min_latency = min_latency;
    s->thread_info.max_latency = max_latency;

    while ((o = pa_hashmap_iterate(s->thread_info.outputs, &state, NULL)))
        if (o->update_source_latency_range)
            o->update_source_latency_range(o);

    pa_source_invalidate_requested_latency(s);
}

size_t pa_source_get_max_rewind(pa_source *s) {
    size_t r;
    pa_source_assert_ref(s);

    if (!PA_SOURCE_IS_LINKED(s->state))
        return s->thread_info.max_rewind;

    pa_assert_se(pa_asyncmsgq_send(s->asyncmsgq, PA_MSGOBJECT(s), PA_SOURCE_MESSAGE_GET_MAX_REWIND, &r, 0, NULL) == 0);

    return r;
}
