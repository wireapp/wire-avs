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


#define EXPIRY_MIN   300  /* in seconds (5 minutes)  */
#define EXPIRY_MAX  3600  /* in seconds (60 minutes) */


struct config {
	config_update_h *updh;
	config_req_h *reqh;
	void *arg;

	struct tmr tmr;

	struct call_config config;
};


static int do_request(struct config *cfg)
{
	int err = 0;
	
	info("config(%p): sending request\n", cfg);

	tmr_cancel(&cfg->tmr);

	if (cfg->reqh)
		err = cfg->reqh(cfg->arg);

	return err;
}
	

static void tmr_handler(void *arg)
{
	struct config *cfg = arg;
	int err;

	info("config(%p): re-requesting call config\n", cfg);

	err = do_request(cfg);
	if (err) {
		warning("config: do_request failed (%m)\n", err);
	}
}


static void cfg_destructor(void *arg)
{
	struct config *cfg = arg;

	tmr_cancel(&cfg->tmr);
	mem_deref(cfg->config.iceserverv);	
}


int config_alloc(struct config **cfgp,
		 config_req_h *reqh, config_update_h *updh, void *arg)
{
	struct config *cfg;
	int err = 0;
	
	if (!cfgp)
		return EINVAL;

	cfg = mem_zalloc(sizeof(*cfg), cfg_destructor);
	if (!cfg)
		return ENOMEM;

	cfg->reqh = reqh;
	cfg->updh = updh;
	cfg->arg = arg;

	tmr_init(&cfg->tmr);

	if (err)
		mem_deref(cfg);
	else
		*cfgp = cfg;

	return err;
}


int config_update(struct config *cfg, int err,
		  const char *conf_json, size_t len)
{
	struct json_object *jobj;
	struct json_object *jices;
	uint32_t ttl = 0;

	if (!cfg || !conf_json)
		return EINVAL;

	if (err) {
		warning("config: call_config response error (%m)\n", err);
		tmr_start(&cfg->tmr, 60 * 1000, tmr_handler, cfg);

		return 0;
	}
	
	err = jzon_decode(&jobj, conf_json, len);
	if (err)
		return err;

#if 0
	jzon_dump(jobj);
#endif

	jzon_u32(&ttl, jobj, "ttl");

	info("config(%p): got ttl of %u seconds\n", cfg, ttl);

	/* apply lower and upper limits */
	ttl = min(ttl, EXPIRY_MAX);
	ttl = max(ttl, EXPIRY_MIN);

	if (0 == jzon_array(&jices, jobj, "ice_servers")) {

		cfg->config.iceserverv = mem_deref(cfg->config.iceserverv);
		err = zapi_iceservers_decode(jices,
					     &cfg->config.iceserverv,
					     &cfg->config.iceserverc);
		if (err) {
			warning("config(%p): failed to decode iceservers(%m)\n",
				cfg, err);
			goto out;
		}

		info("config(%p): got iceservers: %zu\n",
		     cfg, cfg->config.iceserverc);

		if (!cfg->config.iceserverc) {
			warning("config: got no iceservers!\n");
			err = ENOENT;
			goto out;
		}
	}

 out:
	if (err) {
		warning("config(%p): config error (%m)\n", cfg, err);
		tmr_start(&cfg->tmr, 60 * 1000, tmr_handler, cfg);
	}
	else {
		tmr_start(&cfg->tmr, ttl * 9/10 * 1000, tmr_handler, cfg);
		if (cfg->updh)
			cfg->updh(&cfg->config, cfg->arg);
	}

	mem_deref(jobj);

	return err;
}


int config_start(struct config *cfg)
{
	if (!cfg)
		return EINVAL;

	return do_request(cfg);
}


void config_stop(struct config *cfg)
{
	if (!cfg)
		return;

	tmr_cancel(&cfg->tmr);
}


struct zapi_ice_server *config_get_iceservers(struct config *cfg,
					      size_t *count)
{
	if (!cfg || cfg->config.iceserverc == 0)
		return NULL;

	*count = cfg->config.iceserverc;

	return cfg->config.iceserverv;
}
