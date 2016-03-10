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
/*
 * libavs -- Rest client
 *
 * HTTP Cookie Jar
 *
 * See RFC 6265, section 5.3 for details.
 *
 */
#include <string.h>
#include <time.h>
#include <re.h>
#include "avs_log.h"
#include "avs_store.h"
#include "avs_rest.h"


static const struct pl cookie_header = PL("Cookie: ");

static const struct pl param_expires   = PL("expires");
static const struct pl param_max_age   = PL("max-age");
static const struct pl param_domain    = PL("domain");
static const struct pl param_path      = PL("path");
static const struct pl param_secure    = PL("secure");
static const struct pl param_http_only = PL("httponly");


struct cookie_jar {
	struct store *store;
	struct list cookiel;
};

struct raw_cookie {
	struct pl name;
	struct pl value;

	/* optional */
	struct pl expires;
	struct pl max_age;
	struct pl domain;
	struct pl path;
	bool      secure;
	bool      http_only;
};

struct stored_cookie {
	struct le le;

	struct pl name;
	struct pl value;
	time_t expires;
	struct pl domain;
	struct pl path;
	time_t created;
	time_t last_access;
	bool persistent;
	bool host_only;
	bool secure;
	bool http_only;
};


/*** struct stored_cookie
 */

static void stored_cookie_destructor(void *arg)
{
	struct stored_cookie *sc = arg;

	list_unlink(&sc->le);
	mem_deref((void *) sc->name.p);
	mem_deref((void *) sc->value.p);
	mem_deref((void *) sc->domain.p);
	mem_deref((void *) sc->path.p);
}


static int load_stored_cookie(struct cookie_jar *jar, struct sobject *so)
{
	struct stored_cookie *cookie;
	uint64_t v64;
	uint8_t v8;
	int err;

	cookie = mem_zalloc(sizeof(*cookie), stored_cookie_destructor);
	if (!cookie)
		return ENOMEM;

	err = sobject_read_pl(&cookie->name, so);
	if (err)
		goto out;
	err = sobject_read_pl(&cookie->value, so);
	if (err)
		goto out;
	err = sobject_read_u64(&v64, so);
	if (err)
		goto out;
	cookie->expires = (time_t) v64;
	err = sobject_read_pl(&cookie->domain, so);
	if (err)
		goto out;
	err = sobject_read_pl(&cookie->path, so);
	if (err)
		goto out;
	err = sobject_read_u64(&v64, so);
	if (err)
		goto out;
	cookie->created = (time_t) v64;
	err = sobject_read_u64(&v64, so);
	if (err)
		goto out;
	cookie->last_access = (time_t) v64;
	err = sobject_read_u8(&v8, so);
	if (err)
		goto out;
	cookie->persistent = v8 & 0x01;
	cookie->host_only = v8 & 0x02;
	cookie->secure = v8 & 0x04;
	cookie->http_only = v8 & 0x08;

	list_append(&jar->cookiel, &cookie->le, cookie);

 out:
	if (err)
		mem_deref(cookie);
	return err;
}


static int save_stored_cookie(struct sobject *so,
			      const struct stored_cookie *c)
{
	int err;

	err = sobject_write_pl(so, &c->name);
	if (err)
		return err;
	err = sobject_write_pl(so, &c->value);
	if (err)
		return err;
	err = sobject_write_u64(so, c->expires);
	if (err)
		return err;
	err = sobject_write_pl(so, &c->domain);
	if (err)
		return err;
	err = sobject_write_pl(so, &c->path);
	if (err)
		return err;
	err = sobject_write_u64(so, c->created);
	if (err)
		return err;
	err = sobject_write_u64(so, c->last_access);
	if (err)
		return err;
	err = sobject_write_u8(so, c->persistent
				   | (c->host_only << 1)
				   | (c->secure << 2)
				   | (c->http_only << 3));
	return err;
}


