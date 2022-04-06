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
#include <avs.h>
#include "system.h"

struct config *config = NULL;
static struct msystem *msys = NULL;
static const char *sft_url = NULL;

static void msys_mute_handler(bool muted, void *arg)
{
}

static int config_req_handler(void *arg)
{
	struct json_object *jobj = jobj;
	char *json_str = NULL;
	struct json_object *jsfts;
	struct json_object *jsfts_all;
	struct json_object *jurls;
	struct json_object *jsft;
	struct json_object *jurl;
	int err = 0;
	
	jobj = json_object_new_object();
	jsfts = json_object_new_array();
	if (!jobj || !jsfts) {
		err = ENOMEM;
		goto out;
	}

	jsfts_all = json_object_new_array();
	if (!jsfts) {
		err = ENOMEM;
		goto out;
	}

	jurls = json_object_new_array();
	jsft = jzon_alloc_object();
	jurl = json_object_new_string(sft_url);
	if (!jurls || !jsft || !jurl) {
		err = ENOMEM;
		goto out;
	}
	json_object_array_add(jurls, jurl);
	json_object_object_add(jsft, "urls", jurls);
	json_object_array_add(jsfts, jsft);

	jurls = json_object_new_array();
	jsft = jzon_alloc_object();
	jurl = json_object_new_string(sft_url);
	if (!jurls || !jsft || !jurl) {
		err = ENOMEM;
		goto out;
	}
	json_object_array_add(jurls, jurl);
	json_object_object_add(jsft, "urls", jurls);
	json_object_array_add(jsfts_all, jsft);

	json_object_object_add(jobj, "sft_servers", jsfts);
	json_object_object_add(jobj, "sft_servers_all", jsfts_all);

	err = jzon_encode(&json_str, jobj);
	if (err)
		goto out;

	size_t len = strlen(json_str);

	config_update(config, 0, json_str, len);
out:
	mem_deref(jobj);
	mem_deref(json_str);

	return err;
}

static void config_update_handler(struct call_config *cfg,
				  void *arg)
{
}

int init_system(const char *sft)
{
	int err = 0;

	sft_url = sft;

	err = libre_init();
	if (err)
		goto out;

	log_enable_stderr(false);
	log_set_min_level(LOG_LEVEL_ERROR);

	err = avs_init(AVS_FLAG_EXPERIMENTAL | AVS_FLAG_AUDIO_TEST);
	if (err)
		goto out;		

	err = config_alloc(&config,
			   config_req_handler,
			   config_update_handler,
			   NULL);
	if (err)
		goto out;		

	err = config_start(config);
	if (err)
		goto out;

	err = msystem_get(&msys, "voe", NULL,
			  msys_mute_handler, NULL);
	if (err) {
		printf("create, cannot init msystem: %d\n", err);
		goto out;
	}

	iflow_set_alloc(peerflow_alloc);
out:
	return err;
}

static void signal_handler(int sig)
{
	re_cancel();
}

void run_main_loop(void)
{
	re_main(signal_handler);
}

struct config *system_get_config(void)
{
	return config;
}

const char *system_get_sft_url(void)
{
	return sft_url;
}

