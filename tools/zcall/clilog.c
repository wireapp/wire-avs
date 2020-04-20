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
/* avs/zcall
 *
 * Logging
 */

#include <stdio.h>
#include <pthread.h>
#include <re.h>
#include <avs.h>
#include "cli.h"
#include <sys/time.h>


struct {
	enum log_level level;
	FILE *file;
} clilog_glob = {
	.level = LOG_LEVEL_WARN,
	.file = NULL
};


static const char *level_prefix(enum log_level level)
{
	switch (level) {

	case LOG_LEVEL_WARN:  return "WARNING: ";
	case LOG_LEVEL_ERROR: return "ERROR: ";
	default: return "";
	}
}


static void log_handler(uint32_t level, const char *msg, void *arg)
{
	(void)level;

	if (level >= clilog_glob.level) {

		bool color = level == LOG_LEVEL_WARN
			  || level == LOG_LEVEL_ERROR;

		if (color)
			re_printf("\x1b[31m"); /* Red */

		output(msg);

		if (color)
			re_printf("\x1b[;m");
	}
	if (clilog_glob.file) {
		struct timeval tv;

		if (gettimeofday(&tv, NULL) == 0) {
			struct tm  tstruct;
			uint32_t tms;
			char timebuf[64];
			const pthread_t tid = pthread_self();

			memset(timebuf, 0, 64);
			tstruct = *localtime(&tv.tv_sec);
			tms = tv.tv_usec / 1000;
			strftime(timebuf, sizeof(timebuf), "%m-%d %X", &tstruct);
			re_fprintf(clilog_glob.file, "%s.%03u T(0x%08x) %s%s",
				   timebuf, tms,
				   (void *)tid,
				   level_prefix(level), msg);
		}
		else {
			re_fprintf(clilog_glob.file, "%s%s", level_prefix(level), msg);
		}

#if 1
		/* NOTE: temp solution to find a leaking timer */
		fflush(clilog_glob.file);
#endif
	}
}

static struct log logh = {
	.h = log_handler
};


static bool debug_key_handler(int ch)
{
	(void) ch;

	switch (clilog_glob.level) {
	case LOG_LEVEL_DEBUG:
		clilog_glob.level = LOG_LEVEL_WARN;
		output("Log level set to WARN.\n");
		break;
	case LOG_LEVEL_INFO:
		clilog_glob.level = LOG_LEVEL_DEBUG;
		output("Log level set to DEBUG.\n");
		break;
	default:
		clilog_glob.level = LOG_LEVEL_INFO;
		output("Log level set to INFO.\n");
		break;
	}
	if (!clilog_glob.file)
		log_set_min_level(clilog_glob.level);

	return true;
}

struct key_stroke debug_stroke = {
	.ch = 'd',
	.h = debug_key_handler,
	.help = "change debug level"
};


int clilog_init(enum log_level level, const char *path)
{
	clilog_glob.level = level;
	if (path) {
		clilog_glob.file = fopen(path, "a");
		if (!clilog_glob.file) {
			return errno;
		}
		log_set_min_level(LOG_LEVEL_DEBUG);
	}
	else
		log_set_min_level(level);
	log_register_handler(&logh);
	log_enable_stderr(false);
	register_key_stroke(&debug_stroke);
	return 0;
}


void clilog_close(void)
{
	log_unregister_handler(&logh);
	log_enable_stderr(true);
	if (clilog_glob.file) {
		fflush(clilog_glob.file);
		fclose(clilog_glob.file);
	}
}