static void dump_stored_cookie(const struct stored_cookie *c)
{
	debug("Stored cookie:\n\tname='%r'"
			   "\n\tvalue='%r'"
			   "\n\texpires=%lli"
			   "\n\tdomain='%r'"
			   "\n\tpath='%r'"
			   "\n\tcreated=%lli"
			   "\n\tlast_access=%lli"
			   "\n\tpersistent=%i"
			   "\n\thost_only=%i"
			   "\n\tsecure=%i"
			   "\n\thttp_only=%i",
	      &c->name, &c->value, (long long int) c->expires,
	      &c->domain, &c->path, (long long int) c->created,
	      (long long int) c->last_access, (int) c->persistent,
	      (int) c->host_only, (int) c->secure, (int) c->http_only);
}



/*** struct cookie_jar
 */

static void cookie_jar_destructor(void *arg)
{
	struct cookie_jar *jar = arg;

	mem_deref(jar->store);
	list_flush(&jar->cookiel);
}


static int load_cookies(struct cookie_jar *jar)
{
	struct sobject *so;
	int err;

	err = store_user_open(&so, jar->store, "state", "cookie-jar", "rb");
	if (err) {
		if (err == ENOENT)
			return 0;
		else
			return err;
	}

	while (!err) {
		err = load_stored_cookie(jar, so);
		if (err)
			goto out;
	}

 out:
	mem_deref(so);
	return 0;
}


static int save_cookies(struct cookie_jar *jar)
{
	struct sobject *so;
	struct le *le;
	int err;

	err = store_user_open(&so, jar->store, "state", "cookie-jar", "wb");
	if (err)
		return err;

	LIST_FOREACH(&jar->cookiel, le) {
		struct stored_cookie *cookie = le->data;

		err = save_stored_cookie(so, cookie);
		if (err)
			goto out;
	}

 out:
	mem_deref(so);
	return err;
}
	

int cookie_jar_alloc(struct cookie_jar **jarp, struct store *store)
{
	struct cookie_jar *jar;
	int err = 0;

	if (!jarp)
		return EINVAL;

	jar = mem_zalloc(sizeof(*jar), cookie_jar_destructor);
	if (!jar)
		return ENOMEM;

	if (store) {
		jar->store = mem_ref(store);
		err = load_cookies(jar);
		if (err)
			goto out;
	}

	*jarp = jar;

 out:
	if (err)
		mem_deref(jar);

	return err;
}


/*** cookie_jar_print_to_request
 */

static bool domain_match(const struct pl *str, const struct pl *domain)
{
	struct pl tmp;

	if (str->l < domain->l)
		return false;
	else if (str->l == domain->l)
		return !pl_casecmp(str, domain);

	tmp.p = str->p + (str->l - domain->l);
	tmp.l = domain->l;
	return !pl_casecmp(&tmp, domain) && *(tmp.p - 1) == '.';
}


static bool path_match(const struct pl *str, const struct pl *path)
{
	struct pl tmp;

	if (str->l < path->l)
		return false;
	else if (str->l == path->l)
		return !pl_cmp(str, path);

	tmp.p = str->p;
	tmp.l = path->l;

	if (pl_cmp(str, &tmp))
		return false;

	return *(tmp.p + tmp.l - 1) == '/' || *(path->p + str->l) == '/';
}


int cookie_jar_print_to_request(struct cookie_jar *jar, struct re_printf *pf,
				const char *uri)
{
	struct pl scheme, host, port, path;
	bool http_scheme, secure;
	struct le *le;
	bool printing = false;

	if (!jar || !pf || !uri)
		return EINVAL;

	debug("Cookies for %s:\n", uri);

	if (re_regex(uri, strlen(uri), "[a-z]+://[^:/]+[:]*[0-9]*[^?]+",
		     &scheme, &host, NULL, &port, &path) || scheme.p != uri)
		return EINVAL;

	http_scheme = !pl_strcmp(&scheme, "http") ||
		      !pl_strcmp(&scheme, "https");

	/* XXX There are more secure protocols.
	 */
	secure = !pl_strcmp(&scheme, "https") || !pl_strcmp(&scheme, "wss");

	/* Add cookies according to RFC 6265, section 5.4.
	 */
	LIST_FOREACH(&jar->cookiel, le) {
		struct stored_cookie *c = le->data;

		/* Step 1.
		 */
		if (c->host_only) {
			if (pl_cmp(&host, &c->domain))
				continue;
		}
		else {
			if (!domain_match(&host, &c->domain)) {
				continue;
			}
		}

		if (!path_match(&path, &c->path))
			continue;

		if (c->secure && !secure)
			continue;

		if (c->http_only && !http_scheme)
			continue;

		/* Step 2.
		 *
		 * XXX We don't sort yet. We should probably keep
		 *     jar->cookiel in the order specified by step 2.
		 */

		/* Step 3.
		 */
		c->last_access = time(NULL);

		/* Step 4.
		 */
		if (!printing) {
			pf->vph(cookie_header.p, cookie_header.l, pf->arg);
			printing = true;
		}
		else
			pf->vph("; ", 2, pf->arg);
		debug("\t%r=%r\n", &c->name, &c->value);
		pf->vph(c->name.p, c->name.l, pf->arg);
		pf->vph("=", 1, pf->arg);
		pf->vph(c->value.p, c->value.l, pf->arg);
	}
	if (printing)
		pf->vph("\r\n", 2, pf->arg);

	return 0;
}


