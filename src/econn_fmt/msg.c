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
#include "avs_icall.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"


const char econn_proto_version[] = "3.0";


static int econn_props_encode(struct json_object *jobj,
			      const struct econn_props *props)
{
	struct odict *odict_target;
	int err = 0;

	if (!jobj || !props)
		return EINVAL;

	odict_target = jzon_get_odict(jobj);

	err = odict_entry_add(odict_target, "props",
			      ODICT_OBJECT, props->dict);
	if (err)
		return err;

	return err;
}

#if ENABLE_CONFERENCE_CALLS
static int econn_parts_encode(struct json_object *jobj,
			      const struct list *partl)
{
	struct le *le;
	struct json_object *jparts;
	int err = 0;

	jparts = jzon_alloc_array();
	if (!jparts)
		return ENOMEM;
	
	LIST_FOREACH(partl, le) {
		struct econn_group_part *part = le->data;
		struct json_object *jpart;
		char ssrc[32];

		jpart = jzon_alloc_object();
		if (!jpart) {
			err = ENOMEM;
			goto out;
		}
		jzon_add_str(jpart, "userid", part->userid);
		jzon_add_str(jpart, "clientid", part->clientid);
		jzon_add_bool(jpart, "authorized", part->authorized);
		re_snprintf(ssrc, sizeof(ssrc), "%u", part->ssrca);
		jzon_add_str(jpart, "ssrc_audio", ssrc);
		re_snprintf(ssrc, sizeof(ssrc), "%u", part->ssrcv);
		jzon_add_str(jpart, "ssrc_video", ssrc);

		json_object_array_add(jparts, jpart);
	}

	json_object_object_add(jobj, "participants", jparts);

 out:
	return err;
}

static void part_destructor(void *arg)
{
	struct econn_group_part *part = arg;

	mem_deref(part->userid);
	mem_deref(part->clientid);
}

struct econn_group_part *econn_part_alloc(const char *userid,
					  const char *clientid)
{
	struct econn_group_part *part;

	part = mem_zalloc(sizeof(*part), part_destructor);
	if (!part) {
		warning("econn: part_decode_handler: could not alloc part\n");
		return NULL;
	}

	str_dup(&part->userid, userid);
	str_dup(&part->clientid, clientid);

	return part;
}

static bool part_decode_handler(const char *key, struct json_object *jobj,
				void *arg)
{
	struct econn_group_part *part;
	struct list *partl = arg;
	const char *ssrc;
	int err;

	part = mem_zalloc(sizeof(*part), part_destructor);
	if (!part) {
		warning("econn: part_decode_handler: could not alloc part\n");
		return false;
	}

	err = jzon_strdup(&part->userid, jobj, "userid");
	err |= jzon_strdup(&part->clientid, jobj, "clientid");
	err |= jzon_bool(&part->authorized, jobj, "authorized");
	if (err)
		goto out;

	ssrc = jzon_str(jobj, "ssrc_audio");
	if (ssrc)
		sscanf(ssrc, "%u", &part->ssrca);
	else {
		err = EINVAL;
		goto out;
	}
	ssrc = jzon_str(jobj, "ssrc_video");
	if (ssrc)
		sscanf(ssrc, "%u", &part->ssrcv);
	else {
		err = EINVAL;
		goto out;
	}

 out:	
	if (err) {
		warning("econn: failed to parse participant entry\n");
		return false;
	}

	list_append(partl, &part->le, part);

	return false;
}


static int econn_parts_decode(struct list *partl, struct json_object *jobj)
{
	struct json_object *jparts;
	int err = 0;

	err = jzon_array(&jparts, jobj, "participants");
	if (err) {
		warning("econn: parts decode: no participants\n");
		return err;
	}

	jzon_apply(jparts, part_decode_handler, partl);

	return 0;
}
#endif


