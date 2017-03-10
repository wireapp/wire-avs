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
#include "avs_network.h"


/*
 * Translate an IPv4-address to a NAT64-mapped IPv6-address
 *
 *   input:  1.2.3.4
 *   output: 64:ff9b::1.2.3.4
 *
 */
int sa_translate_nat64(struct sa *sa6, const struct sa *sa4)
{
	char buf[256];
	uint16_t port;

	if (!sa6 || !sa4)
		return EINVAL;

	if (sa_af(sa4) != AF_INET)
		return EAFNOSUPPORT;

	if (re_snprintf(buf, sizeof(buf), "64:ff9b::%j", sa4) < 0)
		return ENOMEM;

	port = sa_port(sa4);

	return sa_set_str(sa6, buf, port);
}


bool sa_ipv4_is_private(const struct sa *sa)
{
	static const struct {
		uint32_t addr;
		uint32_t mask;
	} netv[] = {
		{ 0x0a000000, 0xff000000u},  /* 10.0.0.0/8     */
		{ 0xac100000, 0xfff00000u},  /* 172.16.0.0/12  */
		{ 0xc0a80000, 0xffff0000u},  /* 192.168.0.0/16 */
	};
	uint32_t addr = sa_in(sa);
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(netv); i++) {

		if ((addr & netv[i].mask) == netv[i].addr)
			return true;
	}

	return false;
}
