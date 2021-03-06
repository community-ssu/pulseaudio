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

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "semaphore.h"

struct pa_semaphore {
    sem_t sem;
};

pa_semaphore* pa_semaphore_new(unsigned value) {
    pa_semaphore *s;

    s = pa_xnew(pa_semaphore, 1);
    pa_assert_se(sem_init(&s->sem, 0, value) == 0);
    return s;
}

void pa_semaphore_free(pa_semaphore *s) {
    pa_assert(s);
    pa_assert_se(sem_destroy(&s->sem) == 0);
    pa_xfree(s);
}

void pa_semaphore_post(pa_semaphore *s) {
    pa_assert(s);
    pa_assert_se(sem_post(&s->sem) == 0);
}

void pa_semaphore_wait(pa_semaphore *s) {
    int ret;
    pa_assert(s);

    do {
        ret = sem_wait(&s->sem);
    } while (ret < 0 && errno == EINTR);

    pa_assert(ret == 0);
}