int econn_message_encode(char **strp, const struct econn_message *msg)
{
	struct json_object *jobj = NULL;
	char *str = NULL;
	int err;

	if (!strp || !msg)
		return EINVAL;

	err = jzon_creatf(&jobj, "sss",
			  "version", econn_proto_version,
			  "type",   econn_msg_name(msg->msg_type),
			  "sessid", msg->sessid_sender);
	if (err)
		return err;

	if (str_isset(msg->src_userid)) {
		err = jzon_add_str(jobj, "src_userid", msg->src_userid);
		if (err)
			goto out;
	}

	if (str_isset(msg->src_clientid)) {
		err = jzon_add_str(jobj, "src_clientid", msg->src_clientid);
		if (err)
			goto out;
	}

	if (str_isset(msg->dest_userid)) {
		err = jzon_add_str(jobj, "dest_userid", msg->dest_userid);
		if (err)
			goto out;
	}

	if (str_isset(msg->dest_clientid)) {
		err = jzon_add_str(jobj, "dest_clientid", msg->dest_clientid);
		if (err)
			goto out;
	}

	err = jzon_add_bool(jobj, "resp", msg->resp);
	if (err)
		goto out;

	switch (msg->msg_type) {

	case ECONN_SETUP:
	case ECONN_GROUP_SETUP:
	case ECONN_UPDATE:
		err = jzon_add_str(jobj, "sdp", msg->u.setup.sdp_msg);
		if (err)
			goto out;

		/* props is optional for SETUP */
		if (msg->u.setup.props) {
			err = econn_props_encode(jobj, msg->u.setup.props);
			if (err)
				goto out;
		}
		break;

	case ECONN_CANCEL:
		break;

	case ECONN_HANGUP:
		break;

	case ECONN_REJECT:
		break;

	case ECONN_PROPSYNC:

		/* props is mandatory for PROPSYNC */
		if (!msg->u.propsync.props) {
			warning("propsync: missing props\n");
			err = EINVAL;
			goto out;
		}

		err = econn_props_encode(jobj, msg->u.propsync.props);
		if (err)
			goto out;
		break;

	case ECONN_GROUP_START:
		/* props is optional for GROUPSTART */
		if (msg->u.groupstart.props) {
			err = econn_props_encode(jobj, msg->u.groupstart.props);
			if (err)
				goto out;
		}
		break;

	case ECONN_GROUP_LEAVE:
	case ECONN_GROUP_CHECK:
		break;

#if ENABLE_CONFERENCE_CALLS
	case ECONN_CONF_CONN:
		break;

	case ECONN_CONF_START:
		jzon_add_str(jobj, "sft_url", msg->u.confstart.sft_url);
		jzon_add_base64(jobj, "secret",
				msg->u.confstart.secret, msg->u.confstart.secretlen);
		jzon_add_str(jobj, "timestamp", "%llu", msg->u.confstart.timestamp);
		jzon_add_str(jobj, "seqno", "%u", msg->u.confstart.seqno);
		/* props is optional for CONFSTART */
		if (msg->u.confstart.props) {
			err = econn_props_encode(jobj, msg->u.confstart.props);
			if (err)
				goto out;
		}
		break;

	case ECONN_CONF_END:
		break;

	case ECONN_CONF_PART:
		jzon_add_bool(jobj, "should_start",
			      msg->u.confpart.should_start);
		jzon_add_str(jobj, "timestamp", "%llu", msg->u.confpart.timestamp);
		jzon_add_str(jobj, "seqno", "%u", msg->u.confpart.seqno);
		econn_parts_encode(jobj, &msg->u.confpart.partl);
		break;

	case ECONN_CONF_KEY:
		jzon_add_int(jobj, "idx",
			     msg->u.confkey.idx);
		jzon_add_base64(jobj, "key",
				msg->u.confkey.keydata, msg->u.confkey.keylen);
		break;
#endif

	case ECONN_DEVPAIR_PUBLISH:
		err = zapi_iceservers_encode(jobj,
					     msg->u.devpair_publish.turnv,
					     msg->u.devpair_publish.turnc);
		if (err)
			goto out;

		err = jzon_add_str(jobj, "sdp",
				   msg->u.devpair_publish.sdp);
		err |= jzon_add_str(jobj, "username",
				    msg->u.devpair_publish.username);
		if (err)
			goto out;
		break;

	case ECONN_DEVPAIR_ACCEPT:
		err = jzon_add_str(jobj, "sdp",
				   msg->u.devpair_accept.sdp);
		if (err)
			goto out;
		break;

	case ECONN_ALERT:
		err  = jzon_add_int(jobj, "level", msg->u.alert.level);
		err |= jzon_add_str(jobj, "descr", msg->u.alert.descr);
		if (err)
			goto out;
		break;

	default:
		warning("econn: dont know how to encode %d\n", msg->msg_type);
		err = EBADMSG;
		break;
	}
	if (err)
		goto out;

	err = jzon_encode(&str, jobj);
	if (err)
		goto out;

 out:
	mem_deref(jobj);
	if (err)
		mem_deref(str);
	else
		*strp = str;

	return err;
}


static int econn_props_decode(struct econn_props **props,
			      struct json_object *jobj)
{
	struct json_object *jobj_props;
	int err;

	if (!props || !jobj)
		return EINVAL;

	/* get the "props" subtype */
	err = jzon_object(&jobj_props, jobj, "props");
	if (err) {
		warning("econn: no props\n");
		return err;
	}

	err = econn_props_alloc(props, jzon_get_odict(jobj_props));
	if (err) {
		warning("econn: econn_props_alloc error\n");
		return err;
	}

	return err;
}


