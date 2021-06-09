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

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <re.h>
#include "avs_log.h"
#include "avs_string.h"
#include "avs_store.h"


struct store {
	char *dir;
	char *user;
};


struct sobject {
	char *path;
	FILE *file;
};


static void store_destructor(void *arg)
{
	struct store *st = arg;

	mem_deref(st->dir);
	mem_deref(st->user);
}


int store_mkdirf(mode_t mode, const char *fmt, ...)
{
	char path[1024];
	va_list ap;
	int err;

	va_start(ap, fmt);
	err = re_vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);
	if (err == -1)
		return EINVAL;

	if (mkdir(path, mode) < 0 && errno != EEXIST)
		return errno;

	return 0;
}


int store_alloc(struct store **stp, const char *dir)
{
	struct store *st;
	int err;

	if (!stp || !dir)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), store_destructor);
	if (!st)
		return ENOMEM;

	err = store_mkdirf(0700, "%s", dir);
	if (err) {
		error("Failed to create cache directory %s: %m\n",
		      dir, err);
		goto out;
	}
	err = store_mkdirf(0700, "%s/users", dir);
	if (err) {
		error("Failed to create user cache directory %s/users: %m\n",
		      dir, err);
		goto out;
	}
	err = store_mkdirf(0700, "%s/global", dir);
	if (err) {
		error("Failed to create global cache directory %s/global: "
		      "%m\n",
		      dir, err);
		goto out;
	}

	err = str_dup(&st->dir, dir);
	if (err)
		goto out;

	*stp = st;

 out:
	if (err)
		mem_deref(st);

	return err;
}


int store_set_user(struct store *st, const char *user_id)
{
	char *usercpy;
	int err;

	if (!st || !user_id || strchr(user_id, '/'))
		return EINVAL;

	err = store_mkdirf(0700, "%s/users/%s", st->dir, user_id);
	if (err)
		return err;

	err = str_dup(&usercpy, user_id);
	if (err)
		return err;

	mem_deref(st->user);
	st->user = usercpy;
	return 0;
}


/*** store_flush_user
 */

int store_flush_user(struct store *st)
{
	int err;

	if (!st)
		return EINVAL;

	err = store_remove_pathf("%s/users/%s", st->dir, st->user);
	if (err)
		return err;

	return store_mkdirf(0700, "%s/users/%s", st->dir, st->user);
}


/*** struct sobject
 */

static void sobject_destructor(void *arg)
{
	struct sobject *so = arg;

	mem_deref(so->path);
	if (so->file)
		fclose(so->file);
}


static int sobject_alloc(struct sobject **sop, const char *mode,
			      const char *format, ...)
{
	struct sobject *so;
	va_list ap;
	int err;

	so = mem_zalloc(sizeof(*so), sobject_destructor);
	if (!so)
		return ENOMEM;

	va_start(ap, format);
	err = re_vsdprintf(&so->path, format, ap);
	va_end(ap);
	if (err)
		goto out;

	so->file = fopen(so->path, mode);
	if (!so->file) {
		err = errno;
		goto out;
	}

	*sop = so;

 out:
	if (err)
		mem_deref(so);
	return err;
}


/*** store_user_open
 */

int store_user_open(struct sobject **sop, struct store *st,
		    const char *type, const char *id, const char *mode)
{
	int err;

	if (!sop || !st || !type || !id || !mode)
		return EINVAL;

	err = store_mkdirf(0700, "%s/users/%s/%s", st->dir, st->user, type);
	if (err)
		return err;

	return sobject_alloc(sop, mode, "%s/users/%s/%s/%s", st->dir,
				  st->user, type, id);
}


/*** store_global_open
 */

int store_global_open(struct sobject **sop, struct store *st,
		      const char *type, const char *id, const char *mode)
{
	int err;

	if (!sop || !st || !type || !id || !mode)
		return EINVAL;

	err = store_mkdirf(0700, "%s/global/%s", st->dir, type);
	if (err)
		return err;

	return sobject_alloc(sop, mode, "%s/global/%s/%s", st->dir,
				  type, id);
}


/*** store_user/global_unlink
 */

static int unlinkf(const char *fmt, ...)
{
	char path[1024];
	va_list ap;
	int err;

	va_start(ap, fmt);
	err = re_vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);
	if (err == -1)
		return EINVAL;

	if (unlink(path) < 0 && errno != ENOENT && errno != ENOTDIR)
		return errno;

	return 0;
}


int store_user_unlink(struct store *st, const char *type, const char *id)
{
	if (!st || !type || !id)
		return EINVAL;

	return unlinkf("%s/users/%s/%s/%s", st->dir, st->user, type, id);
}


int store_global_unlink(struct store *st, const char *type, const char *id)
{
	if (!st || !type || !id)
		return EINVAL;

	return unlinkf("%s/global/%s/%s", st->dir, type, id);
}


/*** store_user/global_dir
 */

