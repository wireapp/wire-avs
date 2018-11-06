/*
 * Wire
 * Copyright (C) 2018 Wire Swiss GmbH
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
#include "avs_msystem.h"
#include "avs_network.h"
#include "dns_platform.h"

#include <netdb.h>

int dns_platform_init(void *arg)
{
	return 0;
}

int dns_platform_lookup(struct dns_lookup_entry *lent, struct sa *srv)
{
	struct hostent *hent;
	int err = 0;

	hent = gethostbyname(lent->host);
	if (!hent) {
		err = errno;
		goto out;
	}
	sa_init(srv, hent->h_addrtype);
	switch(hent->h_addrtype) {
	case AF_INET:
		sa_set_in(srv,
			  ntohl(*(uint32_t *)((void *)hent->h_addr)),
			  34768);
		break;

	case AF_INET6:
		sa_set_in6(srv, (uint8_t *)hent->h_addr, 34768);
		break;

	default:
		err = EINVAL;
		break;
	}

 out:	
	return err;
}

void dns_platform_close(void)
{
}