int econn_message_decode(struct econn_message **msgp,
			 uint64_t curr_time, uint64_t msg_time,
			 const char *str, size_t len)
{
	struct econn_message *msg = NULL;
	struct json_object *jobj = NULL;
	const char *ver, *type, *sdp, *sessid, *userid, *clientid;
	int err;

	if (!msgp || !str)
		return EINVAL;

	err = jzon_decode(&jobj, str, len);
	if (err)
		return err;

	msg = econn_message_alloc();
	if (!msg) {
		err = ENOMEM;
		goto out;
	}

	ver = jzon_str(jobj, "version");
	if (!ver) {
		warning("econn: missing 'version' field\n");
		err = EBADMSG;
		goto out;
	}

	if (0 != str_casecmp(econn_proto_version, ver)) {
		warning("econn: version mismatch (us=%s, msg=%s)\n",
			econn_proto_version, ver);
		err = EPROTO;
		goto out;
	}

	type = jzon_str(jobj, "type");
	if (!type) {
		warning("econn: missing 'type' field\n");
		err = EBADMSG;
		goto out;
	}

	sessid = jzon_str(jobj, "sessid");
	if (!sessid) {
		warning("econn: missing 'sessid' field\n");
		goto out;
	}
	str_ncpy(msg->sessid_sender, sessid, sizeof(msg->sessid_sender));

	userid = jzon_str(jobj, "src_userid");
	str_ncpy(msg->src_userid, userid, sizeof(msg->src_userid));

	clientid = jzon_str(jobj, "src_clientid");
	str_ncpy(msg->src_clientid, clientid, sizeof(msg->src_clientid));

	userid = jzon_str(jobj, "dest_userid");
	str_ncpy(msg->dest_userid, userid, sizeof(msg->dest_userid));

	clientid = jzon_str(jobj, "dest_clientid");
	str_ncpy(msg->dest_clientid, clientid, sizeof(msg->dest_clientid));

	err = jzon_bool(&msg->resp, jobj, "resp");
	if (err) {
		warning("econn: missing 'resp' field\n");
		goto out;
	}

	if (0 == str_casecmp(type, econn_msg_name(ECONN_SETUP))) {

		msg->msg_type = ECONN_SETUP;

		sdp = jzon_str(jobj, "sdp");
		if (!sdp) {
			warning("econn: missing 'sdp' field\n");
			err = EBADMSG;
			goto out;
		}

		err = str_dup(&msg->u.setup.sdp_msg, sdp);
		if (err)
			goto out;

		err = econn_props_decode(&msg->u.setup.props, jobj);
		if (err)
			goto out;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_GROUP_SETUP))) {

		msg->msg_type = ECONN_GROUP_SETUP;

		sdp = jzon_str(jobj, "sdp");
		if (!sdp) {
			warning("econn: missing 'sdp' field\n");
			err = EBADMSG;
			goto out;
		}

		err = str_dup(&msg->u.setup.sdp_msg, sdp);
		if (err)
			goto out;

		err = econn_props_decode(&msg->u.setup.props, jobj);
		if (err)
			goto out;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_UPDATE))) {

		msg->msg_type = ECONN_UPDATE;

		sdp = jzon_str(jobj, "sdp");
		if (!sdp) {
			warning("econn: missing 'sdp' field\n");
			err = EBADMSG;
			goto out;
		}

		err = str_dup(&msg->u.setup.sdp_msg, sdp);
		if (err)
			goto out;

		err = econn_props_decode(&msg->u.setup.props, jobj);
		if (err)
			info("econn: decode UPDATE: no props\n");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CANCEL))) {

		msg->msg_type = ECONN_CANCEL;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_HANGUP))) {

		msg->msg_type = ECONN_HANGUP;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_REJECT))) {

		msg->msg_type = ECONN_REJECT;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_PROPSYNC))) {

		msg->msg_type = ECONN_PROPSYNC;

		err = econn_props_decode(&msg->u.propsync.props, jobj);
		if (err)
			goto out;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_GROUP_START))) {

		msg->msg_type = ECONN_GROUP_START;

		/* Props are optional, 
		 * dont fail to decode message if they are missing
		 */
		if (econn_props_decode(&msg->u.groupstart.props, jobj))
			info("econn: decode GROUPSTART: no props\n");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_GROUP_LEAVE))) {
		
		msg->msg_type = ECONN_GROUP_LEAVE;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_GROUP_CHECK))) {

		msg->msg_type = ECONN_GROUP_CHECK;
	}
