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

#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_jzon.h"
#include "avs_uuid.h"
#include "avs_zapi.h"


static int zapi_iceservers_encode(struct json_object *jobj,
				  const struct zapi_ice_server *srvv,
				  size_t srvc)
{
	struct json_object *jices;
	size_t i;
	int r, err;

	if (!jobj || !srvv || !srvc)
		return EINVAL;

	jices = json_object_new_array();
	if (!jices)
		return ENOMEM;

	for (i=0; i<srvc; i++) {
		struct json_object *jice;
		const struct zapi_ice_server *srv = &srvv[i];

		err = jzon_creatf(&jice, "sss",
				  "urls",       srv->url,
				  "username",   srv->username,
				  "credential", srv->credential);
		if (err)
			return err;

		r = json_object_array_add(jices, jice);
		if (r)
			return ENOMEM;
	}

	json_object_object_add(jobj, "ice_servers", jices);

	return 0;
}


int zapi_iceservers_decode(struct json_object *jarr,
			   struct zapi_ice_server *srvv, size_t *srvc)
{
	int i, n;

	if (!jarr || !srvv || !srvc || !*srvc)
		return EINVAL;

	if (!jzon_is_array(jarr)) {
		warning("zapi: json object is not an array\n");
		return EINVAL;
	}

	n = min( json_object_array_length(jarr), (int)*srvc);

	for (i = 0; i < n; ++i) {
		struct json_object *jice;
		struct zapi_ice_server *srv = &srvv[i];
		const char *url, *username, *credential;
		struct json_object *urls_arr;

		jice = json_object_array_get_idx(jarr, i);
		if (!jice) {
			warning("zapi: ice_servers[%d] is missing\n", i);
			return ENOENT;
		}

		if (0 == jzon_array(&urls_arr, jice, "urls")) {

			struct json_object *jurl;
			/* NOTE: we use only first server */
			jurl = json_object_array_get_idx(urls_arr, 0);
			url = json_object_get_string(jurl);
		}
		else {
			url        = jzon_str(jice, "urls");
			if (!url)
				url= jzon_str(jice, "url");
		}

		username   = jzon_str(jice, "username");
		credential = jzon_str(jice, "credential");

		if (!url) {
			warning("zapi: ice_servers[%d] is missing"
				" url\n", i);
			return EPROTO;

		}

		str_ncpy(srv->url, url, sizeof(srv->url));
		str_ncpy(srv->username, username, sizeof(srv->username));
		str_ncpy(srv->credential, credential, sizeof(srv->credential));
	}

	*srvc = i;

	return 0;
}


int zapi_flow_encode(struct json_object *jobj,
		     const struct zapi_flow *flow)
{
	int err;

	if (!jobj || !flow)
		return EINVAL;

	json_object_object_add(jobj, "active",
			       json_object_new_boolean(flow->active));
	json_object_object_add(jobj, "id",
			       json_object_new_string(flow->id));
	json_object_object_add(jobj, "sdp_step",
			       json_object_new_string(flow->sdp_step));

	if (flow->srvc) {
		err = zapi_iceservers_encode(jobj,
					     flow->srvv, flow->srvc);
		if (err)
			return err;
	}

	if (str_isset(flow->remote_user)) {

		json_object_object_add(jobj, "remote_user",
			       json_object_new_string(flow->remote_user));
	}

	return 0;
}


int zapi_flow_decode(struct json_object *jobj, struct zapi_flow *flow)
{
	struct json_object *jices = NULL;
	int err;

	if (!jobj || !flow)
		return EINVAL;

	err = jzon_bool(&flow->active, jobj, "active");
	if (err)
		return err;
	str_ncpy(flow->creator, jzon_str(jobj, "creator"),
		 sizeof(flow->creator));
	str_ncpy(flow->id, jzon_str(jobj, "id"),
		 sizeof(flow->id));
	str_ncpy(flow->sdp_step, jzon_str(jobj, "sdp_step"),
		 sizeof(flow->sdp_step));

	/* the "ice_servers" object is optional */
	if (0 == jzon_array(&jices, jobj, "ice_servers") && jices) {

		flow->srvc = ARRAY_SIZE(flow->srvv);
		err = zapi_iceservers_decode(jices, flow->srvv, &flow->srvc);
		if (err)
			return err;
	}
	else {
		flow->srvc = 0;
	}

	str_ncpy(flow->remote_user, jzon_str(jobj, "remote_user"),
		 sizeof(flow->remote_user));

	return 0;
}


