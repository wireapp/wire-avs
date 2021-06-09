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

#include <ctype.h>
#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_turn.h"


int stun_uri_encode(struct re_printf *pf, const struct stun_uri *uri)
{
	const char *scheme;
	uint16_t dport;
	int err;

	if (!uri)
		return 0;

	dport = uri->secure ? 5349 : 3478;

	switch (uri->scheme) {

	case STUN_SCHEME_STUN:
		scheme = "stun";
		break;

	case STUN_SCHEME_TURN:
		scheme = "turn";
		break;

	default:
		return EINVAL;
	}

	err = re_hprintf(pf, "%s%s:%j", scheme, uri->secure ? "s" : "",
			 &uri->addr);
	if (err)
		return err;

	if (sa_port(&uri->addr) != dport) {
		err |= re_hprintf(pf, ":%u", sa_port(&uri->addr));
	}

	if (uri->proto == IPPROTO_TCP)
		err |= re_hprintf(pf, "?transport=tcp");

	return err;
}


int stun_uri_decode(struct stun_uri *stun_uri, const char *str)
{
	struct uri uri;
	struct pl pl_uri;
	uint16_t port;
	struct pl transp;
	int err;

	if (!stun_uri || !str)
		return EINVAL;

	pl_set_str(&pl_uri, str);

	err = uri_decode(&uri, &pl_uri);
	if (err) {
		warning("cannot decode URI (%r)\n", &pl_uri);
		return err;
	}

	if (0 == pl_strcasecmp(&uri.scheme, "stun") ||
	    0 == pl_strcasecmp(&uri.scheme, "stuns")) {

		stun_uri->scheme = STUN_SCHEME_STUN;
	}
	else if (0 == pl_strcasecmp(&uri.scheme, "turn") ||
		 0 == pl_strcasecmp(&uri.scheme, "turns")) {

		stun_uri->scheme = STUN_SCHEME_TURN;
	}
	else {
		warning("unsupported stun scheme (%r)\n", &uri.scheme);
		return ENOTSUP;
	}

	stun_uri->secure = 's' == tolower(uri.scheme.p[uri.scheme.l-1]);

	if (uri.port)
		port = uri.port;
	else if (stun_uri->secure)
		port = 5349;
	else
		port = 3478;

	if (0 == re_regex(str, strlen(str), "?transport=[a-z]+", &transp)) {

		if (0 == pl_strcasecmp(&transp, "udp"))
			stun_uri->proto = IPPROTO_UDP;
		else if (0 == pl_strcasecmp(&transp, "tcp"))
			stun_uri->proto = IPPROTO_TCP;
		else {
			warning("unsupported stun transport (%r)\n", &transp);
			return ENOTSUP;
		}

	}
	else {
		stun_uri->proto = IPPROTO_UDP;
	}

	pl_strcpy(&uri.host, stun_uri->host, sizeof(stun_uri->host));

	stun_uri->port = port;
	err = sa_set(&stun_uri->addr, &uri.host, port);
	if (err)
		return err;
	
	return 0;
}
