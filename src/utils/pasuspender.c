/* $Id$ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <sys/types.h>
#include <sys/wait.h>

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>

#include <sndfile.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <pulse/pulseaudio.h>
#include <pulsecore/macro.h>

#if PA_API_VERSION < 10
#error Invalid PulseAudio API version
#endif

#define BUFSIZE 1024

static pa_context *context = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static char **child_argv = NULL;
static int child_argc = 0;
static pid_t child_pid = (pid_t) -1;
static int child_ret = 0;
static int dead = 1;

static void quit(int ret) {
    pa_assert(mainloop_api);
    mainloop_api->quit(mainloop_api, ret);
}


static void context_drain_complete(pa_context *c, void *userdata) {
    pa_context_disconnect(c);
}

static void drain(void) {
    pa_operation *o;

    if (!(o = pa_context_drain(context, context_drain_complete, NULL)))
        pa_context_disconnect(context);
    else
        pa_operation_unref(o);
}

static void start_child(void) {

    if ((child_pid = fork()) < 0) {

        fprintf(stderr, "fork(): %s\n", strerror(errno));
        quit(1);

    } else if (child_pid == 0) {
        /* Child */

#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
#endif

        if (execvp(child_argv[0], child_argv) < 0)
            fprintf(stderr, "execvp(): %s\n", strerror(errno));

        _exit(1);

    } else {

        /* parent */
        dead = 0;
    }
}

static void suspend_complete(pa_context *c, int success, void *userdata) {
    static int n = 0;

    n++;

    if (!success) {
        fprintf(stderr, "Failure to suspend: %s\n", pa_strerror(pa_context_errno(c)));
        quit(1);
        return;
    }

    if (n >= 2)
        start_child();
}

static void resume_complete(pa_context *c, int success, void *userdata) {
    static int n = 0;

    n++;

    if (!success) {
        fprintf(stderr, "Failure to resume: %s\n", pa_strerror(pa_context_errno(c)));
        quit(1);
        return;
    }

    if (n >= 2)
        drain(); /* drain and quit */
}

static void context_state_callback(pa_context *c, void *userdata) {
    pa_assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

        case PA_CONTEXT_READY:
            if (pa_context_is_local(c)) {
                pa_operation_unref(pa_context_suspend_sink_by_index(c, PA_INVALID_INDEX, 1, suspend_complete, NULL));
                pa_operation_unref(pa_context_suspend_source_by_index(c, PA_INVALID_INDEX, 1, suspend_complete, NULL));
            } else {
                fprintf(stderr, "WARNING: Sound server is not local, not suspending.\n");
                start_child();
            }

            break;

        case PA_CONTEXT_TERMINATED:
            quit(0);
            break;

        case PA_CONTEXT_FAILED:
        default:
            fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));

            pa_context_unref(context);
            context = NULL;

            if (child_pid == (pid_t) -1)
                /* not started yet, then we do it now */
                start_child();
            else if (dead)
                /* already started, and dead, so let's quit */
                quit(1);

            break;
    }
}

static void sigint_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata) {
    fprintf(stderr, "Got SIGINT, exiting.\n");
    quit(0);
}

static void sigchld_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata) {
    int status = 0;
    pid_t p;

    p = waitpid(-1, &status, WNOHANG);

    if (p != child_pid)
        return;

    dead = 1;

    if (WIFEXITED(status))
        child_ret = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) {
        fprintf(stderr, "WARNING: Child process terminated by signal %u\n", WTERMSIG(status));
        child_ret = 1;
    }

    if (context) {
        if (pa_context_is_local(context)) {
            /* A context is around, so let's resume */
            pa_operation_unref(pa_context_suspend_sink_by_index(context, PA_INVALID_INDEX, 0, resume_complete, NULL));
            pa_operation_unref(pa_context_suspend_source_by_index(context, PA_INVALID_INDEX, 0, resume_complete, NULL));
        } else
            drain();
    } else
        /* Hmm, no context here, so let's terminate right away */
        quit(0);
}

static void help(const char *argv0) {

    printf("%s [options] ... \n\n"
           "  -h, --help                            Show this help\n"
           "      --version                         Show version\n"
           "  -s, --server=SERVER                   The name of the server to connect to\n\n",
           argv0);
}

enum {
    ARG_VERSION = 256
};

int main(int argc, char *argv[]) {
    pa_mainloop* m = NULL;
    int c, ret = 1;
    char *server = NULL, *bn;

    static const struct option long_options[] = {
        {"server",      1, NULL, 's'},
        {"version",     0, NULL, ARG_VERSION},
        {"help",        0, NULL, 'h'},
        {NULL,          0, NULL, 0}
    };

    if (!(bn = strrchr(argv[0], '/')))
        bn = argv[0];
    else
        bn++;

    while ((c = getopt_long(argc, argv, "s:h", long_options, NULL)) != -1) {
        switch (c) {
            case 'h' :
                help(bn);
                ret = 0;
                goto quit;

            case ARG_VERSION:
                printf("pasuspender "PACKAGE_VERSION"\nCompiled with libpulse %s\nLinked with libpulse %s\n", pa_get_headers_version(), pa_get_library_version());
                ret = 0;
                goto quit;

            case 's':
                pa_xfree(server);
                server = pa_xstrdup(optarg);
                break;

            default:
                goto quit;
        }
    }

    child_argv = argv + optind;
    child_argc = argc - optind;

    if (child_argc <= 0) {
        help(bn);
        ret = 0;
        goto quit;
    }

    if (!(m = pa_mainloop_new())) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        goto quit;
    }

    pa_assert_se(mainloop_api = pa_mainloop_get_api(m));
    pa_assert_se(pa_signal_init(mainloop_api) == 0);
    pa_signal_new(SIGINT, sigint_callback, NULL);
    pa_signal_new(SIGCHLD, sigchld_callback, NULL);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    if (!(context = pa_context_new(mainloop_api, bn))) {
        fprintf(stderr, "pa_context_new() failed.\n");
        goto quit;
    }

    pa_context_set_state_callback(context, context_state_callback, NULL);
    pa_context_connect(context, server, PA_CONTEXT_NOAUTOSPAWN, NULL);

    if (pa_mainloop_run(m, &ret) < 0) {
        fprintf(stderr, "pa_mainloop_run() failed.\n");
        goto quit;
    }

quit:
    if (context)
        pa_context_unref(context);

    if (m) {
        pa_signal_done();
        pa_mainloop_free(m);
    }

    pa_xfree(server);

    if (!dead)
        kill(child_pid, SIGTERM);

    return ret == 0 ? child_ret : ret;
}