static int path_dir(store_apply_h *h, void *arg, const char *fmt, ...)
{
	char path[1024];
	va_list ap;
	DIR *dir;
	struct dirent *dent;
	int err = 0;

	va_start(ap, fmt);
	err = re_vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);
	if (err == -1)
		return EINVAL;
	
	dir = opendir(path);
	if (!dir) {
		if (errno == ENOENT)
			return 0;
		else
			return errno;
	}

	while ((dent = readdir(dir)) != NULL) {
		if (streq(dent->d_name, ".") || streq(dent->d_name, ".."))
			continue;
		err = h(dent->d_name, arg);
		if (err)
			break;
	}
	closedir(dir);
	return err;
}


int store_user_dir(const struct store *st, const char *type,
		   store_apply_h *h, void *arg)
{
	return path_dir(h, arg, "%s/users/%s/%s", st->dir, st->user, type);
}


int store_global_dir(const struct store *st, const char *type,
		     store_apply_h *h, void *arg)
{
	return path_dir(h, arg, "%s/global/%s", st->dir, type);
}


/*** sobject_close
 */

void sobject_close(struct sobject *so)
{
	if (!so || !so->file)
		return;

	fclose(so->file);
	so->file = NULL;
}


/*** Writing
 */

int sobject_write(struct sobject *so, const uint8_t *buf, size_t size)
{
	if (!so || !so->file || !buf)
		return EINVAL;

	if (fwrite(buf, size, 1, so->file) == 0) 
		return EPIPE;
	return 0;
}


int sobject_write_u8(struct sobject *so, uint8_t v)
{
	return sobject_write(so, (uint8_t *)&v, sizeof(v));
}


int sobject_write_u16(struct sobject *so, uint16_t v)
{
	return sobject_write(so, (uint8_t *)&v, sizeof(v));
}


int sobject_write_u32(struct sobject *so, uint32_t v)
{
	return sobject_write(so, (uint8_t *)&v, sizeof(v));
}


int sobject_write_u64(struct sobject *so, uint64_t v)
{
	return sobject_write(so, (uint8_t *)&v, sizeof(v));
}


int sobject_write_dbl(struct sobject *so, double v)
{
	return sobject_write(so, (uint8_t *)&v, sizeof(v));
}


int sobject_write_lenstr(struct sobject *so, const char *str)
{
	size_t len;
	int err;

	if (str)
		len = strlen(str);
	else
		len = (size_t) -1;

	err = sobject_write(so, (uint8_t *)&len, sizeof(len));
	if (err)
		goto out;

	if (str && len > 0)
		err = sobject_write(so, (uint8_t *)str, len);

 out:
	return err;
}


int sobject_write_pl(struct sobject *so, const struct pl *pl)
{
	size_t len;
	int err;

	if (pl->p)
		len = pl->l;
	else
		len = (size_t) -1;

	err = sobject_write(so, (uint8_t *)&len, sizeof(len));
	if (err)
		goto out;

	if (pl->p && len > 0)
		err = sobject_write(so, (uint8_t *) pl->p, len);

 out:
	return err;
}


/*** Reading
 */

int sobject_read(struct sobject *so, uint8_t *buf, size_t size)
{
	if (!so || !so->file || !buf)
		return EINVAL;

	if (fread(buf, size, 1, so->file) == 0)
		return EPIPE;

	return 0;
}


int sobject_read_u8(uint8_t* v, struct sobject *so)
{
	return sobject_read(so, (uint8_t *)v, sizeof(*v));
}


int sobject_read_u16(uint16_t* v, struct sobject *so)
{
	return sobject_read(so, (uint8_t *)v, sizeof(*v));
}


int sobject_read_u32(uint32_t* v, struct sobject *so)
{
	return sobject_read(so, (uint8_t *)v, sizeof(*v));
}


int sobject_read_u64(uint64_t* v, struct sobject *so)
{
	return sobject_read(so, (uint8_t *)v, sizeof(*v));
}


int sobject_read_dbl(double* v, struct sobject *so)
{
	return sobject_read(so, (uint8_t *)v, sizeof(*v));
}


int sobject_read_lenstr(char **strp, struct sobject *so)
{
	size_t len;
	char *str;
	int err;

	err = sobject_read(so, (uint8_t *)&len, sizeof(len));
	if (err)
		return err;

	if (len == (size_t) -1) {
		*strp = NULL;
		return 0;
	}

	str = mem_alloc(len + 1, NULL);
	if (!str)
		return ENOMEM;

	if (len > 0) {
		err = sobject_read(so, (uint8_t *)str, len);
		if (err)
			goto out;
	}
	str[len] = '\0';
	*strp = str;

 out:
	if (err)
		mem_deref(str);
	return err;
}


int sobject_read_pl(struct pl *pl, struct sobject *so)
{
	size_t len;
	char *str;
	int err;

	err = sobject_read(so, (uint8_t*) &len, sizeof(len));
	if (err)
		return err;

	if (len == (size_t) -1) {
		pl->p = NULL;
		pl->l = 0;
	}

	str = mem_alloc(len + 1, NULL);
	if (!str)
		return ENOMEM;

	if (len > 0) {
		err = sobject_read(so, (uint8_t *) str, len);
		if (err)
			goto out;
	}
	str[len] = '\0';
	pl->p = str;
	pl->l = len;

 out:
	if (err)
		mem_deref(str);
	return err;
}

