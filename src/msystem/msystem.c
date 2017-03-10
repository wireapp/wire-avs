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

#include "re.h"
#include "avs.h"
#include "avs_voe.h"
#include "avs_vie.h"

struct msystem {
	struct msystem_config config;
	struct call_config *call_config;
	bool inited;
	bool started;
	struct tls *dtls;
	struct mqueue *mq;
	struct tmr vol_tmr;
	char *name;
	bool using_voe;
	bool loopback;
	bool privacy;
	bool cbr;
	char ifname[256];

	struct list aucodecl;
	struct list vidcodecl;
};


static const char *cipherv[] = {

	"ECDHE-RSA-AES128-GCM-SHA256",
	"ECDHE-ECDSA-AES128-GCM-SHA256",
	"ECDHE-RSA-AES256-GCM-SHA384",
	"ECDHE-ECDSA-AES256-GCM-SHA384",
	"DHE-RSA-AES128-GCM-SHA256",
	"DHE-DSS-AES128-GCM-SHA256",
	"ECDHE-RSA-AES128-SHA256",
	"ECDHE-ECDSA-AES128-SHA256",
	"ECDHE-RSA-AES128-SHA",
	"ECDHE-ECDSA-AES128-SHA",
	"ECDHE-RSA-AES256-SHA384",
	"ECDHE-ECDSA-AES256-SHA384",
	"ECDHE-RSA-AES256-SHA",
	"ECDHE-ECDSA-AES256-SHA",
	"DHE-RSA-AES128-SHA256",
	"DHE-RSA-AES128-SHA",
	"DHE-DSS-AES128-SHA256",
	"DHE-RSA-AES256-SHA256",
	"DHE-DSS-AES256-SHA",
	"DHE-RSA-AES256-SHA",
	"ECDHE-RSA-AES128-CBC-SHA",

};

static struct msystem *g_msys = NULL;


static void msystem_destructor(void *data)
{
	struct msystem *msys = data;

	if (msys->name) {
		if (streq(msys->name, "audummy"))
			audummy_close();
		else if (streq(msys->name, "voe")) {
			vie_close();
			voe_close();
		}
	}

	tmr_cancel(&msys->vol_tmr);

	msys->mq = mem_deref(msys->mq);
	msys->dtls = mem_deref(msys->dtls);
	msys->name = mem_deref(msys->name);
	msys->call_config = mem_deref(msys->call_config);
	
	dce_close();

	msys->inited = false;

	if (g_msys == msys)
		g_msys = NULL;
}


/* This is just a dummy handler fo waking up re_main() */
static void wakeup_handler(int id, void *data, void *arg)
{
	(void)id;
	(void)data;
	(void)arg;
}


static int msystem_init(struct msystem **msysp, const char *msysname,
			enum tls_keytype cert_type,
			struct msystem_config *config)
{
	struct msystem *msys;
	int err;

	if (!msysp)
		return EINVAL;

	msys = mem_zalloc(sizeof(*msys), msystem_destructor);
	if (!msys)
		return ENOMEM;

	err = mqueue_alloc(&msys->mq, wakeup_handler, NULL);
	if (err) {
		warning("flowmgr: failed to create mqueue (%m)\n", err);
		goto out;
	}

	err = tls_alloc(&msys->dtls, TLS_METHOD_DTLS, NULL, NULL);
	if (err) {
		warning("flowmgr: failed to create DTLS context (%m)\n",
			err);
		goto out;
	}

	err = cert_enable_ecdh(msys->dtls);
	if (err)
		goto out;

	info("flowmgr: setting %zu ciphers for DTLS\n", ARRAY_SIZE(cipherv));
	err = tls_set_ciphers(msys->dtls, cipherv, ARRAY_SIZE(cipherv));
	if (err)
		goto out;

	{
		uint64_t t1, t2;

		t1 = tmr_jiffies();

		switch (cert_type) {

		case TLS_KEYTYPE_EC:
			info("flowmgr: generating ECDSA certificate\n");
			err = cert_tls_set_selfsigned_ecdsa(msys->dtls,
							    "prime256v1");
			if (err) {
				warning("flowmgr: failed to generate ECDSA"
					" certificate"
					" (%m)\n", err);
				goto out;
			}
			break;

		default:
			warning("flowmgr: invalid cert type\n");
			err = ENOTSUP;
			goto out;
		}

		t2 = tmr_jiffies();

		info("flowmgr: generate certificate took %d ms\n",
		     (int)(t2-t1));
	}

	tls_set_verify_client(msys->dtls);

	err = tls_set_srtp(msys->dtls, "SRTP_AES128_CM_SHA1_80");
	if (err) {
		warning("flowmgr: failed to enable SRTP profile (%m)\n",
			err);
		goto out;
	}

