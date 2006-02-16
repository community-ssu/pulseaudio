#ifndef fooiochannelhfoo
#define fooiochannelhfoo

/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <sys/types.h>
#include <polyp/mainloop-api.h>

/* A wrapper around UNIX file descriptors for attaching them to the a
   main event loop. Everytime new data may be read or be written to
   the channel a callback function is called. It is safe to destroy
   the calling iochannel object from the callback */

/* When pa_iochannel_is_readable() returns non-zero, the user has to
 * call this function in a loop until it is no longer set or EOF
 * reached. Otherwise strange things may happen when an EOF is
 * reached. */

typedef struct pa_iochannel pa_iochannel;

/* Create a new IO channel for the specified file descriptors for
input resp. output. It is safe to pass the same file descriptor for
both parameters (in case of full-duplex channels). For a simplex
channel specify -1 for the other direction. */

pa_iochannel* pa_iochannel_new(pa_mainloop_api*m, int ifd, int ofd);
void pa_iochannel_free(pa_iochannel*io);

ssize_t pa_iochannel_write(pa_iochannel*io, const void*data, size_t l);
ssize_t pa_iochannel_read(pa_iochannel*io, void*data, size_t l);

int pa_iochannel_is_readable(pa_iochannel*io);
int pa_iochannel_is_writable(pa_iochannel*io);
int pa_iochannel_is_hungup(pa_iochannel*io);

/* Don't close the file descirptors when the io channel is freed. By
 * default the file descriptors are closed. */
void pa_iochannel_set_noclose(pa_iochannel*io, int b);

/* Set the callback function that is called whenever data becomes available for read or write */
typedef void (*pa_iochannel_callback_t)(pa_iochannel*io, void *userdata);
void pa_iochannel_set_callback(pa_iochannel*io, pa_iochannel_callback_t callback, void *userdata);

/* In case the file descriptor is a socket, return a pretty-printed string in *s which describes the peer connected */
void pa_iochannel_socket_peer_to_string(pa_iochannel*io, char*s, size_t l);

/* Use setsockopt() to tune the recieve and send buffers of TCP sockets */
int pa_iochannel_socket_set_rcvbuf(pa_iochannel*io, size_t l);
int pa_iochannel_socket_set_sndbuf(pa_iochannel*io, size_t l);

pa_mainloop_api* pa_iochannel_get_mainloop_api(pa_iochannel *io);

#endif