/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2 of the
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

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>

#include <pulse/timeval.h>
#include <pulse/gccmacro.h>

#include <pulsecore/log.h>
#include <pulsecore/macro.h>

static int msec_lower, msec_upper;

static void* work(void *p) PA_GCC_NORETURN;

static void* work(void *p) {
    cpu_set_t mask;
    struct sched_param param;

    pa_log_notice("CPU%i: Created thread.", PA_PTR_TO_INT(p));

    memset(&param, 0, sizeof(param));
    param.sched_priority = 12;
    pa_assert_se(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0);

    CPU_ZERO(&mask);
    CPU_SET(PA_PTR_TO_INT(p), &mask);
    pa_assert_se(pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == 0);

    for (;;) {
        struct timespec now, end;
        uint64_t nsec;

        pa_log_notice("CPU%i: Sleeping for 1s", PA_PTR_TO_INT(p));
        sleep(1);

        pa_assert_se(clock_gettime(CLOCK_REALTIME, &end) == 0);

        nsec =
            (uint64_t) ((((double) rand())*(msec_upper-msec_lower)*PA_NSEC_PER_MSEC)/RAND_MAX) +
            (uint64_t) (msec_lower*PA_NSEC_PER_MSEC);

        pa_log_notice("CPU%i: Freezing for %ims", PA_PTR_TO_INT(p), (int) (nsec/PA_NSEC_PER_MSEC));

        end.tv_sec += nsec / PA_NSEC_PER_SEC;
        end.tv_nsec += nsec % PA_NSEC_PER_SEC;

        while ((pa_usec_t) end.tv_nsec > PA_NSEC_PER_SEC) {
            end.tv_sec++;
            end.tv_nsec -= PA_NSEC_PER_SEC;
        }

        do {
            pa_assert_se(clock_gettime(CLOCK_REALTIME, &now) == 0);
        } while (now.tv_sec < end.tv_sec ||
                 (now.tv_sec == end.tv_sec && now.tv_nsec < end.tv_nsec));
    }
}

int main(int argc, char*argv[]) {
    int n;

    srand(time(NULL));

    if (argc >= 3) {
        msec_lower = atoi(argv[1]);
        msec_upper = atoi(argv[2]);
    } else if (argc >= 2) {
        msec_lower = 0;
        msec_upper = atoi(argv[1]);
    } else {
        msec_lower = 0;
        msec_upper = 1000;
    }

    pa_assert(msec_upper > 0);
    pa_assert(msec_upper >= msec_lower);

    pa_log_notice("Creating random latencies in the range of %ims to  %ims.", msec_lower, msec_upper);

    for (n = 1; n < sysconf(_SC_NPROCESSORS_CONF); n++) {
        pthread_t t;
        pa_assert_se(pthread_create(&t, NULL, work, PA_INT_TO_PTR(n)) == 0);
    }

    work(PA_INT_TO_PTR(0));

    return 0;
}
