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

#include <re/re.h>
#include "avs.h"
#include "avs_wcall.h"
#include "flowmgr.h"



#define EXPIRY_MIN   300  /* in seconds (5 minutes)  */
#define EXPIRY_MAX  3600  /* in seconds (60 minutes) */


static int do_request(struct flowmgr *fm);

static void tmr_handler(void *arg)
{
	struct flowmgr *fm = arg;

	info("flowmgr: re-requesting call config..\n");

	do_request(fm);
}


static void call_config_resp_handler(int status, struct rr_resp *rr,
				     struct json_object *jobj, void *arg)
{
	struct flowmgr *fm = arg;
	struct json_object *jices;
	struct call_config *config = &fm->config.cfg;
	uint32_t ttl = 0;
	size_t srvc;
	int err = 0;

	if (status < 200 || status >= 300) {
		warning("flowmgr(%p): call_config_resp_handler: failed:"
			" status = %d\n",
			fm, status);

		err = EPROTO;
		goto out;
	}

#if 0
	jzon_dump(jobj);
#endif

	jzon_u32(&ttl, jobj, "ttl");

	info("flowmgr: config: got ttl of %u seconds\n", ttl);

	/* apply lower and upper limits */
	ttl = min(ttl, EXPIRY_MAX);
	ttl = max(ttl, EXPIRY_MIN);

	if (0 == jzon_array(&jices, jobj, "ice_servers")) {

		srvc = ARRAY_SIZE(config->iceserverv);
		err = zapi_iceservers_decode(jices, config->iceserverv, &srvc);
		if (err) {
			warning("could not decode iceservers (%m)\n", err);
			goto out;
		}

		config->iceserverc = srvc;

		info("flowmgr: got iceservers: %zu\n", srvc);

		if (!srvc) {
			warning("flowmgr: config: got no iceservers!\n");
			err = ENOENT;
			goto out;
		}
	}

	config->early_dtls = false;
	jzon_bool(&config->early_dtls, jobj, "early_dtls");

	/* Features config */
	{
		struct json_object *jfeat;

		config->features.ver_one_to_one = 2.0;
		config->features.ver_multiparty = 2.0;		
		
		err = jzon_object(&jfeat, jobj, "features");
		if (err) {
			info("config: could not find features\n");
			err = 0;
		}
		else {
			const char *val;
			val = jzon_str(jfeat, "protocol_version_1to1");
			if (val) {
				config->features.ver_one_to_one =
					strtod(val, NULL);
			}
			else {
				warning("config: protocol_version_1to1 "
					"failed %m\n", err);
			}
			val = jzon_str(jfeat, "protocol_version_group");
			if (val) {
				config->features.ver_multiparty =
					strtod(val, NULL);
			}
			else {
				warning("config: protocol_version_group "
					"failed %m\n", err);
			}
			
		}

		info("flowmgr: config: protocol_1to1: %f protocol_group: %f\n",
		     (float)config->features.ver_one_to_one,
		     (float)config->features.ver_multiparty);
	}
 out:
        fm->config.rr = NULL;

	if (err) {
		warning("flowmgr: call_config response error (%m)\n", err);
		tmr_start(&fm->config.tmr, 60 * 1000,
			  tmr_handler, fm);
	}
	else {
		tmr_start(&fm->config.tmr, ttl * 9/10 * 1000,
			  tmr_handler, fm);
	}

	if (!err && fm->config.configh)
		fm->config.configh(&fm->config.cfg, fm->config.arg);
}


static int do_request(struct flowmgr *fm)
{
	struct rr_resp *rr = NULL;
	char url[256];
	int err;

	info("flowmgr: sending request to %s\n", CREQ_CONFIG);

	tmr_cancel(&fm->config.tmr);

	err = rr_alloc(&rr, fm, NULL, call_config_resp_handler, fm);
	if (err) {
		warning("flowmgr(%p): flowmgr_get_iceservers: "
			"cannot allocate rest response (%m)\n",
			fm, err);
		return err;
	}
        fm->config.rr = rr;

	re_snprintf(url, sizeof(url), CREQ_CONFIG);
	err = flowmgr_send_request(fm, NULL, rr, url, HTTP_GET, NULL, NULL);
	if (err) {
		warning("flowmgr(%p): flowmgr_get_iceservers: rest_req failed"
			" (%m)\n", fm, err);
		goto out;
	}

 out:
	return err;
}


/* NOTE: should only be called after successful login */
int flowmgr_config_start(struct flowmgr *fm)
{
	int err = 0;
	
	if (!fm)
		return EINVAL;

	err = do_request(fm);
	if (err)
		return err;

	return err;
}


int flowmgr_config_starth(struct flowmgr *fm, call_config_h *configh, void *arg)
{
	int err = 0;
	
	if (!fm)
		return EINVAL;

	fm->config.configh = configh;
	fm->config.arg     = arg;

	err = do_request(fm);
	if (err)
		return err;

	return err;
}


void flowmgr_config_stop(struct flowmgr *fm)
{
	if (!fm)
		return;

        if (fm->config.rr)
		rr_cancel(fm->config.rr);

	tmr_cancel(&fm->config.tmr);
}


struct zapi_ice_server *flowmgr_config_iceservers(struct flowmgr *fm,
						  size_t *count)
{
	if (!fm || !fm->config.cfg.iceserverc)
		return NULL;

	*count = fm->config.cfg.iceserverc;

	return fm->config.cfg.iceserverv;
}
