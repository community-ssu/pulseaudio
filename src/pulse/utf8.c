/* $Id$ */

/* This file is based on the GLIB utf8 validation functions. The
 * original license text follows. */

/* gutf8.c - Operations on UTF-8 strings.
 *
 * Copyright (C) 1999 Tom Tromey
 * Copyright (C) 2000 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include "utf8.h"
#include "xmalloc.h"

#define FILTER_CHAR '_'

static inline int is_unicode_valid(uint32_t ch) {
    if (ch >= 0x110000) /* End of unicode space */
        return 0;
    if ((ch & 0xFFFFF800) == 0xD800) /* Reserved area for UTF-16 */
        return 0;
    if ((ch >= 0xFDD0) && (ch <= 0xFDEF)) /* Reserved */
        return 0;
    if ((ch & 0xFFFE) == 0xFFFE) /* BOM (Byte Order Mark) */
        return 0;
    return 1;
}

static inline int is_continuation_char(uint8_t ch) {
    if ((ch & 0xc0) != 0x80) /* 10xxxxxx */
        return 0;
    return 1;
}

static inline void merge_continuation_char(uint32_t *u_ch, uint8_t ch) {
    *u_ch <<= 6;
    *u_ch |= ch & 0x3f;
}

static char* utf8_validate(const char *str, char *output) {
    uint32_t val = 0;
    uint32_t min = 0;
    const uint8_t *p, *last;
    int size;
    uint8_t *o;

    o = (uint8_t*) output;
    for (p = (const uint8_t*) str; *p; p++) {
        if (*p < 128) {
            if (o)
                *o = *p;
        } else {
            last = p;

            if ((*p & 0xe0) == 0xc0) { /* 110xxxxx two-char seq. */
                size = 2;
                min = 128;
                val = *p & 0x1e;
                goto ONE_REMAINING;
            } else if ((*p & 0xf0) == 0xe0) { /* 1110xxxx three-char seq.*/
                size = 3;
                min = (1 << 11);
                val = *p & 0x0f;
                goto TWO_REMAINING;
            } else if ((*p & 0xf8) == 0xf0) { /* 11110xxx four-char seq */
                size = 4;
                min = (1 << 16);
                val = *p & 0x07;
            } else {
                size = 1;
                goto error;
            }

            p++;
            if (!is_continuation_char(*p))
                goto error;
            merge_continuation_char(&val, *p);

TWO_REMAINING:
            p++;
            if (!is_continuation_char(*p))
                goto error;
            merge_continuation_char(&val, *p);

ONE_REMAINING:
            p++;
            if (!is_continuation_char(*p))
                goto error;
            merge_continuation_char(&val, *p);

            if (val < min)
                goto error;

            if (!is_unicode_valid(val))
                goto error;

            if (o) {
                memcpy(o, last, size);
                o += size - 1;
            }

            if (o)
                o++;
            
            continue;

error:
            if (o) {
                *o = FILTER_CHAR;
                p = last; /* We retry at the next character */
            } else
                goto failure;
        }

        if (o)
            o++;
    }

    if (o) {
        *o = '\0';
        return output;
    }

    return (char*) str;

failure:
    return NULL;
}

const char* pa_utf8_valid (const char *str) {
    return utf8_validate(str, NULL);
}

char* pa_utf8_filter (const char *str) {
    char *new_str;

    new_str = pa_xnew(char, strlen(str) + 1);

    return utf8_validate(str, new_str);
}

#ifdef HAVE_ICONV

static char* iconv_simple(const char *str, const char *to, const char *from) {
    char *new_str;
    size_t len, inlen;

    iconv_t cd;
    ICONV_CONST char *inbuf;
    char *outbuf;
    size_t res, inbytes, outbytes;

    cd = iconv_open(to, from);
    if (cd == (iconv_t)-1)
        return NULL;

    inlen = len = strlen(str) + 1;
    new_str = pa_xmalloc(len);
    assert(new_str);

    while (1) {
        inbuf = (ICONV_CONST char*)str; /* Brain dead prototype for iconv() */
        inbytes = inlen;
        outbuf = new_str;
        outbytes = len;

        res = iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes);

        if (res != (size_t)-1)
            break;

        if (errno != E2BIG) {
            pa_xfree(new_str);
            new_str = NULL;
            break;
        }

        assert(inbytes != 0);

        len += inbytes;
        new_str = pa_xrealloc(new_str, len);
        assert(new_str);
    }

    iconv_close(cd);

    return new_str;
}

char* pa_utf8_to_locale (const char *str) {
    return iconv_simple(str, "", "UTF-8");
}

char* pa_locale_to_utf8 (const char *str) {
    return iconv_simple(str, "UTF-8", "");
}

#else

char* pa_utf8_to_locale (const char *str) {
    return NULL;
}

char* pa_locale_to_utf8 (const char *str) {
    return NULL;
}

#endif