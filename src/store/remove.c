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

#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <sys/types.h>
#include <fts.h>
#include <re.h>
#include "avs_store.h"


#ifndef ANDROID

static int remove_ent(FTSENT *ent)
{
	switch(ent->fts_info) {
	case FTS_D:       /* pre-order visit to directory ... skip  */
	case FTS_DOT:     /* "." or ".."  */
		return 0;
	case FTS_DC:      /* directory causing a cycle  */
		return EEXIST;
	case FTS_DNR:     /* directory that cannot be read  */
	case FTS_ERR:     /* error  */
	case FTS_NS:      /* file without stat and error  */
		return ent->fts_errno;
	case FTS_DEFAULT: /* something else  */
	case FTS_DP:      /* post-order directory visit  */
	case FTS_F:       /* regular file  */
	case FTS_NSOK:    /* file without stat but fine  */
	case FTS_SL:      /* symbolic link  */
	case FTS_SLNONE:  /* broken symlink  */
		if (remove(ent->fts_accpath))
			return errno;
		else
			return 0;
	default:
		/* Urmpf.  */
		return EPROTO;
	}
}


static int remove_path(const char *path)
{
	FTS *fts;
	FTSENT *ent;
	const char *argv[2] = { path, NULL };
	int err;

	fts = fts_open((char *const *)argv, FTS_NOSTAT | FTS_PHYSICAL, NULL);
	if (!fts)
		return errno;

	for (;;) {
		ent = fts_read(fts);
		if (!ent) {
			err = errno;
			break;
		}

		err = remove_ent(ent);
		if (err)
			break;
	}
	fts_close(fts);
	return err;
}


#endif


int store_remove_pathf(const char *fmt, ...)
{
#ifdef ANDROID
	return ENOSYS;
#else
	char path[1024];
	va_list ap;
	int err;

	va_start(ap, fmt);
	err = re_vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);
	if (err == -1)
		return EINVAL;

	return remove_path(path);
#endif
}