/*** cookie_jar_handle_response
 */

static void decode_value_param(struct pl *dest,
			       const struct pl *hval, const struct pl *name)
{
	char expr[128];
	struct pl v;

	(void)re_snprintf(expr, sizeof(expr), ";[ \t\r\n]*%r=[^;]+", name);

	/* XXX The RFC specifically says to always use the last value.
	 */
	if (!re_regex(hval->p, hval->l, expr, NULL, &v))
		*dest = v;
}


static void decode_flag_param(bool *dest, const struct pl *hval,
			      const struct pl *name)
{
	char expr[128];
	struct pl semi;

	(void)re_snprintf(expr, sizeof(expr), ";[ \t\r\n]*%r[;]*", name);
	if (re_regex(hval->p, hval->l, expr, NULL, &semi))
		*dest = false;
	else if (pl_isset(&semi))
		*dest = true;
	else if (hval->p + hval->l == semi.p)
		*dest = true;
	else
		*dest = false;
}


static int decode_set_cookie_header(struct raw_cookie *cookie,
				    const struct pl *hval)
{
	struct pl r = *hval;

	if (!cookie || !hval)
		return EINVAL;

	memset(cookie, 0, sizeof(*cookie));

	if (re_regex(r.p, r.l, "[ \t\r\n]*[^ \t\r\n=]+=[~ \t\r\n;]*",
		      NULL, &cookie->name, &cookie->value))
		return EINVAL;

	pl_advance(&r, cookie->value.p + cookie->value.l - r.p);

	/* XXX This can probably be optimized, but the spec is a little
	 *     iffy and this surely is the easiest ...
	 */
	decode_value_param(&cookie->expires, &r, &param_expires);
	decode_value_param(&cookie->max_age, &r, &param_max_age);
	decode_value_param(&cookie->domain, &r, &param_domain);
	decode_value_param(&cookie->path, &r, &param_path);
	decode_flag_param(&cookie->secure, &r, &param_secure);
	decode_flag_param(&cookie->http_only, &r, &param_http_only);

	if (pl_isset(&cookie->domain) && *cookie->domain.p == '.')
		pl_advance(&cookie->domain, 1);

	return 0;
}


static int parse_max_age(struct stored_cookie *sc,
			 const struct raw_cookie *rc)
{
	/* XXX This happily parses a broken value ...
	 */
	sc->expires = sc->created + (time_t)pl_u64(&rc->max_age);
	return 0;
}


/* Parses an RFC 2616 full date (section 3.3.1) in rc->expires and sets
 * sc->expires to it. Returns ENOENT if the format is broken.
 */
