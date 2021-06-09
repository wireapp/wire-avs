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

#include <re.h>
#include "avs_log.h"
#include <string.h>


static struct {
	struct list logl;
	enum log_level min_level;
	bool stder;
} lg = {
	.logl  = LIST_INIT,
	.min_level = LOG_LEVEL_WARN,
	.stder = true
};


void log_register_handler(struct log *log)
{
	if (!log)
		return;

	list_append(&lg.logl, &log->le, log);
}


void log_unregister_handler(struct log *log)
{
	if (!log)
		return;

	list_unlink(&log->le);
}


void log_set_min_level(enum log_level level)
{
	lg.min_level = level;
}


enum log_level log_get_min_level(void)
{
	return lg.min_level;
}


void log_enable_stderr(bool enable)
{
	lg.stder = enable;
}


void vloglv(enum log_level level, const char *fmt, va_list ap)
{
	if (lg.min_level > level)
		return;

	vlog(level, fmt, ap);
}


void vlog(enum log_level level, const char *fmt, va_list ap)
{
	struct le *le;
	char *msg;
	int err;

	err = re_vsdprintf(&msg, fmt, ap);
	if (err)
		return;

	log_mask_ipaddr(msg);

	if (lg.stder) {

		bool color = level == LOG_LEVEL_WARN
			  || level == LOG_LEVEL_ERROR;

		if (color)
			(void)re_fprintf(stderr, "\x1b[31m"); /* Red */

		(void)re_fprintf(stderr, "%s", msg);

		if (color)
			(void)re_fprintf(stderr, "\x1b[;m");
	}

	le = lg.logl.head;

	while (le) {

		struct log *log = le->data;
		le = le->next;

		if (log->h)
			log->h(level, msg, log->arg);
	}

	mem_deref(msg);
}


void loglv(enum log_level level, const char *fmt, ...)
{
	va_list ap;

	if (level < lg.min_level)
		return;

	va_start(ap, fmt);
	vlog(level, fmt, ap);
	va_end(ap);
}


void debug(const char *fmt, ...)
{
	va_list ap;

	if (lg.min_level > LOG_LEVEL_DEBUG)
		return;

	va_start(ap, fmt);
	vlog(LOG_LEVEL_DEBUG, fmt, ap);
	va_end(ap);
}


void info(const char *fmt, ...)
{
	va_list ap;

	if (lg.min_level > LOG_LEVEL_INFO)
		return;

	va_start(ap, fmt);
	vlog(LOG_LEVEL_INFO, fmt, ap);
	va_end(ap);
}


void warning(const char *fmt, ...)
{
	va_list ap;

	if (lg.min_level > LOG_LEVEL_WARN)
		return;

	va_start(ap, fmt);
	vlog(LOG_LEVEL_WARN, fmt, ap);
	va_end(ap);
}


void error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(LOG_LEVEL_ERROR, fmt, ap);
	va_end(ap);
}

const char *anon_id(char *outid, const char *inid)
{
	if (outid) {
		memset(outid, 0, ANON_ID_LEN);
		if (inid) {
			str_ncpy(outid, inid, ANON_ID_LEN);
		}
	}

	return outid;
}

const char *anon_client(char *outid, const char *inid)
{
	if (outid) {
		memset(outid, 0, ANON_CLIENT_LEN);
		if (inid) {
			str_ncpy(outid, inid, ANON_CLIENT_LEN);
		}
	}

	return outid;
}


#define MASK_CHAR 'x'


void log_mask_ipaddr(const char *msg)
{
	struct pl a, b, c, d, e, f, g, h;
	const char *p = msg;
	const char *pend = msg + str_len(msg);
	size_t i;

	while (p < pend) {

		const size_t len = pend - p;

		if (0 == re_regex(p, len,
				  "[0-9]+.[0-9]+.[0-9]+.[0-9]+",
				  &a, &b, &c, &d)) {

			for (i=0; i<c.l; i++)
				*(char *)&c.p[i] = MASK_CHAR;

			for (i=0; i<d.l; i++)
				*(char *)&d.p[i] = MASK_CHAR;

			p = d.p + d.l;
		}
		else {
			break;
		}
	}

	p = msg;
	while (p < pend) {

		const size_t len = pend - p;

		if (0 == re_regex(p, len,
				  "[0-9a-fA-F]+:[0-9a-fA-F]+:[0-9a-fA-F]+:[0-9a-fA-F]+:"
				  "[0-9a-fA-F]+:[0-9a-fA-F]+:[0-9a-fA-F]+:[0-9a-fA-F]+",
				  &a, &b, &c, &d, &e, &f, &g, &h)) {

			for (i=0; i<d.l; i++)
				*(char *)&d.p[i] = MASK_CHAR;

			for (i=0; i<e.l; i++)
				*(char *)&e.p[i] = MASK_CHAR;

			for (i=0; i<f.l; i++)
				*(char *)&f.p[i] = MASK_CHAR;

			for (i=0; i<g.l; i++)
				*(char *)&g.p[i] = MASK_CHAR;

			for (i=0; i<h.l; i++)
				*(char *)&h.p[i] = MASK_CHAR;

			p = h.p + h.l;
		}
		else {
			break;
		}
	}
}
