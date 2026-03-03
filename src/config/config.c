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
	uint64_t expires_ts;
	bool is_updating;

	struct call_config config;

	struct list updl; /* list of update handlers */
};


static int do_request(struct config *cfg)
{
	int err = 0;
	
	info("config(%p): sending request\n", cfg);

	tmr_cancel(&cfg->tmr);

	if (cfg->reqh) {
		cfg->is_updating = true;
		err = cfg->reqh(cfg->arg);
	}

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
	mem_deref(cfg->config.sftserverv);	
	mem_deref(cfg->config.sftservers_allv);	
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
	struct json_object *jsfts;
	uint32_t ttl = 0;

	if (!cfg || !conf_json)
		return EINVAL;

	cfg->is_updating = false;

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

	cfg->expires_ts = tmr_jiffies() + (ttl * 1000);

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

	if (0 == jzon_array(&jsfts, jobj, "sft_servers")) {
		size_t i;

		cfg->config.sftserverv = mem_deref(cfg->config.sftserverv);
		err = zapi_iceservers_decode(jsfts,
					     &cfg->config.sftserverv,
					     &cfg->config.sftserverc);
		if (err) {
			warning("config(%p): failed to decode sftservers(%m)\n",
				cfg, err);
			goto out;
		}

		info("config(%p): got sftservers: %zu\n",
		     cfg, cfg->config.sftserverc);

		for(i = 0; i < cfg->config.sftserverc; ++i) {
			struct zapi_ice_server *sft = &cfg->config.sftserverv[i];

			info("config(%p): sft(%d): %s user %s cred %s\n",
			     cfg,
			     i,
			     sft->url,
			     sft->username,
			     sft->credential);
		}

		if (!cfg->config.sftserverc) {
			warning("config: got no sftservers!\n");
			err = ENOENT;
			goto out;
		}
	}

	if (0 == jzon_array(&jsfts, jobj, "sft_servers_all")) {
		size_t i;

		cfg->config.sftservers_allv = mem_deref(cfg->config.sftservers_allv);
		err = zapi_iceservers_decode(jsfts,
					     &cfg->config.sftservers_allv,
					     &cfg->config.sftservers_allc);
		if (err) {
			warning("config(%p): failed to decode sftservers_all(%m)\n",
				cfg, err);
			goto out;
		}

		info("config(%p): got sftservers_all: %zu\n",
		     cfg, cfg->config.sftservers_allc);

		for(i = 0; i < cfg->config.sftservers_allc; ++i) {
			struct zapi_ice_server *sft = &cfg->config.sftservers_allv[i];

			info("config(%p): sft_all(%d): %s user %s cred %s\n",
			     cfg,
			     i,
			     sft->url,
			     sft->username,
			     sft->credential);
		}

		if (!cfg->config.sftservers_allc) {
			warning("config: got no sftservers_all!\n");
			err = ENOENT;
			goto out;
		}
	}

	if (0 != jzon_bool(&cfg->config.is_federating, jobj, "is_federating")) {
		cfg->config.is_federating = cfg->config.sftservers_allc > 0;
		info("config(%p): setting federating: %s from sftservers_all: %zu\n",
		     cfg,
		     cfg->config.is_federating ? "YES" : "NO",
		     cfg->config.sftservers_allc);
	}
	else
		info("config(%p): setting federating: %s from json\n",
		     cfg,
		     cfg->config.is_federating ? "YES" : "NO");

	if (0 == jzon_array(&jsfts, jobj, "sft_ice_servers")) {
		size_t i;

		cfg->config.sfticeserverv = mem_deref(cfg->config.sfticeserverv);
		err = zapi_iceservers_decode(jsfts,
					     &cfg->config.sfticeserverv,
					     &cfg->config.sfticeserverc);
		if (err) {
			warning("config(%p): failed to decode sftservers(%m)\n",
				cfg, err);
			goto out;
		}

		info("config(%p): got sfticeservers: %zu\n",
		     cfg, cfg->config.sfticeserverc);
		for(i = 0; i < cfg->config.sfticeserverc; ++i) {
			struct zapi_ice_server *sft = &cfg->config.sfticeserverv[i];

			info("config(%p): sft-TURN(%d): %s\n", cfg, i, sft->url);
		}

		if (!cfg->config.sfticeserverc)
			warning("config(%p): got no sfticeservers!\n", cfg);
	}
	
 out:
	if (err) {
		warning("config(%p): config error (%m)\n", cfg, err);
		tmr_start(&cfg->tmr, 60 * 1000, tmr_handler, cfg);
	}
	else {
		struct le *le;
		tmr_start(&cfg->tmr, ttl * 9/10 * 1000, tmr_handler, cfg);
		if (cfg->updh)
			cfg->updh(&cfg->config, cfg->arg);
		le = cfg->updl.head;
		while(le) {
			struct config_update_elem *upel = le->data;

			le = le->next;

			if (upel->updh)
				upel->updh(&upel->cfg->config, upel->arg);
		}
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

struct zapi_ice_server *config_get_sftservers(struct config *cfg,
					      size_t *count)
{
	if (!cfg || cfg->config.sftserverc == 0)
		return NULL;

	*count = cfg->config.sftserverc;

	return cfg->config.sftserverv;
}

struct zapi_ice_server *config_get_sftservers_all(struct config *cfg,
						  size_t *count)
{
	if (!cfg || cfg->config.sftservers_allc == 0)
		return NULL;

	*count = cfg->config.sftservers_allc;

	return cfg->config.sftservers_allv;
}

struct zapi_ice_server *config_get_sfticeservers(struct config *cfg,
						 size_t *count)
{
	if (!cfg || cfg->config.sfticeserverc == 0)
		return NULL;

	*count = cfg->config.sfticeserverc;

	return cfg->config.sfticeserverv;
}

bool config_is_federating(struct config *cfg)
{
	if (!cfg)
		return false;

	return cfg->config.is_federating;
}

int config_request(struct config *cfg)
{
	int err;

	if (!cfg)
		return EINVAL;

	err = do_request(cfg);

	return err;
}

int config_register_update_handler(struct config_update_elem *upe, struct config *cfg)
{
	if (!upe || !cfg)
		return EINVAL;

	if (!upe->updh)
		return ENOSYS;

	upe->cfg = cfg;
	
	list_append(&cfg->updl, &upe->le, upe);

	return 0;
}

int config_unregister_update_handler(struct config_update_elem *upe)
{
	if (!upe)
		return EINVAL;

	if (!upe->cfg)
		return ENOSYS;

	list_unlink(&upe->le);

	return 0;
}

int config_unregister_all_updates(struct config *cfg, void *arg)
{
	struct le *le;

	if (!cfg || !arg)
		return EINVAL;

	le = cfg->updl.head;
	while(le) {
		struct config_update_elem *upel = le->data;

		le = le->next;

		if (upel->arg == arg) {
			config_unregister_update_handler(upel);
		}
	}

	return 0;
}

bool config_needs_update(struct config *cfg)
{
	uint64_t now = tmr_jiffies();
	
	return cfg->is_updating || (now >= cfg->expires_ts);
}
