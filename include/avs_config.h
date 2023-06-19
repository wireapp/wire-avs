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

/*
 * Call config
 */

struct call_config {
	struct zapi_ice_server *iceserverv;
	size_t iceserverc;
	struct zapi_ice_server *sftserverv;
	size_t sftserverc;
	struct zapi_ice_server *sftservers_allv;
	size_t sftservers_allc;
	struct zapi_ice_server *sfticeserverv;
	size_t sfticeserverc;
	bool is_federating;
};


typedef int (config_req_h)(void *arg);
typedef void (config_update_h)(struct call_config *config, void *arg);

struct config;

struct config_update_elem {
	config_update_h *updh;
	void *arg;

	/* Internal use */
	struct config *cfg;
	struct le le;
};


int  config_alloc(struct config **cfgp,
		  config_req_h *reqh, config_update_h *updh, void *arg);
int  config_update(struct config *cfg, int err,
		   const char *conf_json, size_t len);
int  config_start(struct config *cfg);
void config_stop(struct config *cfg);
int  config_request(struct config *cfg);
int  config_register_update_handler(struct config_update_elem *upe, struct config *cfg);
int  config_unregister_update_handler(struct config_update_elem *upe);
int  config_unregister_all_updates(struct config *cfg, void *arg);

struct zapi_ice_server *config_get_iceservers(struct config *cfg,
					      size_t *count);

struct zapi_ice_server *config_get_sftservers(struct config *cfg,
					      size_t *count);

struct zapi_ice_server *config_get_sftservers_all(struct config *cfg,
						  size_t *count);

struct zapi_ice_server *config_get_sfticeservers(struct config *cfg,
						 size_t *count);

bool config_is_federating(struct config *cfg);

