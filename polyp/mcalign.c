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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "mcalign.h"
#include "xmalloc.h"

struct pa_mcalign {
    size_t base;
    struct pa_memchunk leftover, current;
    struct pa_memblock_stat *memblock_stat;
};

struct pa_mcalign *pa_mcalign_new(size_t base, struct pa_memblock_stat *s) {
    struct pa_mcalign *m;
    assert(base);

    m = pa_xmalloc(sizeof(struct pa_mcalign));
    m->base = base;
    pa_memchunk_reset(&m->leftover);
    pa_memchunk_reset(&m->current);
    m->memblock_stat = s;
    
    return m;
}

void pa_mcalign_free(struct pa_mcalign *m) {
    assert(m);

    if (m->leftover.memblock)
        pa_memblock_unref(m->leftover.memblock);

    if (m->current.memblock)
        pa_memblock_unref(m->current.memblock);
    
    pa_xfree(m);
}

void pa_mcalign_push(struct pa_mcalign *m, const struct pa_memchunk *c) {
    assert(m && c && c->memblock && c->length);
    
    /* Append to the leftover memory block */
    if (m->leftover.memblock) {
        assert(!m->current.memblock);
        
        /* Try to merge */
        if (m->leftover.memblock == c->memblock &&
            m->leftover.index + m->leftover.length == c->index) {

            /* Merge */
            m->leftover.length += c->length;

            /* If the new chunk is larger than m->base, move it to current */
            if (m->leftover.length >= m->base) {
                m->current = m->leftover;
                pa_memchunk_reset(&m->leftover);
            } 

        } else {
            size_t l;

            /* We have to copy */
            assert(m->leftover.length < m->base);
            l = m->base - m->leftover.length;
            
            if (l > c->length)
                l = c->length;

            /* Can we use the current block? */
            pa_memchunk_make_writable(&m->leftover, m->memblock_stat, m->base);

            memcpy((uint8_t*) m->leftover.memblock->data + m->leftover.index + m->leftover.length, (uint8_t*) c->memblock->data + c->index, l);
            m->leftover.length += l;

            assert(m->leftover.length <= m->base && m->leftover.length <= m->leftover.memblock->length);

            if (c->length > l) {
                /* Save the remainder of the memory block */
                m->current = *c;
                m->current.index += l;
                m->current.length -= l;
                pa_memblock_ref(m->current.memblock);
            }
        }
    } else {
        assert(!m->leftover.memblock && !m->current.memblock);

        /* Nothing to merge or copy, just store it */
        
        if (c->length >= m->base)
            m->current = *c;
        else
            m->leftover = *c;

        pa_memblock_ref(c->memblock);
    }
}

int pa_mcalign_pop(struct pa_mcalign *m, struct pa_memchunk *c) {
    assert(m && c);

    /* First test if there's a leftover memory block available */
    if (m->leftover.memblock) {
        assert(m->leftover.length > 0 && m->leftover.length <= m->base);

        /* The leftover memory block is not yet complete */
        if (m->leftover.length < m->base)
            return -1;

        /* Return the leftover memory block */
        *c = m->leftover;
        pa_memchunk_reset(&m->leftover);

        /* If the current memblock is too small move it the leftover */
        if (m->current.memblock && m->current.length < m->base) {
            m->leftover = m->current;
            pa_memchunk_reset(&m->current);
        }
        
        return 0;
    }

    /* Now let's see if there is other data available */
    if (m->current.memblock) {
        size_t l;
        assert(m->current.length >= m->base);

        /* The length of the returned memory block */
        l = m->current.length;
        l /= m->base;
        l *= m->base;
        assert(l > 0);

        /* Prepare the returned block */
        *c = m->current;
        pa_memblock_ref(c->memblock);
        c->length = l;

        /* Drop that from the current memory block */
        assert(l <= m->current.length);
        m->current.index += l;
        m->current.length -= l;

        /* In case the whole block was dropped ... */
        if (m->current.length == 0)
            pa_memblock_unref(m->current.memblock);
        else {
            /* Move the raimainder to leftover */
            assert(m->current.length < m->base && !m->leftover.memblock);

            m->leftover = m->current;
        }

        pa_memchunk_reset(&m->current);
            
        return 0;
    }

    /* There's simply nothing */
    return -1;
    
}