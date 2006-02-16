#ifndef foocmdlinehfoo
#define foocmdlinehfoo

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

#include "daemon-conf.h"

/* Parese the command line and store its data in *c. Return the index
 * of the first unparsed argument in *d. */
int pa_cmdline_parse(pa_daemon_conf*c, int argc, char *const argv [], int *d);

/* Show the command line help. The command name is extracted from
 * argv[0] which should be passed in argv0. */
void pa_cmdline_help(const char *argv0);

#endif