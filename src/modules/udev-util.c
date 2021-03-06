/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libudev.h>

#include <pulse/xmalloc.h>
#include <pulse/proplist.h>

#include <pulsecore/log.h>
#include <pulsecore/core-util.h>

#include "udev-util.h"

static int read_id(struct udev_device *d, const char *n) {
    const char *v;
    unsigned u;

    pa_assert(d);
    pa_assert(n);

    if (!(v = udev_device_get_property_value(d, n)))
        return -1;

    if (pa_startswith(v, "0x"))
        v += 2;

    if (!*v)
        return -1;

    if (sscanf(v, "%04x", &u) != 1)
        return -1;

    if (u > 0xFFFFU)
        return -1;

    return u;
}

int pa_udev_get_info(pa_core *core, pa_proplist *p, int card_idx) {
    int r = -1;
    struct udev *udev;
    struct udev_device *card = NULL;
    char *t;
    const char *v;
    int id;

    pa_assert(core);
    pa_assert(p);
    pa_assert(card_idx >= 0);

    if (!(udev = udev_new())) {
        pa_log_error("Failed to allocate udev context.");
        goto finish;
    }

    t = pa_sprintf_malloc("%s/class/sound/card%i", udev_get_sys_path(udev), card_idx);
    card = udev_device_new_from_syspath(udev, t);
    pa_xfree(t);

    if (!card) {
        pa_log_error("Failed to get card object.");
        goto finish;
    }

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_BUS))
        if ((v = udev_device_get_property_value(card, "ID_BUS")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_BUS, v);

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_VENDOR_ID))
        if ((id = read_id(card, "ID_VENDOR_ID")) > 0)
            pa_proplist_setf(p, PA_PROP_DEVICE_VENDOR_ID, "%04x", id);

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_VENDOR_NAME)) {
        if ((v = udev_device_get_property_value(card, "ID_VENDOR_FROM_DATABASE")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_VENDOR_NAME, v);
        else if ((v = udev_device_get_property_value(card, "ID_VENDOR")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_VENDOR_NAME, v);
    }

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_PRODUCT_ID))
        if ((id = read_id(card, "ID_MODEL_ID")) >= 0)
            pa_proplist_setf(p, PA_PROP_DEVICE_PRODUCT_ID, "%04x", id);

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_PRODUCT_NAME)) {
        if ((v = udev_device_get_property_value(card, "ID_MODEL_FROM_DATABASE")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_PRODUCT_NAME, v);
        else if ((v = udev_device_get_property_value(card, "ID_MODEL")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_PRODUCT_NAME, v);
    }

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_SERIAL))
        if ((v = udev_device_get_property_value(card, "ID_SERIAL")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_SERIAL, v);

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_FORM_FACTOR))
        if ((v = udev_device_get_property_value(card, "SOUND_FORM_FACTOR")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_FORM_FACTOR, v);

    if (!pa_proplist_contains(p, PA_PROP_DEVICE_BUS_PATH))
        if ((v = udev_device_get_devpath(card)))
            pa_proplist_sets(p, PA_PROP_DEVICE_BUS_PATH, v);

    /* This is normaly not set by th udev rules but may be useful to
     * allow administrators to overwrite the device description.*/
    if (!pa_proplist_contains(p, PA_PROP_DEVICE_DESCRIPTION))
        if ((v = udev_device_get_property_value(card, "SOUND_DESCRIPTION")) && *v)
            pa_proplist_sets(p, PA_PROP_DEVICE_DESCRIPTION, v);

    r = 0;

finish:

    if (card)
        udev_device_unref(card);

    if (udev)
        udev_unref(udev);

    return r;
}
