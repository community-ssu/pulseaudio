#ifndef fooprotocolclihfoo
#define fooprotocolclihfoo

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

#include "core.h"
#include "socket-server.h"
#include "module.h"
#include "modargs.h"

typedef struct pa_protocol_cli pa_protocol_cli;

pa_protocol_cli* pa_protocol_cli_new(pa_core *core, pa_socket_server *server, pa_module *m, pa_modargs *ma);
void pa_protocol_cli_free(pa_protocol_cli *n);

#endif