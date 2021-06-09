/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <re/re.h>

#include "avs.h"


struct trace {
	char *path;
	FILE *fp;
	uint64_t ts;
	bool use_stdout;
};


static void destructor(void *arg)
{
	struct trace *t = arg;

	if (t->fp)
		fclose(t->fp);

	mem_deref(t->path);
}


int trace_alloc(struct trace **tp, const char *path, bool use_stdout)
{
	struct trace *t;
	int err = 0;

	t = mem_zalloc(sizeof(*t), destructor);
	if (!t)
		return ENOMEM;

	if (path) {
		err = str_dup(&t->path, path);
		t->fp = fopen(path, "w");
		if (!t->fp) {		
			err = errno;
			goto out;
		}
	}
	t->use_stdout = use_stdout;
	t->ts = tmr_jiffies();
	

 out:	
	if (err)
		mem_deref(t);
	else
		*tp = t;
	
	return err;	
}


void trace_write(struct trace *t, const char *fmt, ...)
{
	struct mbuf *mb;
	va_list args;
	uint64_t tdiff;

	if (!t)
		return;
	
	mb = mbuf_alloc(1024);
	if (!mb)
		return;

	tdiff = tmr_jiffies() - t->ts;
	
	mbuf_printf(mb, "[%d.%03d]: ", tdiff/1000, tdiff % 1000);
	
	va_start(args, fmt);

	mbuf_vprintf(mb, fmt, args);
	
	if (t->use_stdout || !t->fp)
		re_printf("%b", mb->buf, mb->end);

	if (t->fp)
		re_fprintf(t->fp, "%b", mb->buf, mb->end);

	va_end(args);

	mem_deref(mb);
}
