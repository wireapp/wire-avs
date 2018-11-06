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
#include <openssl/crypto.h>
#include <re.h>
#include <sodium.h>

#include "avs_log.h"
#include "avs_base.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_flowmgr.h"
#include "avs_version.h"


#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re/re_dbg.h>



static struct {
	bool inited;
	uint64_t flags;

	char *token;
} base = {
	.inited = false,
	.flags = 0,
	.token = NULL,
};


static void debug_handler(int level, const char *p, size_t len, void *arg)
{
	(void)arg;
	(void)level;

	switch (level) {

	case DBG_EMERG:
	case DBG_ALERT:
	case DBG_CRIT:
	case DBG_ERR:
		error("%b\n", p, len);
		break;

	case DBG_WARNING:
		warning("%b\n", p, len);
		break;

	case DBG_NOTICE:
	case DBG_INFO:
		info("%b\n", p, len);
		break;

	case DBG_DEBUG:
	default:
		debug("%b\n", p, len);
		break;
	}
}


int avs_init(uint64_t flags)
{
	if (sodium_init() == -1) {
		warning("init: could not init libsodium\n");
		return ENOSYS;
	}

	base.flags = flags;
	base.inited = true;

	dbg_handler_set(debug_handler, NULL);

	info("AVS inited with flags=0x%llx [%s]\n", flags, avs_version_str());

	info("init: using async polling method '%s'\n",
	     poll_method_name(poll_method_best()));

	avs_print_versions();

	avs_print_network();

	return 0;
}


int avs_start(const char *token)
{
	int err;

	info("avs_start: token=%s\n", token);
	
	err = str_dup(&base.token, token);
	if (err)
		goto out;

	err = flowmgr_start();
	if (err)
		goto out;

 out:
	return err;
}


void avs_close(void)
{
	base.inited = false;

	base.token = mem_deref(base.token);
}


uint64_t avs_get_flags(void)
{
	return base.flags;
}


void avs_print_versions(void)
{
	info("init: avs:      %s\n", avs_version_str());
	info("init: libre:    %s\n", sys_libre_version_get());
	info("init: openssl:  %s (%s)\n",
	     SSLeay_version(SSLEAY_VERSION),
	     SSLeay_version(SSLEAY_PLATFORM));
	info("init: sodium:   %s\n", sodium_version_string());
}


static bool ifaddr_handler(const char *ifname, const struct sa *sa,
			   void *arg)
{
	char buf[256] = "???";

	net_if_getname(buf, sizeof(buf), sa_af(sa), sa);

	info("init: interface: %-8s    %-26j  resolved='%s'\n", ifname, sa, buf);

	return false;
}


void avs_print_network(void)
{
	info("init: network info:\n");

	info("init: Interfaces:\n");
	net_if_apply(ifaddr_handler, 0);
	info("init\n");
}
