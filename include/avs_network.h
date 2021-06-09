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


/* Network utility functions */

struct sa;

int sa_translate_nat64(struct sa *sa6, const struct sa *sa4);
bool sa_ipv4_is_private(const struct sa *sa);

int dns_get_servers(char *domain, size_t dsize, struct sa *nsv, uint32_t *n);

/* Platform native lookup */
typedef void (dns_lookup_h)(int err, const struct sa *srv, void *arg);
int  dns_init(void *arg);
void dns_close(void);
int  dns_lookup(const char *url, dns_lookup_h *lookuph, void *arg);