int zapi_flows_encode(struct json_object *jobj,
		      const struct zapi_flow *flowv, size_t flowc)
{
	struct json_object *jflows;
	size_t i;
	int err;

	if (!jobj || !flowv || !flowc)
		return EINVAL;

	jflows = json_object_new_array();

	for (i=0; i<flowc; i++) {

		struct json_object *jflow;

		jflow = json_object_new_object();

		err = zapi_flow_encode(jflow, &flowv[i]);
		if (err)
			return err;

		json_object_array_add(jflows, jflow);
	}

	json_object_object_add(jobj, "flows", jflows);

	return 0;
}


int zapi_flows_decode(struct json_object *jobj,
		      struct zapi_flow *flowv, size_t *flowc)
{
	struct json_object *jflows;
	int i, n;
	int err;

	if (!jobj || !flowv || !flowc || !*flowc)
		return EINVAL;

	err = jzon_array(&jflows, jobj, "flows");
	if (err) {
		warning("zapi: flow_decode: no 'flows' array\n");
		return err;
	}

	n = min(json_object_array_length(jflows), (int)*flowc);

	for (i = 0; i < n; ++i) {

		err = zapi_flow_decode(json_object_array_get_idx(jflows, i),
				       &flowv[i]);
		if (err)
			return err;
	}

	*flowc = i;

	return 0;
}


int zapi_flowadd_encode(struct json_object *jobj,
			const char *convid,
			const struct zapi_flow *flowv, size_t flowc)
{
	if (!jobj || !convid || !flowv || !flowc)
		return EINVAL;

	json_object_object_add(jobj, "conversation",
			       json_object_new_string(convid));
	json_object_object_add(jobj, "type",
			       json_object_new_string("call.flow-add"));

	return zapi_flows_encode(jobj, flowv, flowc);
}


int zapi_local_sdp_encode(struct json_object *jobj, const struct zapi_local_sdp *lsdp)
{
	if (!jobj || !lsdp)
		return EINVAL;

	json_object_object_add(jobj, "type", json_object_new_string(lsdp->type));
	json_object_object_add(jobj, "sdp", json_object_new_string(lsdp->sdp));

	return 0;
}


int zapi_local_sdp_decode(struct zapi_local_sdp *lsdp, struct json_object *jobj)
{
	if (!lsdp || !jobj)
		return EINVAL;

	lsdp->type = jzon_str(jobj, "type");
	lsdp->sdp = jzon_str(jobj, "sdp");

	return 0;
}


int zapi_remote_sdp_encode(struct json_object *jobj, const struct zapi_remote_sdp *rsdp)
{
	if (!jobj || !rsdp)
		return EINVAL;

	json_object_object_add(jobj, "state",        json_object_new_string(rsdp->state));
	json_object_object_add(jobj, "sdp",          json_object_new_string(rsdp->sdp));
	json_object_object_add(jobj, "flow",         json_object_new_string(rsdp->flow));
	json_object_object_add(jobj, "conversation", json_object_new_string(rsdp->conv));
	json_object_object_add(jobj, "type",         json_object_new_string("call.remote-sdp"));

	return 0;
}


int zapi_remote_sdp_decode(struct zapi_remote_sdp *rsdp, struct json_object *jobj)
{
	if (!rsdp || !jobj)
		return EINVAL;

	if (0 != str_casecmp(jzon_str(jobj, "type"), "call.remote-sdp"))
		return EPROTO;

	rsdp->state = jzon_str(jobj, "state");
	rsdp->sdp   = jzon_str(jobj, "sdp");
	rsdp->flow  = jzon_str(jobj, "flow");
	rsdp->conv  = jzon_str(jobj, "conversation");

	return 0;
}