static int parse_expires(struct stored_cookie *sc,
			 const struct raw_cookie *rc)
{
	struct pl day, month, year, hour, minute, second;
	struct tm tm;
	time_t res;
	int err;


	/* Try rfc1123-date, then rfc850-date, then asctime-date, then fail.
	 */
	err = re_regex(rc->expires.p, rc->expires.l,
		       "[a-z]+,[ \t\r\n]+"
		       "[0-9]+[ \t\r\n]+[a-z]+[ \t\r\n]+[0-9]+[ \t\r\n]+"
		       "[0-9]+:[0-9]+:[0-9]+[ \t\r\n]+GMT",
		       NULL, NULL,
		       &day, NULL, &month, NULL, &year, NULL,
		       &hour, &minute, &second, NULL);
	if (err) {
		err = re_regex(rc->expires.p, rc->expires.l,
				"[a-z]+,[ \t\r\n]+"
				"[0-9]+-[a-z]+-[0-9]+[ \t\r\n]*"
				"[0-9]+:[0-9]+:[0-9]+[ \t\r\n]+GMT",
				NULL, NULL,
				&day, &month, &year, NULL,
				&hour, &minute, &second, NULL);
	}
	if (err) {
		err = re_regex(rc->expires.p, rc->expires.l,
				"[a-z]+[ \t\r\n]+"
				"[a-z]+[ \t\r\n]+[0-9]+[ \t\r\n]+"
				"[0-9]+:[0-9]+:[0-9]+[ \t\r\n]+"
				"[0-9]+",
				NULL, NULL,
				&month, NULL, &day, NULL,
				&hour, &minute, &second, NULL,
				&year);
	}
	if (err) {
		debug("cookie expires parsing failed for '%r'\n",
		      &rc->expires);
		return ENOENT;
	}

	tm.tm_isdst = 0;
	tm.tm_sec = pl_u32(&second);
	tm.tm_min = pl_u32(&minute);
	tm.tm_hour = pl_u32(&hour);
	tm.tm_mday = pl_u32(&day);
	tm.tm_year = pl_u32(&year);
	if (tm.tm_year < 100) {
		/* Hardwire two-digit years to be in the 21st century as
		 * job protection for 2099.
		 */
		tm.tm_year += 100;
	}
	else {
		tm.tm_year -= 1900;
	}
	if (!pl_strcasecmp(&month, "jan"))
		tm.tm_mon = 0;
	else if (!pl_strcasecmp(&month, "feb"))
		tm.tm_mon = 1;
	else if (!pl_strcasecmp(&month, "mar"))
		tm.tm_mon = 2;
	else if (!pl_strcasecmp(&month, "apr"))
		tm.tm_mon = 3;
	else if (!pl_strcasecmp(&month, "may"))
		tm.tm_mon = 4;
	else if (!pl_strcasecmp(&month, "jun"))
		tm.tm_mon = 5;
	else if (!pl_strcasecmp(&month, "jul"))
		tm.tm_mon = 6;
	else if (!pl_strcasecmp(&month, "aug"))
		tm.tm_mon = 7;
	else if (!pl_strcasecmp(&month, "sep"))
		tm.tm_mon = 8;
	else if (!pl_strcasecmp(&month, "oct"))
		tm.tm_mon = 9;
	else if (!pl_strcasecmp(&month, "nov"))
		tm.tm_mon = 10;
	else if (!pl_strcasecmp(&month, "dec"))
		tm.tm_mon = 11;
	else {
		debug("Illegal month in cookie expires '%r'\n",
		      &rc->expires);
	}

	res = mktime(&tm);
	if (res == (time_t) -1) {
		debug("Illegal cookie expires '%r'\n",
		      &rc->expires);
		return ENOENT;
	}
	sc->expires = res;

	/* XXX mktime creates local time. We need to compensate for that
	 *     but there is no portable way.
	 */
#ifdef timezone
	sc->expires -= timezone;
#else
#ifdef __timezone
	sc->expires -= __timezone;
#else
	sc->expires -= 12 * 3600;
#endif
#endif
	return 0;
}


/* Creates a stored cookie and stores it in the *jar* according to the
 * rules layed out in RFC 6265, section 5.3, steps 1 to 10.
 *
 * Returns 0 if the cookie was successfully created, EINVAL
 * if any of the arguments is wrong, ENOMEM if there is no memory, and
 * ENOENT if the cookie was ignored according to the storage rules. This
 * also happens if the cookie cannot be parsed correctly.
 */
