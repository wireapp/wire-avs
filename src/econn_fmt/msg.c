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
#include <sodium.h>
#include "avs_log.h"
#include "avs_jzon.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_icall.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_string.h"


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
		jzon_add_str(jpart, "userid", "%s", part->userid);
		jzon_add_str(jpart, "clientid", "%s", part->clientid);
		jzon_add_bool(jpart, "authorized", part->authorized);
		re_snprintf(ssrc, sizeof(ssrc), "%u", part->ssrca);
		jzon_add_str(jpart, "ssrc_audio", "%s", ssrc);
		re_snprintf(ssrc, sizeof(ssrc), "%u", part->ssrcv);
		jzon_add_str(jpart, "ssrc_video", "%s", ssrc);
		jzon_add_int(jpart, "timestamp", (int32_t)part->ts);

		switch(part->muted_state) {
		case MUTED_STATE_UNMUTED:
			jzon_add_bool(jpart, "muted", false);
			break;

		case MUTED_STATE_MUTED:
			jzon_add_bool(jpart, "muted", true);
			break;

		default:
			break;
		}

		json_object_array_add(jparts, jpart);
	}

	json_object_object_add(jobj, "participants", jparts);

 out:
	return err;
}

static void part_destructor(void *arg)
{
	struct econn_group_part *part = arg;

	list_unlink(&part->le);

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
	int32_t ts;
	bool muted;
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

	/* Timestamp is optional */
	err = jzon_int(&ts, jobj, "timestamp");
	if (!err)
		part->ts = (uint64_t)ts;
	
	err = jzon_bool(&muted, jobj, "muted");
	if (err) {
		err = 0;
		part->muted_state = MUTED_STATE_UNKNOWN;
	}
	else {
		part->muted_state =
			muted ? MUTED_STATE_MUTED : MUTED_STATE_UNMUTED;
	}

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

static int econn_keys_encode(struct json_object *jobj,
			     const struct list *keyl)
{
	struct le *le;
	struct json_object *jkeys;
	int err = 0;

	jkeys = jzon_alloc_array();
	if (!jkeys)
		return ENOMEM;
	
	LIST_FOREACH(keyl, le) {
		struct econn_key_info *key = le->data;
		struct json_object *jkey;

		jkey = jzon_alloc_object();
		if (!jkey) {
			err = ENOMEM;
			goto out;
		}
		jzon_add_int(jkey, "idx", key->idx);
		jzon_add_base64(jkey, "data",
				key->data, key->dlen);

		json_object_array_add(jkeys, jkey);
	}

	json_object_object_add(jobj, "keys", jkeys);

 out:
	return err;
}

static void key_destructor(void *arg)
{
	struct econn_key_info *key = arg;

	list_unlink(&key->le);
	if (key->data && key->dlen)
		sodium_memzero(key->data, key->dlen);
	key->data = mem_deref(key->data);
	key->dlen = 0;
}

struct econn_key_info *econn_key_info_alloc(size_t keysz)
{
	struct econn_key_info *key;

	key = mem_zalloc(sizeof(*key), key_destructor);
	if (!key) {
		warning("econn: key_decode_handler: could not alloc key\n");
		return NULL;
	}

	key->data = mem_zalloc(keysz, NULL);
	if (!key->data) {
		warning("econn: key_decode_handler: could not alloc key\n");
		mem_deref(key);
		return NULL;
	}
	key->dlen = keysz;

	return key;
}

static bool key_decode_handler(const char *keystr,
			       struct json_object *jobj,
			       void *arg)
{
	struct econn_key_info *key;
	struct list *keyl = arg;
	int32_t i;
	const char *dstr;
	uint8_t *d;
	size_t sz;
	int err;

	key = mem_zalloc(sizeof(*key), key_destructor);
	if (!key) {
		warning("econn: key_decode_handler: could not alloc part\n");
		return false;
	}

	err = jzon_int(&i, jobj, "idx");
	if (err)
		return false;

	key->idx = i;

	dstr = jzon_str(jobj, "data");
	if (!dstr)
		return false;

	sz = str_len(dstr);

	d = mem_zalloc(sz, NULL);
	if (!d)
		return false;

	err = base64_decode(dstr, str_len(dstr), d, &sz);
	if (err) {
		warning("econn: failed to base64 decode key\n");
		mem_deref(d);
		return err;
	}

	key->data = d;
	key->dlen = sz;

	list_append(keyl, &key->le, key);

	return false;
}

static int econn_keys_decode(struct list *keyl, struct json_object *jobj)
{
	struct json_object *jkeys;
	int err = 0;

	err = jzon_array(&jkeys, jobj, "keys");
	if (err) {
		warning("econn: keys decode: no keys\n");
		return err;
	}

	jzon_apply(jkeys, key_decode_handler, keyl);

	return 0;
}

static int econn_streams_encode(struct json_object *jobj,
				const struct list *streaml)
{
	struct le *le;
	struct json_object *jstreams;
	int err = 0;

	jstreams = jzon_alloc_array();
	if (!jstreams)
		return ENOMEM;
	
	LIST_FOREACH(streaml, le) {
		struct econn_stream_info *stream = le->data;
		struct json_object *jstream;

		jstream = jzon_alloc_object();
		if (!jstream) {
			err = ENOMEM;
			goto out;
		}
		jzon_add_str(jstream, "userid", "%s", stream->userid);
		jzon_add_int(jstream, "quality", stream->quality);
		if (stream->ssrcv.hi)
			jzon_add_int(jstream, "ssrcv_hi", stream->ssrcv.hi);
		if (stream->ssrcv.lo)
			jzon_add_int(jstream, "ssrcv_lo", stream->ssrcv.lo);
		if (str_isset(stream->ssrcv.clientid))
			jzon_add_str(jstream, "clientid", "%s", stream->ssrcv.clientid);

		json_object_array_add(jstreams, jstream);
	}

	json_object_object_add(jobj, "streams", jstreams);

 out:
	return err;
}

static void stream_destructor(void *arg)
{
	struct econn_stream_info *stream = arg;

	list_unlink(&stream->le);
}

struct econn_stream_info *econn_stream_info_alloc(const char *userid,
						  uint32_t quality)
{
	struct econn_stream_info *stream;

	stream = mem_zalloc(sizeof(*stream), stream_destructor);
	if (!stream) {
		warning("econn: stream_decode_handler: could not alloc stream\n");
		return NULL;
	}

	str_ncpy(stream->userid, userid, sizeof(stream->userid));
	stream->quality = quality;

	return stream;
}

static bool stream_decode_handler(const char *keystr,
				  struct json_object *jobj,
				  void *arg)
{
	struct econn_stream_info *stream;
	struct list *streaml = arg;
	const char *userid = NULL;
	const char *clientid = NULL;
	int32_t quality = 0;
	uint32_t ssrcv = 0;
	uint32_t ssrcv_hi = 0;
	uint32_t ssrcv_lo = 0;
	int err;

	err = jzon_int(&quality, jobj, "quality");
	if (err)
		goto out;

	userid = jzon_str(jobj, "userid");
	if (!userid)
		goto out;

	err = jzon_u32(&ssrcv, jobj, "ssrcv_hi");
	if (0 == err)
		ssrcv_hi = ssrcv;
	err = jzon_u32(&ssrcv, jobj, "ssrcv_lo");
	if (0 == err)
		ssrcv_lo = ssrcv;

	clientid = jzon_str(jobj, "clientid");

	stream = econn_stream_info_alloc(userid, quality);
	if (!stream) {
		warning("econn: stream_decode_handler: could not alloc stream\n");
		goto out;
	}
	if (ssrcv_hi)
		stream->ssrcv.hi = ssrcv_hi;
	if (ssrcv_lo)
		stream->ssrcv.lo = ssrcv_lo;
	if (clientid) {
		str_ncpy(stream->ssrcv.clientid, clientid,
			 ARRAY_SIZE(stream->ssrcv.clientid));
	}

	list_append(streaml, &stream->le, stream);

out:
	return false;
}

static int econn_streams_decode(struct list *streaml, struct json_object *jobj)
{
	struct json_object *jstreams;
	int err = 0;

	err = jzon_array(&jstreams, jobj, "streams");
	if (err) {
		warning("econn: streams decode: no streams\n");
		return err;
	}

	jzon_apply(jstreams, stream_decode_handler, streaml);

	return 0;
}

static int econn_stringlist_encode(struct json_object *jobj,
				   const struct list *strl,
				   const char* name)
{
	struct le *le;
	struct json_object *jarray;
	int err = 0;

	if (list_count(strl) > 0) {
		jarray = jzon_alloc_array();
		if (!jarray)
			return ENOMEM;

		LIST_FOREACH(strl, le) {
			struct stringlist_info *str = le->data;
			struct json_object *jstr;

			jstr = json_object_new_string(str->str);
			if (!jstr) {
				err = ENOMEM;
				goto out;
			}
			json_object_array_add(jarray, jstr);
		}
		json_object_object_add(jobj, name, jarray);
	}

 out:
	return err;
}

static bool string_decode_handler(const char *keystr,
				  struct json_object *jobj,
				  void *arg)
{
	struct list *strl = arg;
	const char *val = NULL;
	int err = 0;

	val = json_object_get_string(jobj);
	if (val) {
		err =  stringlist_append(strl, val);
		if (err) {
			warning("econn: string_decode_handler: could not decode string\n");
			goto out;
		}
	}

out:
	return false;
}

static int econn_stringlist_decode(struct list *strl, struct json_object *jobj, const char *name)
{
	struct json_object *jarray;
	int err = 0;

	err = jzon_array(&jarray, jobj, name);
	if (err) {
		warning("econn: streams decode: no strings\n");
		return err;
	}

	jzon_apply(jarray, string_decode_handler, strl);

	return 0;
}

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
		err = jzon_add_str(jobj, "src_userid", "%s", msg->src_userid);
		if (err)
			goto out;
	}

	if (str_isset(msg->src_clientid)) {
		err = jzon_add_str(jobj, "src_clientid",
				   "%s", msg->src_clientid);
		if (err)
			goto out;
	}

	if (str_isset(msg->dest_userid)) {
		err = jzon_add_str(jobj, "dest_userid",
				   "%s", msg->dest_userid);
		if (err)
			goto out;
	}

	if (str_isset(msg->dest_clientid)) {
		err = jzon_add_str(jobj, "dest_clientid",
				   "%s", msg->dest_clientid);
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
		err = jzon_add_str(jobj, "sdp", "%s", msg->u.setup.sdp_msg);
		if (err)
			goto out;

		/* props is optional for SETUP */
		if (msg->u.setup.props) {
			err = econn_props_encode(jobj, msg->u.setup.props);
			if (err)
				goto out;
		}
		if (msg->u.setup.url) {
			err = jzon_add_str(jobj, "url", "%s", msg->u.setup.url);
			if (err)
				goto out;
		}
		if (msg->u.setup.sft_tuple) {
			err = jzon_add_str(jobj, "sft_tuple", "%s", msg->u.setup.sft_tuple);
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

	case ECONN_CONF_CONN:
		if (msg->u.confconn.turnc > 0) {
			err = zapi_iceservers_encode(jobj,
						     msg->u.confconn.turnv,
						     msg->u.confconn.turnc);
			if (err)
				goto out;
		}

		jzon_add_bool(jobj, "update",
			      msg->u.confconn.update);
		jzon_add_str(jobj, "tool", 
			     "%s", msg->u.confconn.tool);
		jzon_add_str(jobj, "toolver",
			     "%s", msg->u.confconn.toolver);
		jzon_add_int(jobj, "env",
			      msg->u.confconn.env);
		jzon_add_int(jobj, "status",
			      msg->u.confconn.status);
		jzon_add_bool(jobj, "selective_audio",
			      msg->u.confconn.selective_audio);
		jzon_add_bool(jobj, "selective_video",
			      msg->u.confconn.selective_video);
		jzon_add_int(jobj, "vstreams",
			      msg->u.confconn.vstreams);
		if (msg->u.confconn.sft_url) {
			jzon_add_str(jobj, "sft_url",
				     "%s", msg->u.confconn.sft_url);
		}
		if (msg->u.confconn.sft_tuple) {
			jzon_add_str(jobj, "sft_tuple",
				     "%s", msg->u.confconn.sft_tuple);
		}
		if (msg->u.confconn.sft_username) {
			jzon_add_str(jobj, "username",
				     "%s", msg->u.confconn.sft_username);
		}
		if (msg->u.confconn.sft_credential) {
			jzon_add_str(jobj, "credential",
				     "%s", msg->u.confconn.sft_credential);
		}
		break;

	case ECONN_CONF_START:
		jzon_add_str(jobj, "sft_url", "%s", msg->u.confstart.sft_url);
		if (msg->u.confstart.sft_tuple) {
			jzon_add_str(jobj, "sft_tuple", "%s", msg->u.confstart.sft_tuple);
		}
		jzon_add_base64(jobj, "secret",
				msg->u.confstart.secret, msg->u.confstart.secretlen);
		jzon_add_str(jobj, "timestamp", "%llu", msg->u.confstart.timestamp);
		jzon_add_str(jobj, "seqno", "%u", msg->u.confstart.seqno);
		econn_stringlist_encode(jobj, &msg->u.confstart.sftl, "sfts");
		/* props is optional for CONFSTART */
		if (msg->u.confstart.props) {
			err = econn_props_encode(jobj, msg->u.confstart.props);
			if (err)
				goto out;
		}
		break;

	case ECONN_CONF_CHECK:
		jzon_add_str(jobj, "sft_url", "%s", msg->u.confcheck.sft_url);
		if (msg->u.confcheck.sft_tuple) {
			jzon_add_str(jobj, "sft_tuple", "%s", msg->u.confcheck.sft_tuple);
		}
		jzon_add_base64(jobj, "secret",
				msg->u.confcheck.secret, msg->u.confcheck.secretlen);
		jzon_add_str(jobj, "timestamp", "%llu", msg->u.confcheck.timestamp);
		jzon_add_str(jobj, "seqno", "%u", msg->u.confcheck.seqno);
		econn_stringlist_encode(jobj, &msg->u.confcheck.sftl, "sfts");
		break;

	case ECONN_CONF_END:
		break;

	case ECONN_CONF_PART:
		jzon_add_bool(jobj, "should_start",
			      msg->u.confpart.should_start);
		jzon_add_str(jobj, "timestamp", "%llu", msg->u.confpart.timestamp);
		jzon_add_str(jobj, "seqno", "%u", msg->u.confpart.seqno);
		jzon_add_base64(jobj, "entropy",
				msg->u.confpart.entropy, msg->u.confpart.entropylen);
		econn_parts_encode(jobj, &msg->u.confpart.partl);
		econn_stringlist_encode(jobj, &msg->u.confpart.sftl, "sfts");
		break;

	case ECONN_CONF_KEY:
		econn_keys_encode(jobj, &msg->u.confkey.keyl);
		break;

	case ECONN_CONF_STREAMS:
		jzon_add_str(jobj, "mode", "%s", msg->u.confstreams.mode);
		econn_streams_encode(jobj, &msg->u.confstreams.streaml);
		break;

	case ECONN_DEVPAIR_PUBLISH:
		err = zapi_iceservers_encode(jobj,
					     msg->u.devpair_publish.turnv,
					     msg->u.devpair_publish.turnc);
		if (err)
			goto out;

		err = jzon_add_str(jobj, "sdp",
				   "%s", msg->u.devpair_publish.sdp);
		err |= jzon_add_str(jobj, "username",
				    "%s", msg->u.devpair_publish.username);
		if (err)
			goto out;
		break;

	case ECONN_DEVPAIR_ACCEPT:
		err = jzon_add_str(jobj, "sdp",
				   "%s", msg->u.devpair_accept.sdp);
		if (err)
			goto out;
		break;

	case ECONN_ALERT:
		err  = jzon_add_int(jobj, "level", msg->u.alert.level);
		err |= jzon_add_str(jobj, "descr", "%s", msg->u.alert.descr);
		if (err)
			goto out;
		break;

	case ECONN_PING:
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

	type = jzon_str(jobj, "type");
	if (!type) {
		warning("econn: missing 'type' field\n");
		err = EBADMSG;
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
		const char *url, *tuple;

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

		url = jzon_str(jobj, "url");
		if (url)
			str_dup(&msg->u.setup.url, url);

		tuple = jzon_str(jobj, "sft_tuple");
		if (tuple)
			str_dup(&msg->u.setup.sft_tuple, tuple);
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
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_START))) {
		struct pl pl = PL_INIT;
		const char *secret;
		uint8_t *sdata;
		size_t slen;
		const char *tuple;

		msg->msg_type = ECONN_CONF_START;

		err = jzon_strdup(&msg->u.confstart.sft_url, jobj, "sft_url");
		if (err) {
			warning("econn: decode CONFSTART: couldnt read SFT URL\n");
			goto out;
		}
		tuple = jzon_str(jobj, "sft_tuple");
		/* sft_tuple is optional for backwards compat */
		if (tuple)
			str_dup(&msg->u.confstart.sft_tuple, tuple);
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

		econn_stringlist_decode(&msg->u.confstart.sftl, jobj, "sfts");
		/* Props are optional, dont fail to decode message if they are missing */
		if (econn_props_decode(&msg->u.confstart.props, jobj))
			info("econn: decode CONFSTART: no props\n");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_CHECK))) {
		struct pl pl = PL_INIT;
		const char *secret;
		uint8_t *sdata;
		size_t slen;
		const char *tuple;

		msg->msg_type = ECONN_CONF_CHECK;

		err = jzon_strdup(&msg->u.confcheck.sft_url, jobj, "sft_url");
		if (err) {
			warning("econn: decode CONFCHECK: couldnt read SFT URL\n");
			goto out;
		}
		tuple = jzon_str(jobj, "sft_tuple");
		/* sft_tuple is optional for backwards compat */
		if (tuple)
			str_dup(&msg->u.confcheck.sft_tuple, tuple);
		pl_set_str(&pl, jzon_str(jobj, "timestamp"));
		msg->u.confcheck.timestamp = pl_u64(&pl);
		pl_set_str(&pl, jzon_str(jobj, "seqno"));
		msg->u.confcheck.seqno = pl_u32(&pl);

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

		msg->u.confcheck.secret = sdata;
		msg->u.confcheck.secretlen = slen;

		econn_stringlist_decode(&msg->u.confcheck.sftl, jobj, "sfts");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_CONN))) {
		struct json_object *jturns;
		const char *json_str = NULL;
		int32_t status = 0, vstreams = 0;
		bool selective_audio = false, selective_video = false;

		msg->msg_type = ECONN_CONF_CONN;

		err = jzon_array(&jturns, jobj, "ice_servers");
		if (!err) {
			err = zapi_iceservers_decode(jturns,
						     &msg->u.confconn.turnv,
						     &msg->u.confconn.turnc);
			if (err) {
				warning("econn: confconn: "
					"could not decode ICE servers (%m)\n", err);
				goto out;
			}
		}

		jzon_bool(&msg->u.confconn.update, jobj,
			  "update");

		json_str = jzon_str(jobj, "tool");
		if (json_str) {
			err = str_dup(&msg->u.confconn.tool, json_str);
			if (err)
				goto out;
		}

		json_str = jzon_str(jobj, "toolver");
		if (json_str) {
			err = str_dup(&msg->u.confconn.toolver, json_str);
			if (err)
				goto out;
		}

		jzon_int(&msg->u.confconn.env, jobj, "env");
		
		/* status is optional, missing = 0 */
		err = jzon_int(&status, jobj, "status");
		if (err) {
			status = 0;
		}

		msg->u.confconn.status = (enum econn_confconn_status) status;

		/* selective_audio is optional, missing = false */
		err = jzon_bool(&selective_audio, jobj, "selective_audio");
		if (err) {
			selective_audio = false;
		}
		msg->u.confconn.selective_audio = selective_audio;

		/* selective_video is optional, missing = false */
		err = jzon_bool(&selective_video, jobj, "selective_video");
		if (err) {
			selective_video = false;
		}
		msg->u.confconn.selective_video = selective_video;

		/* vstreams is optional, missing = 0, range 0 to 32 */
		err = jzon_int(&vstreams, jobj, "vstreams");
		if (err) {
			vstreams = 0;
			err = 0;
		}
		if (vstreams < 0)
			vstreams = 0;
		if (vstreams > 32)
			vstreams = 32;
		msg->u.confconn.vstreams = vstreams;

		json_str = jzon_str(jobj, "sft_url");
		if (json_str) {
			err = str_dup(&msg->u.confconn.sft_url, json_str);
			if (err)
				goto out;
		}
		json_str = jzon_str(jobj, "sft_tuple");
		if (json_str) {
			err = str_dup(&msg->u.confconn.sft_tuple, json_str);
			if (err)
				goto out;
		}

		json_str = jzon_str(jobj, "username");
		if (json_str) {
			err = str_dup(&msg->u.confconn.sft_username, json_str);
			if (err)
				goto out;
		}
		json_str = jzon_str(jobj, "credential");
		if (json_str) {
			err = str_dup(&msg->u.confconn.sft_credential, json_str);
			if (err)
				goto out;
		}
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_END))) {
		msg->msg_type = ECONN_CONF_END;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_PART))) {
		struct pl pl = PL_INIT;
		const char *entropy;
		uint8_t *edata;
		size_t elen;

		msg->msg_type = ECONN_CONF_PART;

		jzon_bool(&msg->u.confpart.should_start, jobj,
			  "should_start");
		pl_set_str(&pl, jzon_str(jobj, "timestamp"));
		msg->u.confpart.timestamp = pl_u64(&pl);
		pl_set_str(&pl, jzon_str(jobj, "seqno"));
		msg->u.confpart.seqno = pl_u32(&pl);

		entropy = jzon_str(jobj, "entropy");
		if (entropy) {
			elen = str_len(entropy) * 3 / 4;

			edata = mem_zalloc(elen, NULL);
			if (!edata) {
				return ENOMEM;
			}
			err = base64_decode(entropy, str_len(entropy), edata, &elen);
			if (err) {
				mem_deref(edata);
				return err;
			}

			msg->u.confpart.entropy = edata;
			msg->u.confpart.entropylen = elen;
		}

		if (econn_parts_decode(&msg->u.confpart.partl, jobj))
			warning("econn: decode: CONF_PART no parts\n");

		econn_stringlist_decode(&msg->u.confpart.sftl, jobj, "sfts");
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_KEY))) {
		msg->msg_type = ECONN_CONF_KEY;
		err = econn_keys_decode(&msg->u.confkey.keyl, jobj);
		if (err)
			return err;
	}
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_CONF_STREAMS))) {
		msg->msg_type = ECONN_CONF_STREAMS;
		err = econn_streams_decode(&msg->u.confstreams.streaml, jobj);
		if (err)
			return err;

		err = jzon_strdup(&msg->u.confstreams.mode,
				  jobj, "mode");
		if (err) {
			warning("econn: conf_streams: "
				"could not find mode in message\n");
			goto out;
		}
	}
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
	else if (0 == str_casecmp(type, econn_msg_name(ECONN_PING))) {
		msg->msg_type = ECONN_PING;
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
