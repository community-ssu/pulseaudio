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

#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include <polypcore/module.h>
#include <polypcore/iochannel.h>
#include <polypcore/modargs.h>
#include <polypcore/protocol-native.h>
#include <polypcore/log.h>

#include "module-native-protocol-fd-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering")
PA_MODULE_DESCRIPTION("Native protocol autospawn helper")
PA_MODULE_VERSION(PACKAGE_VERSION)

static const char* const valid_modargs[] = {
    "fd",
    "public",
    "cookie",
    NULL,
};

int pa__init(pa_core *c, pa_module*m) {
    pa_iochannel *io;
    pa_modargs *ma;
    int fd, r = -1;
    assert(c && m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log(__FILE__": failed to parse module arguments.\n");
        goto finish;
    }

    if (pa_modargs_get_value_s32(ma, "fd", &fd) < 0) {
        pa_log(__FILE__": invalid file descriptor.\n");
        goto finish;
    }
    
    io = pa_iochannel_new(c->mainloop, fd, fd);

    if (!(m->userdata = pa_protocol_native_new_iochannel(c, io, m, ma))) {
        pa_iochannel_free(io);
        goto finish;
    }

    r = 0;

finish:
    if (ma)
        pa_modargs_free(ma);
    
    return r;
}

void pa__done(pa_core *c, pa_module*m) {
    assert(c && m);

    pa_protocol_native_free(m->userdata);
}