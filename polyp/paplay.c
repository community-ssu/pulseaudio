/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <sndfile.h>

#include <polyp/polyplib.h>
#include <polyp/polyplib-error.h>
#include <polyp/mainloop.h>
#include <polyp/mainloop-signal.h>
#include <polyp/polyplib-version.h>

#if PA_API_VERSION != PA_API_VERSION_0_6
#error Invalid Polypaudio API version
#endif

static struct pa_context *context = NULL;
static struct pa_stream *stream = NULL;
static struct pa_mainloop_api *mainloop_api = NULL;

static char *stream_name = NULL, *client_name = NULL, *device = NULL;

static int verbose = 0;
static pa_volume_t volume = PA_VOLUME_NORM;

static SNDFILE* sndfile = NULL;

static struct pa_sample_spec sample_spec = { 0, 0, 0 }; 

static sf_count_t (*readf_function)(SNDFILE *sndfile, void *ptr, sf_count_t frames);

/* A shortcut for terminating the application */
static void quit(int ret) {
    assert(mainloop_api);
    mainloop_api->quit(mainloop_api, ret);
}

/* Connection draining complete */
static void context_drain_complete(struct pa_context*c, void *userdata) {
    pa_context_disconnect(c);
}

/* Stream draining complete */
static void stream_drain_complete(struct pa_stream*s, int success, void *userdata) {
    struct pa_operation *o;

    if (!success) {
        fprintf(stderr, "Failed to drain stream: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
    }
    
    if (verbose)    
        fprintf(stderr, "Playback stream drained.\n");

    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream = NULL;
    
    if (!(o = pa_context_drain(context, context_drain_complete, NULL)))
        pa_context_disconnect(context);
    else {
        pa_operation_unref(o);

        if (verbose)
            fprintf(stderr, "Draining connection to server.\n");
    }
}

/* This is called whenever new data may be written to the stream */
static void stream_write_callback(struct pa_stream *s, size_t length, void *userdata) {
    size_t k;
    sf_count_t f, n;
    void *data;
    assert(s && length);

    if (!sndfile)
        return;
    
    k = pa_frame_size(&sample_spec);

    data = malloc(length);

    n = length/k;
    
    f = readf_function(sndfile, data, n);

    if (f > 0)
        pa_stream_write(s, data, f*k, free, 0);

    if (f < n) {
        sf_close(sndfile);
        sndfile = NULL;
        pa_operation_unref(pa_stream_drain(s, stream_drain_complete, NULL));
    }
}

/* This routine is called whenever the stream state changes */
static void stream_state_callback(struct pa_stream *s, void *userdata) {
    assert(s);

    switch (pa_stream_get_state(s)) {
        case PA_STREAM_CREATING:
        case PA_STREAM_TERMINATED:
            break;

        case PA_STREAM_READY:
            if (verbose)
                fprintf(stderr, "Stream successfully created\n");
            break;
            
        case PA_STREAM_FAILED:
        default:
            fprintf(stderr, "Stream errror: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
            quit(1);
    }
}

/* This is called whenever the context status changes */
static void context_state_callback(struct pa_context *c, void *userdata) {
    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
        
        case PA_CONTEXT_READY:
            
            assert(c && !stream);

            if (verbose)
                fprintf(stderr, "Connection established.\n");

            stream = pa_stream_new(c, stream_name, &sample_spec);
            assert(stream);

            pa_stream_set_state_callback(stream, stream_state_callback, NULL);
            pa_stream_set_write_callback(stream, stream_write_callback, NULL);
            pa_stream_connect_playback(stream, device, NULL, 0, volume);
                
            break;
            
        case PA_CONTEXT_TERMINATED:
            quit(0);
            break;

        case PA_CONTEXT_FAILED:
        default:
            fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
            quit(1);
    }
}

/* UNIX signal to quit recieved */
static void exit_signal_callback(struct pa_mainloop_api*m, struct pa_signal_event *e, int sig, void *userdata) {
    if (verbose)
        fprintf(stderr, "Got SIGINT, exiting.\n");
    quit(0);
    
}

static void help(const char *argv0) {

    printf("%s [options] [FILE]\n\n"
           "  -h, --help                            Show this help\n"
           "      --version                         Show version\n\n"
           "  -v, --verbose                         Enable verbose operations\n\n"
           "  -s, --server=SERVER                   The name of the server to connect to\n"
           "  -d, --device=DEVICE                   The name of the sink/source to connect to\n"
           "  -n, --client-name=NAME                How to call this client on the server\n"
           "      --stream-name=NAME                How to call this stream on the server\n"
           "      --volume=VOLUME                   Specify the initial (linear) volume in range 0...256\n",
           argv0);
}

enum {
    ARG_VERSION = 256,
    ARG_STREAM_NAME,
    ARG_VOLUME
};

int main(int argc, char *argv[]) {
    struct pa_mainloop* m = NULL;
    int ret = 1, r, c;
    char *bn, *server = NULL, *filename;
    SF_INFO sfinfo;

    static const struct option long_options[] = {
        {"device",      1, NULL, 'd'},
        {"server",      1, NULL, 's'},
        {"client-name", 1, NULL, 'n'},
        {"stream-name", 1, NULL, ARG_STREAM_NAME},
        {"version",     0, NULL, ARG_VERSION},
        {"help",        0, NULL, 'h'},
        {"verbose",     0, NULL, 'v'},
        {"volume",      1, NULL, ARG_VOLUME},
        {NULL,          0, NULL, 0}
    };

    if (!(bn = strrchr(argv[0], '/')))
        bn = argv[0];
    else
        bn++;

    while ((c = getopt_long(argc, argv, "d:s:n:hv", long_options, NULL)) != -1) {

        switch (c) {
            case 'h' :
                help(bn);
                ret = 0;
                goto quit;
                
            case ARG_VERSION:
                printf("paplay "PACKAGE_VERSION"\nCompiled with libpolyp %s\nLinked with libpolyp %s\n", pa_get_headers_version(), pa_get_library_version());
                ret = 0;
                goto quit;

            case 'd':
                free(device);
                device = strdup(optarg);
                break;

            case 's':
                free(server);
                server = strdup(optarg);
                break;

            case 'n':
                free(client_name);
                client_name = strdup(optarg);
                break;

            case ARG_STREAM_NAME:
                free(stream_name);
                stream_name = strdup(optarg);
                break;

            case 'v':
                verbose = 1;
                break;

            case ARG_VOLUME: {
                int v = atoi(optarg);
                volume = v < 0 ? 0 : v;
                break;
            }

            default:
                goto quit;
        }
    }


    filename = optind < argc ? argv[optind] : "STDIN";
    
    
    if (!client_name)
        client_name = strdup(bn);

    if (!stream_name)
        stream_name = strdup(filename);

    memset(&sfinfo, 0, sizeof(sfinfo));

    if (optind < argc)
        sndfile = sf_open(filename, SFM_READ, &sfinfo);
    else
        sndfile = sf_open_fd(STDIN_FILENO, SFM_READ, &sfinfo, 0);

    if (!sndfile) {
        fprintf(stderr, "Failed to open file '%s'\n", filename);
        goto quit;
    }
              
    sample_spec.rate = sfinfo.samplerate;
    sample_spec.channels = sfinfo.channels;
    
    switch (sfinfo.format & 0xFF) {
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_U8:
        case SF_FORMAT_ULAW:
        case SF_FORMAT_ALAW:
            sample_spec.format = PA_SAMPLE_S16NE;
            readf_function = (sf_count_t (*)(SNDFILE *sndfile, void *ptr, sf_count_t frames)) sf_readf_short;
            break;
        case SF_FORMAT_FLOAT:
        default:
            sample_spec.format = PA_SAMPLE_FLOAT32NE;
            readf_function = (sf_count_t (*)(SNDFILE *sndfile, void *ptr, sf_count_t frames)) sf_readf_float;
            break;
    }

    if (verbose) {
        char t[PA_SAMPLE_SPEC_SNPRINT_MAX];
        pa_sample_spec_snprint(t, sizeof(t), &sample_spec);
        fprintf(stderr, "Using sample spec '%s'\n", t);
    }
    
    /* Set up a new main loop */
    if (!(m = pa_mainloop_new())) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        goto quit;
    }

    mainloop_api = pa_mainloop_get_api(m);

    r = pa_signal_init(mainloop_api);
    assert(r == 0);
    pa_signal_new(SIGINT, exit_signal_callback, NULL);
    signal(SIGPIPE, SIG_IGN);
    
    /* Create a new connection context */
    if (!(context = pa_context_new(mainloop_api, client_name))) {
        fprintf(stderr, "pa_context_new() failed.\n");
        goto quit;
    }

    pa_context_set_state_callback(context, context_state_callback, NULL);

    /* Connect the context */
    pa_context_connect(context, server, 1, NULL);

    /* Run the main loop */
    if (pa_mainloop_run(m, &ret) < 0) {
        fprintf(stderr, "pa_mainloop_run() failed.\n");
        goto quit;
    }
    
quit:
    if (stream)
        pa_stream_unref(stream);

    if (context)
        pa_context_unref(context);

    if (m) {
        pa_signal_done();
        pa_mainloop_free(m);
    }

    free(server);
    free(device);
    free(client_name);
    free(stream_name);

    if (sndfile)
        sf_close(sndfile);
    
    return ret;
}