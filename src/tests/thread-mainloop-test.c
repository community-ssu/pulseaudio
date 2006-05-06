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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <polypcore/gccmacro.h>
#include <polypcore/util.h>
#include <polyp/thread-mainloop.h>

static void tcb(pa_mainloop_api*a, pa_time_event *e, const struct timeval *tv, void *userdata) {
    fprintf(stderr, "TIME EVENT START\n");
    pa_threaded_mainloop_signal(userdata, 1);
    fprintf(stderr, "TIME EVENT END\n");
}

int main(PA_GCC_UNUSED int argc, PA_GCC_UNUSED char *argv[]) {
    pa_mainloop_api *a;
    pa_threaded_mainloop *m;
    struct timeval tv;

    m = pa_threaded_mainloop_new();
    assert(m);
    a = pa_threaded_mainloop_get_api(m);
    assert(a);

    pa_threaded_mainloop_start(m);

    pa_threaded_mainloop_lock(m);
    
    pa_gettimeofday(&tv);
    tv.tv_sec += 5;
    a->time_new(a, &tv, tcb, m);
    
    fprintf(stderr, "waiting 5s (signal)\n");
    pa_threaded_mainloop_wait(m);
    fprintf(stderr, "wait completed\n");

    pa_threaded_mainloop_unlock(m);
    
    fprintf(stderr, "waiting 5s (sleep)\n");
    sleep(5);

    fprintf(stderr, "shutting down\n");

    pa_threaded_mainloop_stop(m);

    pa_threaded_mainloop_free(m);
    return 0;
}