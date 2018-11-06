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
};

typedef int (config_req_h)(void *arg);
typedef void (config_update_h)(struct call_config *config, void *arg);

struct config;

int  config_alloc(struct config **cfgp,
		  config_req_h *reqh, config_update_h *updh, void *arg);
int  config_update(struct config *cfg, int err,
		   const char *conf_json, size_t len);
int  config_start(struct config *cfg);
void config_stop(struct config *cfg);

struct zapi_ice_server *config_get_iceservers(struct config *cfg,
					      size_t *count);