	tmr_init(&msys->vol_tmr);

	err = str_dup(&msys->name, msysname);
	if (streq(msys->name, "audummy"))
		err = audummy_init(&msys->aucodecl);
	else if (streq(msys->name, "voe")) {
		err = voe_init(&msys->aucodecl);
		if (err) {
			warning("flowmgr: voe init failed (%m)\n", err);
			goto out;
		}

		err = vie_init(&msys->vidcodecl);
		if (err) {
			warning("flowmgr: vie init failed (%m)\n", err);
			goto out;
		}

		msys->using_voe = true;
	}
	else {
		warning("flowmgr: media-system not available (%s)\n",
			msys->name);
		err = EINVAL;
	}

	if (err)
		goto out;

	info("msystem: successfully initialized\n");

	msys->inited = true;

	if (config) {
		msys->config = *config;

		if (config->data_channel) {

			err = dce_init();
			if (err)
				goto out;
		}
	}
	
	g_msys = msys;

 out:
	if (err)
		mem_deref(msys);
	else
		*msysp = msys;

	return err;
}


int msystem_get(struct msystem **msysp, const char *msysname,
		enum tls_keytype cert_type, struct msystem_config *config)
{
	if (!msysp)
		return EINVAL;
	
	if (g_msys) {
		*msysp = mem_ref(g_msys);
		return 0;
	}


	return msystem_init(msysp, msysname, cert_type, config);
}



struct tls *msystem_dtls(struct msystem *msys)
{
	return msys ? msys->dtls : NULL;
}


struct list *msystem_aucodecl(struct msystem *msys)
{
	return msys ? &msys->aucodecl : NULL;
}


struct list *msystem_vidcodecl(struct msystem *msys)
{
	return msys ? &msys->vidcodecl : NULL;
}


bool msystem_get_loopback(struct msystem *msys)
{
	return msys ? msys->loopback : false;
}


bool msystem_get_privacy(struct msystem *msys)
{
	return msys ? msys->privacy : false;
}


const char *msystem_get_interface(struct msystem *msys)
{
	return msys ? msys->ifname : NULL;
}


void msystem_start(struct msystem *msys)
{
	if (!msys)
		return;

	msys->started = true;
}


void msystem_stop(struct msystem *msys)
{
	if (!msys)
		return;

	msys->started = false;
}


bool msystem_is_started(struct msystem *msys)
{
	return msys ? msys->started : false;
}


int msystem_push(struct msystem *msys, int op, void *arg)
{
	if (!msys->mq)
		return ENOSYS;

	return mqueue_push(msys->mq, op, arg);	
}


bool msystem_is_using_voe(struct msystem *msys)
{
	return msys ? msys->using_voe : false;
}


void msystem_enable_loopback(struct msystem *msys, bool enable)
{
	if (!msys)
		return;

	msys->loopback = enable;	
}


void msystem_enable_privacy(struct msystem *msys, bool enable)
{
	if (!msys)
		return;

	msys->privacy = enable;	
}

void msystem_enable_cbr(struct msystem *msys, bool enable)
{
	if (!msys)
		return;
    
	voe_enable_cbr(enable);
    
	msys->cbr = enable;
}

bool msystem_have_cbr(const struct msystem *msys)
{
	return msys ? voe_have_cbr() : false;
}

void msystem_set_ifname(struct msystem *msys, const char *ifname)
{
	if (!msys)
		return;

	str_ncpy(msys->ifname, ifname, sizeof(msys->ifname));
}
			  

bool msystem_is_initialized(struct msystem *msys)
{
	return msys ? msys->inited : false;
}


int msystem_enable_datachannel(struct msystem *msys, bool enable)
{
	int err;

	if (!msys)
		return EINVAL;

	msys->config.data_channel = enable;

	if (enable) {

		err = dce_init();
		if (err)
			return err;
	}
	else {
		dce_close();
	}

	return 0;
}


bool msystem_have_datachannel(const struct msystem *msys)
{
	return msys ? msys->config.data_channel : false;
}


int msystem_set_call_config(struct msystem *msys, struct call_config *cfg)
{
	if (!msys || !cfg)
		return EINVAL;

	if (msys->call_config)
		msys->call_config = mem_deref(msys->call_config);
	msys->call_config = mem_zalloc(sizeof(*cfg), NULL);
	if (!msys->call_config)
		return ENOMEM;

	*msys->call_config = *cfg;

	return 0;
}


struct call_config *msystem_get_call_config(const struct msystem *msys)
{
	return msys ? msys->call_config : NULL;
}
	
