/*
* Wire
* Copyright (C) 2020 Wire Swiss GmbH
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


static uint32_t g_mid = 1000;

enum bundle_type {
        BUNDLE_TYPE_AUDIO,
        BUNDLE_TYPE_VIDEO,
};

struct bundle {
	uint32_t mid;
	struct mbuf *mb;
};


static struct sdp_media *find_media(struct sdp_session *sess,
				    const char *type)
{
	struct sdp_media *sdpm;
	const struct list *medial;
	struct le *le;
	bool found = false;

	if (!sess) {
		return NULL;
	}

	medial = sdp_session_medial(sess, false);

	for (le = medial->head; le && !found; le = le->next) {
		sdpm = (struct sdp_media *)le->data;
		
		found = streq(type, sdp_media_name(sdpm));
	}

	return found ? sdpm : NULL;
}

static bool fmt_handler(struct sdp_format *fmt, void *arg)
{
	struct sdp_media *sdpm = (struct sdp_media *)arg;

	if (streq(sdp_media_name(sdpm), "audio")) {
		if (streq(fmt->name, "opus")) {
			sdp_format_add(NULL, sdpm, false,
				       fmt->id, fmt->name, fmt->srate, fmt->ch,
				       NULL, NULL, NULL, false, "%s", fmt->params);
		}
	}
	else if (streq(sdp_media_name(sdpm), "video")) {
		if (strcaseeq(fmt->name, "vp8")) {
			sdp_format_add(NULL, sdpm, false,
				       fmt->id, fmt->name, fmt->srate, fmt->ch,
				       NULL, NULL, NULL, false, "%s", fmt->params);
		}
	}
	else if (streq(sdp_media_name(sdpm), "application")) {

		sdp_format_add(NULL, sdpm, false,
				       fmt->id, fmt->name, fmt->srate, fmt->ch,
				       NULL, NULL, NULL, false, "%s", fmt->params);
	}

	return false;	
}


static bool media_rattr_handler(const char *name, const char *value, void *arg)
{
	struct sdp_media *sdpm = (struct sdp_media *)arg;
	bool should_set = false;

	should_set = streq(name, "fingerprint")
		  || streq(name, "ice-ufrag")
		  || streq(name, "ice-pwd")
		  || streq(name, "rtcp-mux")
		  || streq(name, "extmap");
	
	if (should_set)
		sdp_safe_media_set_lattr(sdpm, false, name, value);

	return false;
}


static void bundle_ssrc(enum bundle_type type, struct conf_member *cm,
			struct bundle *bundle,
			struct sdp_session *sess, struct sdp_media *sdpm)
{
	struct sdp_media *newm;
	const char *mtype;
	uint32_t ssrc;
	uint32_t mid;
	bool disabled = false;
	int lport;
	int err;

	switch (type) {
	case BUNDLE_TYPE_AUDIO:
		ssrc = cm->ssrca;
		mtype = sdp_media_audio;
		if (cm->mida == 0) {
			cm->mida = g_mid;
			g_mid++;
		}
		mid = cm->mida;
		break;

	case BUNDLE_TYPE_VIDEO:
		ssrc = cm->ssrcv;
		mtype = sdp_media_video;
		if (cm->midv == 0) {
			cm->midv = g_mid;
			g_mid++;
		}
		mid = cm->midv;
		break;

	default:
		warning("bundle_ssrc: unknown bundle type\n");
		return;
	}

	disabled = ssrc == 0 || !cm->active;
	lport = 9; //disabled ? 0 : 9;
	err = sdp_media_add(&newm, sess, mtype, lport,
			    sdp_media_proto(sdpm));
	if (err) {
		warning("bundle_ssrc: video add failed: %m\n", err);
		return;
	}
	
	sdp_media_set_disabled(newm, false);
	sdp_media_set_laddr(newm, sdp_media_raddr(sdpm));
	sdp_media_set_lport(newm, lport);
	sdp_media_set_lattr(newm, false, "mid", "%u", mid);
	if (!disabled) {
		sdp_media_set_lattr(newm, false, "ssrc", "%u cname:%s",
				    ssrc, cm->cname);
		sdp_media_set_lattr(newm, false, "ssrc", "%u msid:%s %s",
				    ssrc, cm->msid, cm->label);
		sdp_media_set_lattr(newm, false, "ssrc", "%u mslabel:%s",
				    ssrc, cm->msid);
		sdp_media_set_lattr(newm, false, "ssrc", "%u label:%s",
				    ssrc, cm->label);
	}

	sdp_media_format_apply(sdpm, false, NULL, -1, NULL,
			       -1, -1, fmt_handler, newm);

	sdp_media_rattr_apply(sdpm, NULL,
			      media_rattr_handler, newm);

	sdp_media_set_ldir(newm, disabled ? SDP_INACTIVE : SDP_SENDONLY);
	sdp_media_set_lbandwidth(newm, SDP_BANDWIDTH_AS,
				 sdp_media_rbandwidth(sdpm, SDP_BANDWIDTH_AS));

	mbuf_printf(bundle->mb, " %u", mid);
}


int bundle_update(struct iflow *flow,
		  enum icall_conv_type conv_type,
		  bool include_audio,
		  const char *remote_sdp,
		  struct list *membl,
		  bundle_flow_update_h *flow_updateh)
{
	struct sdp_media *sdpa = NULL;
	struct sdp_media *sdpv = NULL;
	char *grpstr;
	int err = 0;
	struct le *le;
	struct sdp_session *sess;
	struct bundle bundle;
	char *sdpres = NULL;
	struct mbuf *mbb;

	sdp_dup(&sess, conv_type, remote_sdp, false);
	sdpa = find_media(sess, "audio");
	sdpv = find_media(sess, "video");
	
	bundle.mid = 0;
	bundle.mb = mbuf_alloc(128);
	mbuf_printf(bundle.mb, "%s", sdp_session_rattr(sess, "group"));

	list_flush((struct list *)sdp_session_medial(sess, true));
	
	LIST_FOREACH(membl, le) {
		struct conf_member *cm = (struct conf_member *)le->data;

		if (include_audio && sdpa)
			bundle_ssrc(BUNDLE_TYPE_AUDIO, cm, &bundle, sess, sdpa);
		if (sdpv && cm->ssrcv)
			bundle_ssrc(BUNDLE_TYPE_VIDEO, cm, &bundle, sess, sdpv);
	}

	bundle.mb->pos = 0;
	mbuf_strdup(bundle.mb, &grpstr, mbuf_get_left(bundle.mb));
	sdp_safe_session_set_lattr(sess, true, "group", grpstr);
	mem_deref(grpstr);
	mem_deref(bundle.mb);

	sdp_encode(&mbb, sess, true);
	mbuf_strdup(mbb, &sdpres, mbb->end);
	mem_deref(mbb);
		
	if (flow_updateh) {
		flow_updateh(flow, sdpres);
	}

	mem_deref(sdpres);
	mem_deref(sess);

	return err;
}
