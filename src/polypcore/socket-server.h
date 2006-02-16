#ifndef foosocketserverhfoo
#define foosocketserverhfoo

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

#include <inttypes.h>
#include <polyp/mainloop-api.h>
#include "iochannel.h"

/* It is safe to destroy the calling socket_server object from the callback */

typedef struct pa_socket_server pa_socket_server;

pa_socket_server* pa_socket_server_new(pa_mainloop_api *m, int fd);
pa_socket_server* pa_socket_server_new_unix(pa_mainloop_api *m, const char *filename);
pa_socket_server* pa_socket_server_new_ipv4(pa_mainloop_api *m, uint32_t address, uint16_t port, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv6(pa_mainloop_api *m, const uint8_t address[16], uint16_t port);

void pa_socket_server_unref(pa_socket_server*s);
pa_socket_server* pa_socket_server_ref(pa_socket_server *s);

void pa_socket_server_set_callback(pa_socket_server*s, void (*on_connection)(pa_socket_server*s, pa_iochannel *io, void *userdata), void *userdata);

char *pa_socket_server_get_address(pa_socket_server *s, char *c, size_t l);

#endif