// Encode only 1 JSON object
int zapi_candidate_encode(struct json_object *jobj, const struct zapi_candidate *cand)
{
	if (!jobj || !cand)
		return EINVAL;

	json_object_object_add(jobj, "sdp_mid",         json_object_new_string(cand->mid));
	json_object_object_add(jobj, "sdp_mline_index", json_object_new_int(cand->mline_index));
	json_object_object_add(jobj, "sdp",             json_object_new_string(cand->sdp));

	return 0;
}


int zapi_candidate_decode(struct zapi_candidate *cand, struct json_object *jobj)
{
	int err;

	if (!cand || !jobj)
		return EINVAL;

	cand->mid = jzon_str(jobj, "sdp_mid");
	err = jzon_int(&cand->mline_index, jobj, "sdp_mline_index");
	cand->sdp = jzon_str(jobj, "sdp");

	return err;
}


int zapi_call_state_encode(struct json_object *jobj,
			   const struct zapi_call_state *cs)
{
	struct json_object *self, *inner;

	if (!jobj || !cs)
		return EINVAL;

	self = json_object_new_object();
	if (!self)
		return ENOMEM;

	inner = json_object_new_string(cs->state);
	if (inner == NULL)
		return ENOMEM;
	json_object_object_add(self, "state", inner);

	inner = json_object_new_double(cs->quality);
	if (inner == NULL)
		return ENOMEM;
	json_object_object_add(self, "quality", inner);

	json_object_object_add(jobj, "self", self);

	return 0;
}


int zapi_call_state_decode(struct zapi_call_state *cs,
			   struct json_object *jobj)
{
	struct json_object *self;
	int err;

	if (!cs || !jobj)
		return EINVAL;

	err = jzon_object(&self, jobj, "self");
	if (err)
		return err;

	cs->state = jzon_str(self, "state");
	err |= jzon_double(&cs->quality, self, "quality");

	return err;
}


int zapi_call_state_event_encode(struct json_object *jobj,
				 const struct zapi_call_state_event *cs)
{
	(void)jobj;
	(void)cs;
	return ENOSYS;  /* TODO: implement this */
}


static bool part_handler(const char *key, struct json_object *jobj,
			 void *arg)
{
	const char *user_id = key;
	struct zapi_call_state_event *cs = arg;
	struct zapi_participant *part;
	const char *state = jzon_str(jobj, "state");

	if (cs->participantc >= ARRAY_SIZE(cs->participantv))
		return true;

	part = &cs->participantv[cs->participantc++];

	str_ncpy(part->userid, user_id, sizeof(part->userid));
	str_ncpy(part->state, state, sizeof(part->state));

	return false;
}


int zapi_call_state_event_decode(struct zapi_call_state_event *cs,
				 struct json_object *jobj)
{
	struct json_object *jpart;
	struct json_object *jself;
	int err = 0;	

	if (!cs || !jobj)
		return EINVAL;

	memset(cs, 0, sizeof(*cs));

	str_ncpy(cs->convid, jzon_str(jobj, "conversation"),
		 sizeof(cs->convid));
	str_ncpy(cs->cause, jzon_str(jobj, "cause"),
		 sizeof(cs->cause));

	err = jzon_object(&jpart, jobj, "participants");
	if (err) {
		warning("zapi: call_state_event: missing 'participants'\n");
		return err;
	}

	cs->participantc = 0;
	if (jzon_apply(jpart, part_handler, cs)) {
		warning("zapi: call_state: max participants (%d)\n",
			MAX_PARTICIPANTS);
		cs->participantc = 0;
		return EOVERFLOW;
	}
		
	err = jzon_object(&jself, jobj, "self");
	if (!err && jself != NULL) {
		str_ncpy(cs->self.reason, jzon_str(jself, "reason"),
			 sizeof(cs->self.reason));
		str_ncpy(cs->self.state, jzon_str(jself, "state"),
			 sizeof(cs->self.state));
	}

	err = jzon_u32(&cs->sequence, jobj, "sequence");
	if (err)
		return err;

	str_ncpy(cs->type, jzon_str(jobj, "type"), sizeof(cs->type));
	str_ncpy(cs->session, jzon_str(jobj, "session"), sizeof(cs->session));

	return 0;
}
