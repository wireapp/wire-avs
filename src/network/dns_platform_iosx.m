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

#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>

#include "re.h"
#include "avs.h"
#include "dns_platform.h"


static int lookup_done(struct sa *srv,
		       CFHostRef host,
		       const CFStreamError *serr,
		       struct dns_lookup_entry *lent)
{
	CFArrayRef addrs;
	CFDataRef addr;
	struct sockaddr *sa;
	int err = 0;

	if (serr && serr->error) {
		warning("dns: host_client lookup failed: (%d)%d\n",
			serr->domain, serr->error);
		err = serr->error;
		goto out;
	}

	addrs = CFHostGetAddressing(host, NULL);
	addr = (CFDataRef)CFArrayGetValueAtIndex(addrs, 0);
	sa = (struct sockaddr *)CFDataGetBytePtr(addr);
	err = sa_set_sa(srv, sa);
	if (err)
		warning("dns: failed to set address: %m\n", err);

	sa_set_port(srv, 3478);

 out:
	return err;
}

int dns_platform_init(void *arg)
{
	(void)arg;

	return 0;
}


void dns_platform_close(void)
{
}


int dns_platform_lookup(struct dns_lookup_entry *lent, struct sa *srv)
{
	CFHostRef host;
	CFStringRef cfurl;
	CFStreamError serr;
	Boolean ok = false;

	cfurl = CFStringCreateWithCString(NULL, lent->host,
					  kCFStringEncodingUTF8);
	host = CFHostCreateWithName(kCFAllocatorDefault, cfurl);
	if (!host) {
		serr.error = ENOSYS;
		goto out;
	}

	ok = CFHostStartInfoResolution(host, kCFHostAddresses, &serr);

 out:
	return lookup_done(srv, host, ok ? NULL : &serr, lent);
}