#if ENABLE_CONFERENCE_CALLS
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_START))) {
		struct pl pl = PL_INIT;
		const char *secret;
		uint8_t *sdata;
		size_t slen;

		msg->msg_type = ECONN_CONF_START;

		err = jzon_strdup(&msg->u.confstart.sft_url, jobj, "sft_url");
		if (err) {
			warning("econn: decode CONFSTART: couldnt read SFT URL\n");
			goto out;
		}
		pl_set_str(&pl, jzon_str(jobj, "timestamp"));
		msg->u.confstart.timestamp = pl_u64(&pl);
		pl_set_str(&pl, jzon_str(jobj, "seqno"));
		msg->u.confstart.seqno = pl_u32(&pl);

		secret = jzon_str(jobj, "secret");
		if (!secret || err)
			return EBADMSG;

		slen = str_len(secret) * 3 / 4;

		sdata = mem_zalloc(slen, NULL);
		if (!sdata) {
			return ENOMEM;
		}
		err = base64_decode(secret, str_len(secret), sdata, &slen);
		if (err) {
			mem_deref(sdata);
			return err;
		}

		msg->u.confstart.secret = sdata;
		msg->u.confstart.secretlen = slen;
		/* Props are optional, dont fail to decode message if they are missing */
		if (econn_props_decode(&msg->u.confstart.props, jobj))
			info("econn: decode CONFSTART: no props\n");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_CONN))) {
		msg->msg_type = ECONN_CONF_CONN;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_END))) {
		msg->msg_type = ECONN_CONF_END;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_PART))) {
		struct pl pl = PL_INIT;

		msg->msg_type = ECONN_CONF_PART;

		jzon_bool(&msg->u.confpart.should_start, jobj,
			  "should_start");
		pl_set_str(&pl, jzon_str(jobj, "timestamp"));
		msg->u.confpart.timestamp = pl_u64(&pl);
		pl_set_str(&pl, jzon_str(jobj, "seqno"));
		msg->u.confpart.seqno = pl_u32(&pl);
		
		if (econn_parts_decode(&msg->u.confpart.partl, jobj))
			warning("econn: decode: CONF_PART no parts\n");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_KEY))) {
		const char *key;
		uint8_t *kdata;
		size_t klen;
		msg->msg_type = ECONN_CONF_KEY;

		jzon_int(&msg->u.confkey.idx, jobj, "idx");
		key = jzon_str(jobj, "key");
		if (!key || err)
			return EBADMSG;

		klen = str_len(key) * 3 / 4;

		kdata = mem_zalloc(klen, NULL);
		if (!kdata) {
			return ENOMEM;
		}
		err = base64_decode(key, str_len(key), kdata, &klen);
		if (err) {
			mem_deref(kdata);
			return err;
		}

		msg->u.confkey.keydata = kdata;
		msg->u.confkey.keylen = klen;
	}
#endif
	else if (0 == str_casecmp(type,
				  econn_msg_name(ECONN_DEVPAIR_PUBLISH))) {

		struct json_object *jturns;

		msg->msg_type = ECONN_DEVPAIR_PUBLISH;

		err = jzon_array(&jturns, jobj, "ice_servers");
		if (err) {
			warning("econn: devpair_publish: no ICE servers\n");
			goto out;
		}

		err = zapi_iceservers_decode(jturns,
					     &msg->u.devpair_publish.turnv,
					     &msg->u.devpair_publish.turnc);
		if (err) {
			warning("econn: devpair_publish: "
				"could not decode ICE servers (%m)\n", err);
			goto out;
		}

		err = jzon_strdup(&msg->u.devpair_publish.sdp,
				  jobj, "sdp");
		if (err) {
			warning("econn: devpair_publish: "
				"could not find SDP in message\n");
			goto out;
		}
		err = jzon_strdup(&msg->u.devpair_publish.username,
				  jobj, "username");
		if (err) {
			warning("econn: devpair_publish: "
				"could not find username in message\n");
			goto out;
		}
	}
	else if (0 == str_casecmp(type,
				  econn_msg_name(ECONN_DEVPAIR_ACCEPT))) {

		msg->msg_type = ECONN_DEVPAIR_ACCEPT;

		err = jzon_strdup(&msg->u.devpair_accept.sdp,
				  jobj, "sdp");
		if (err) {
			warning("econn: devpair_accept: "
				"could not find SDP in message\n");
			goto out;
		}
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_ALERT))) {

		msg->msg_type = ECONN_ALERT;

		err = jzon_u32(&msg->u.alert.level, jobj, "level");
		if (err) {
			warning("econn: alert: "
				"could not find level in message\n");
			goto out;
		}

		err = jzon_strdup(&msg->u.alert.descr,
				  jobj, "descr");
		if (err) {
			warning("econn: alert: "
				"could not find descr in message\n");
			goto out;
		}
	}
	else {
		warning("econn: decode: unknown message type '%s'\n", type);
		err = EPROTONOSUPPORT;
		goto out;
	}

	msg->time = msg_time;
	msg->age = (msg_time > curr_time) ? 0 : curr_time - msg_time;

#if 0
	re_printf("%H\n", jzon_encode_odict_pretty, jzon_get_odict(jobj));
#endif

 out:
	mem_deref(jobj);
	if (err)
		mem_deref(msg);
	else
		*msgp = msg;

	return err;
}
