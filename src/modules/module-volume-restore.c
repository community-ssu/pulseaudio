/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

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

#include <pulse/xmalloc.h>

#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/core-util.h>

#include "module-volume-restore-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("Compatibility module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

static const char* const valid_modargs[] = {
    "table",
    "restore_device",
    "restore_volume",
    NULL,
};

int pa__init(pa_module*m) {
    pa_modargs *ma = NULL;
    pa_bool_t restore_device = TRUE, restore_volume = TRUE;
    char *t;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    if (pa_modargs_get_value_boolean(ma, "restore_device", &restore_device) < 0 ||
        pa_modargs_get_value_boolean(ma, "restore_volume", &restore_volume) < 0) {
        pa_log("restore_volume= and restore_device= expect boolean arguments");
        goto fail;
    }

    pa_log_warn("module-volume-restore is obsolete. It has been replaced by module-stream-restore. We will now load the latter but please make sure to remove module-volume-restore from your configuration.");

    t = pa_sprintf_malloc("restore_volume=%s restore_device=%s", pa_yes_no(restore_volume), pa_yes_no(restore_device));
    pa_module_load(m->core, "module-stream-restore", t);
    pa_xfree(t);

    pa_module_unload_request(m, TRUE);

    pa_modargs_free(ma);
    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    return  -1;
}
