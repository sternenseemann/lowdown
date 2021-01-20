/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016, 2021, Kristaps Dzonsons
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

static void
hbuf_init(struct lowdown_buf *buf, size_t unit, int buffer_free)
{
	assert(buf);

	buf->data = NULL;
	buf->size = buf->maxsize = 0;
	buf->unit = unit;
	buf->buffer_free = buffer_free;
}

/*
 * Clone the buffer at "buf" into the one at "v".
 * The storage of "v" is externally managed.
 * This is a deep copy.
 * Always returns a valid pointer to "v".
 */
struct lowdown_buf *
hbuf_clone(const struct lowdown_buf *buf, struct lowdown_buf *v)
{

	v->data = NULL;
	if (buf->size) {
		v->data = xmalloc(buf->size);
		memcpy(v->data, buf->data, buf->size);
	} 
	v->size = buf->size;
	v->maxsize = buf->maxsize;
	v->unit = buf->unit;
	v->buffer_free = buf->buffer_free;
	return v;
}

void
hbuf_truncate(struct lowdown_buf *buf)
{

	buf->size = 0;
}

int
hbuf_streq(const struct lowdown_buf *buf1, const char *buf2)
{
	size_t	 sz;

	sz = strlen(buf2);
	return buf1->size == sz &&
	       memcmp(buf1->data, buf2, sz) == 0;
}

int
hbuf_strprefix(const struct lowdown_buf *buf1, const char *buf2)
{
	size_t	 sz;

	sz = strlen(buf2);
	return buf1->size >= sz &&
	       memcmp(buf1->data, buf2, sz) == 0;
}

int
hbuf_eq(const struct lowdown_buf *buf1, const struct lowdown_buf *buf2)
{

	return buf1->size == buf2->size &&
	       memcmp(buf1->data, buf2->data, buf1->size) == 0;
}

struct lowdown_buf *
hbuf_new(size_t unit)
{
	struct lowdown_buf	*ret;

	if ((ret = malloc(sizeof(struct lowdown_buf))) == NULL)
		return NULL;
	hbuf_init(ret, unit, 1);
	return ret;
}

/*
 * Exported function name.
 */
struct lowdown_buf *
lowdown_buf_new(size_t unit)
{

	return hbuf_new(unit);
}

/* 
 * Free the buffer.
 * Passing NULL is a noop.
 */
void
hbuf_free(struct lowdown_buf *buf)
{

	if (buf == NULL) 
		return;
	free(buf->data);
	if (buf->buffer_free)
		free(buf);
}

void
lowdown_buf_free(struct lowdown_buf *buf)
{

	hbuf_free(buf);
}

int
hbuf_grow(struct lowdown_buf *buf, size_t neosz)
{
	size_t	 neoasz;
	void	*pp;

	if (buf->maxsize >= neosz)
		return 1;

	/* FIXME: don't loop!  Use arithmetic! */

	neoasz = buf->maxsize + buf->unit;
	while (neoasz < neosz)
		neoasz += buf->unit;

	if ((pp = realloc(buf->data, neoasz)) == NULL)
		return 0;
	buf->data = pp;
	buf->maxsize = neoasz;
	return 1;
}

void
hbuf_putb(struct lowdown_buf *buf, const struct lowdown_buf *b)
{

	assert(buf != NULL && b != NULL);
	hbuf_put(buf, b->data, b->size);
}

/* 
 * Append raw data to a buffer.
 * May not be NULL.
 * See hbuf_grow().
 */
void
hbuf_put(struct lowdown_buf *buf, const char *data, size_t size)
{
	assert(buf != NULL && buf->unit);

	if (data == NULL || size == 0)
		return;

	if (buf->size + size > buf->maxsize)
		hbuf_grow(buf, buf->size + size);

	memcpy(buf->data + buf->size, data, size);
	buf->size += size;
}

/* 
 * Append a nil-terminated string to a buffer.
 * Neither may be NULL.
 * See hbuf_put().
 */
void
hbuf_puts(struct lowdown_buf *buf, const char *str)
{

	assert(buf != NULL && str != NULL);
	hbuf_put(buf, str, strlen(str));
}

/* 
 * Append a single char to a buffer.
 * May not be NULL.
 * See hbuf_grow().
 */
void
hbuf_putc(struct lowdown_buf *buf, char c)
{
	assert(buf && buf->unit);

	if (buf->size >= buf->maxsize)
		hbuf_grow(buf, buf->size + 1);

	buf->data[buf->size] = c;
	buf->size += 1;
}

int
hbuf_putf(struct lowdown_buf *buf, FILE *file)
{

	assert(buf != NULL && buf->unit);
	while (!(feof(file) || ferror(file))) {
		if (!hbuf_grow(buf, buf->size + buf->unit))
			return 0;
		buf->size += fread(buf->data + buf->size, 
			1, buf->unit, file);
	}

	return ferror(file) == 0;
}

/* 
 * Formatted printing to a buffer.
 */
void
hbuf_printf(struct lowdown_buf *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	assert(buf && buf->unit);

	if (buf->size >= buf->maxsize)
		hbuf_grow(buf, buf->size + 1);

	va_start(ap, fmt);
	n = vsnprintf(buf->data + buf->size, buf->maxsize - buf->size, fmt, ap);
	va_end(ap);

	if (n < 0)
		return;

	if ((size_t)n >= buf->maxsize - buf->size) {
		hbuf_grow(buf, buf->size + n + 1);

		va_start(ap, fmt);
		n = vsnprintf(buf->data + buf->size, buf->maxsize - buf->size, fmt, ap);
		va_end(ap);
	}

	if (n < 0)
		return;

	buf->size += n;
}