static int process_set_cookie(struct cookie_jar *jar, const struct pl *hval,
			      const char *uri)
{
	struct raw_cookie rawc;
	struct pl scheme, host, port, path;
	bool http_scheme;
	struct stored_cookie *newc = NULL;
	struct le *le;
	time_t now = time(NULL);
	int err;

	/* Preparation: parse set-cookie header and uri.
	 */
	debug("Cookie '%r' for '%s'\n", hval, uri);

	err = decode_set_cookie_header(&rawc, hval);
	if (err)
		return err;

	if (re_regex(uri, strlen(uri), "[a-z]+://[^:/]+[:]*[0-9]*[^?]+",
		     &scheme, &host, NULL, &port, &path) || scheme.p != uri)
		return EINVAL;

	http_scheme = !pl_strcmp(&scheme, "http") ||
		      !pl_strcmp(&scheme, "https");


	/* Step 1.
	 *
	 * Ignore cookie at will -- we don't.
	 */

	/* Step 2.
	 */
	newc = mem_zalloc(sizeof(*newc), stored_cookie_destructor);
	if (!newc)
		return ENOMEM;

	err = pl_dup(&newc->name, &rawc.name);
	if (err)
		goto out;
	err = pl_dup(&newc->value, &rawc.value);
	if (err)
		goto out;
	newc->created = newc->last_access = now;

	/* Step 3.
	 */
	if (pl_isset(&rawc.max_age)) {
		newc->persistent = true;
		err = parse_max_age(newc, &rawc);
		if (err)
			goto out;
	}
	else if (pl_isset(&rawc.expires)) {
		newc->persistent = true;
		err = parse_expires(newc, &rawc);
		if (err)
			goto out;
	}
	else {
		newc->persistent = false;
		newc->expires = (time_t) -1;
	}

	/* Step 4.
	 */
	if (pl_isset(&rawc.domain)) {
		/* Step 5.
		 *
		 * XXX This is where we would reject public suffixes which
		 *     we don't just yet.
		 */

		/* Step 6.
		 */
		if (!domain_match(&host, &rawc.domain)) {
			err = ENOENT;
			goto out;
		}
		newc->host_only = false;
		err = pl_dup(&newc->domain, &rawc.domain);
		if (err)
			goto out;
	}
	else {
		/* Step 6.
		 */
		newc->host_only = true;
		err = pl_dup(&newc->domain, &host);
		if (err)
			goto out;
	}

	/* Step 7.
	 */
	if (pl_isset(&rawc.path)) {
		err = pl_dup(&newc->path, &rawc.path);
		if (err)
			goto out;
	}
	else {
		err = pl_dup(&newc->path, &path);
		if (err)
			goto out;
	}

	/* Step 8.
	 */
	newc->secure = rawc.secure;

	/* Step 9.
	 */
	newc->http_only = rawc.http_only;

	/* Step 10.
	 */
	if (rawc.http_only && !http_scheme) {
		err = ENOENT;
		goto out;
	}

	/* Step 11.
	 */
	le = list_head(&jar->cookiel);
	while (le) {
		struct stored_cookie *oldc = le->data;
		struct le *next = le->next;

		if (!pl_cmp(&newc->name, &oldc->name) &&
		    !pl_casecmp(&newc->domain, &oldc->domain) &&
		    !pl_cmp(&newc->path, &oldc->path))
		{
			/* Step 11.2
			 */
			if (!http_scheme && oldc->http_only) {
				err = ENOENT;
				goto out;
			}

			/* Step 11.3
			 */
			newc->created = oldc->created;

			/* Step 11.4
			 */
			mem_deref(oldc);
		}
		else if (oldc->expires < now) {
			mem_deref(oldc);
		}
		le = next;
	}

	/* Step 12.
	 */
	list_append(&jar->cookiel, &newc->le, newc);
	if (jar->store)
		save_cookies(jar);
	dump_stored_cookie(newc);
	debug(": added.\n");

 out:
	if (err) {
		debug(": %m\n", err);
		mem_deref(newc);
	}
	return 0;
}


int cookie_jar_handle_response(struct cookie_jar *jar, const char *uri,
			       const struct http_msg *msg)
{
	struct le *le;
	int err;

	if (!jar || !uri || !msg)
		return EINVAL;

	LIST_FOREACH(&msg->hdrl, le) {
		struct http_hdr *hdr = le->data;

		if (!pl_strcasecmp(&hdr->name, "set-cookie")) {
			err = process_set_cookie(jar, &hdr->val, uri);
			if (err && err != ENOENT)
				return err;
		}
	}

	return 0;
}


const struct list *cookie_jar_list(const struct cookie_jar *jar)
{
	return jar ? &jar->cookiel : NULL;
}
