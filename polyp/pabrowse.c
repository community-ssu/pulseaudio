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
#include <signal.h>

#include <polyp/mainloop.h>
#include <polyp/mainloop-signal.h>
#include <polyp/polyplib-browser.h>
#include <polyp/typeid.h>

static void exit_signal_callback(struct pa_mainloop_api*m, struct pa_signal_event *e, int sig, void *userdata) {
    fprintf(stderr, "Got signal, exiting\n");
    m->quit(m, 0);
}

static void dump_server(const struct pa_browse_info *i) {
    char t[16];

    if (i->cookie)
        snprintf(t, sizeof(t), "0x%08x", *i->cookie);
    
    printf("server: %s\n"
           "server-version: %s\n"
           "user-name: %s\n"
           "fqdn: %s\n"
           "cookie: %s\n",
           i->server,
           i->server_version ? i->server_version : "n/a",
           i->user_name ? i->user_name : "n/a",
           i->fqdn ? i->fqdn : "n/a",
           i->cookie ? t : "n/a");
}

static void dump_device(const struct pa_browse_info *i) {
    char t[16], ss[PA_SAMPLE_SPEC_SNPRINT_MAX];

    if (i->sample_spec)
        pa_sample_spec_snprint(ss, sizeof(ss), i->sample_spec);

    if (i->typeid)
        pa_typeid_to_string(*i->typeid, t, sizeof(t));
    
    printf("device: %s\n"
           "description: %s\n"
           "type: %s\n"
           "sample spec: %s\n",
           i->device,
           i->description ? i->description : "n/a",
           i->typeid ? t : "n/a",
           i->sample_spec ? ss : "n/a");
           
}

static void browser_callback(struct pa_browser *b, enum pa_browse_opcode c, const struct pa_browse_info *i, void *userdata) {
    assert(b && i);

    switch (c) {

        case PA_BROWSE_NEW_SERVER:
            printf("\n=> new server <%s>\n", i->name);
            dump_server(i);
            break;

        case PA_BROWSE_NEW_SINK:
            printf("\n=> new sink <%s>\n", i->name);
            dump_server(i);
            dump_device(i);
            break;

        case PA_BROWSE_NEW_SOURCE:
            printf("\n=> new source <%s>\n", i->name);
            dump_server(i);
            dump_device(i);
            break;
            
        case PA_BROWSE_REMOVE:
            printf("\n=> removed service <%s>\n", i->name);
            break;
            
        default:
            ;
    }
}


int main(int argc, char *argv[]) {
    struct pa_mainloop *mainloop = NULL;
    struct pa_browser *browser = NULL;
    int ret = 1, r;

    if (!(mainloop = pa_mainloop_new()))
        goto finish;

    r = pa_signal_init(pa_mainloop_get_api(mainloop));
    assert(r == 0);
    pa_signal_new(SIGINT, exit_signal_callback, NULL);
    pa_signal_new(SIGTERM, exit_signal_callback, NULL);
    signal(SIGPIPE, SIG_IGN);
    
    if (!(browser = pa_browser_new(pa_mainloop_get_api(mainloop))))
        goto finish;

    pa_browser_set_callback(browser, browser_callback, NULL);
    
    ret = 0;
    pa_mainloop_run(mainloop, &ret);

finish:
    if (mainloop) {
        pa_signal_done();
        pa_mainloop_free(mainloop);
    }

    return ret;
}