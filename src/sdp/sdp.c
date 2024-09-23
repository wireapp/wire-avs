/*
* Wire
* Copyright (C) 2019 Wire Swiss GmbH
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
#include <avs_version.h>

#include "avs_peerflow.h"

const char *VIDEO_CODEC = "vp9";
const char *VIDEO_CODEC_FMTP = "profile-id=0";


#define MAX_NET_ID 5

enum {
      AUDIO_GROUP_BANDWIDTH = 32,
      VIDEO_GROUP_BANDWIDTH = 800,
};
      

struct norelay_elem {
	peerflow_norelay_h *h;
};

struct conv_sdp {
	struct sdp_media *sdpm;
	enum icall_conv_type conv_type;
};

void sdp_safe_session_set_lattr(struct sdp_session *sess, bool replace,
				const char *name, const char *value)
{
	if (value && *value != '\0')
		sdp_session_set_lattr(sess, replace, name, "%s", value);
	else
		sdp_session_set_lattr(sess, replace, name, "");
}

void sdp_safe_media_set_lattr(struct sdp_media *sdpm, bool replace,
			const char *name, const char *value)
{
	if (value && *value != '\0')
		sdp_media_set_lattr(sdpm, replace, name, "%s", value);
	else
		sdp_media_set_lattr(sdpm, replace, name, "");
}


static bool sess_rattr_handler(const char *name, const char *value, void *arg)
{
	struct sdp_session *sess = (struct sdp_session *)arg;

	sdp_safe_session_set_lattr(sess, false, name, value);

	return false;
}

static bool cand_handler(const char *name, const char *value, void *arg)
{
	debug("sdp: cand_handler: name=%s value=%s\n", name, value);

	return strstr(value, "relay") != NULL;
}


struct fmt_mod {
	struct sdp_media *sdpm;
	char *vid;
};

static bool fmt_handler(struct sdp_format *fmt, void *arg)
{
	struct fmt_mod *fmtm = arg;
	struct sdp_media *sdpm = fmtm->sdpm;
	bool use_fmt = false;

	if (streq(sdp_media_name(sdpm), "audio")) {
		if (streq(fmt->name, "opus")) {
			sdp_format_add(NULL, sdpm, false,
				       fmt->id, fmt->name, fmt->srate, fmt->ch,
				       NULL, NULL, NULL, false,
				       "%s", fmt->params);
		}
	}
	else if (streq(sdp_media_name(sdpm), "video")) {
		char aptid[16];

		if (strcaseeq(fmt->name, VIDEO_CODEC)) {
			if (VIDEO_CODEC_FMTP == NULL
			    || strcaseeq(fmt->params, VIDEO_CODEC_FMTP)) {
				use_fmt = true;
				if (fmtm->vid == NULL) {
					str_dup(&fmtm->vid, fmt->id);
				}
			}
		}
		if (strcaseeq(fmt->name, "rtx")) {
			int n;

			n = sscanf(fmt->params, "apt=%15s", aptid);
			if (n == 1) {
				if (strcaseeq(aptid, fmtm->vid))
					use_fmt = true;
			}
		}
		if (use_fmt) {
			sdp_format_add(NULL, sdpm, false,
				       fmt->id, fmt->name, fmt->srate, fmt->ch,
				       NULL, NULL, NULL, false,
				       "%s", fmt->params);
		}
	}
	else if (streq(sdp_media_name(sdpm), "application")) {

		sdp_format_add(NULL, sdpm, false,
			       fmt->id, fmt->name, fmt->srate, fmt->ch,
			       NULL, NULL, NULL, false,
			       "%s", fmt->params);
	}

	return false;
	
}


static bool has_payload(int pt, struct sdp_media *sdpm)
{
	struct sdp_format *fmt;

	fmt = sdp_media_format(sdpm, true, NULL, pt, NULL, -1, -1);

	return fmt != NULL;
}

enum cand_state {
	CAND_STATE_NORMAL = 0,
	CAND_STATE_TYP    = 1,
	CAND_STATE_NET    = 2
};

static bool media_rattr_handler(const char *name, const char *value, void *arg)
{
	struct conv_sdp *csdp = (struct conv_sdp *)arg;
	struct sdp_media *sdpm = csdp->sdpm;
	char *cval = NULL, *tok = NULL, *ctok = NULL;
	enum cand_state state = CAND_STATE_NORMAL;
	uint32_t idx = 0;
	bool is_host = false;
	bool is_ipv4 = false;
	int32_t network_id = 0;
	struct pl a, b, c, d;
	int err = 0;

	if (!name || ! value || !csdp)
		return false;

	if (streq(name, "extmap")) {
		int xid;
		char rval[256];
			
		sscanf(value, "%d %255s", &xid, rval);

		if (strstr(rval, "generic-frame-descriptor")) {
			if (csdp->conv_type != ICALL_CONV_TYPE_CONFERENCE)
				goto out;
		}
		if (xid > 0)
			sdp_safe_media_set_lattr(sdpm, false, name, value);
	}
	else if (streq(name, "rtcp-fb")) {
		int pt;
		char rval[256];
		
		sscanf(value, "%d %255s", &pt, rval);
		if (has_payload(pt, sdpm))
			sdp_safe_media_set_lattr(sdpm, false, name, value);
		else
			goto out;
	}
	else if (streq(name, "candidate")) {

		err = str_dup(&cval, value);
		if (err)
			goto out;

		ctok = cval;
		while ((tok = strtok_r(ctok, " ", &ctok))) {
			switch (state) {
			case CAND_STATE_NORMAL:
				if (4 == idx && 0 == re_regex(tok, strlen(tok),
				    "[0-9]+.[0-9]+.[0-9]+.[0-9]+",
				    &a, &b, &c, &d)) {
					is_ipv4 = true;
				}
				break;

			case CAND_STATE_TYP:
				if (0 == strcmp(tok, "host"))
					is_host = true;
				break;

			case CAND_STATE_NET:
				network_id = atoi(tok);
				break;
			}

			if (0 == strcmp(tok, "typ"))
				state = CAND_STATE_TYP;
			else if (0 == strcmp(tok, "network-id"))
				state = CAND_STATE_NET;
			else
				state = CAND_STATE_NORMAL;

			idx++;
		}

		/* Only drop typ host, IPv4 with network-id > 5 */
		if (is_host && is_ipv4 && network_id > MAX_NET_ID) {
			info("sdp_dup: dropping candidate line %s\n", value);
			goto out;
		}
		else {
			sdp_safe_media_set_lattr(sdpm, false, name, value);
		}
	}
	else {
		sdp_safe_media_set_lattr(sdpm, false, name, value);
	}

out:
	mem_deref(cval);
	return false;
}



static int sdp_dup_int(struct sdp_session **sessp,
		enum icall_conv_type conv_type,
		const char *sdp,
		bool offer,
		bool strip_video)
{
	struct mbuf mb;
	struct sdp_session *sess;
	const struct list *medial;
	struct le *le;
	struct sa laddr;
	int err = 0;

	mb.buf = (uint8_t *)sdp;

	mb.pos = 0;
	mb.size = str_len(sdp);
	mb.end = mb.size;

	sa_init(&laddr, AF_INET);
	sa_set_str(&laddr, "127.0.0.1", 9);
	err = sdp_session_alloc(&sess, &laddr);
	if (err) {
		warning("sdp_dup: failed to alloc sdp_session: %m\n", err);
		goto out;
	}
	err = sdp_decode(sess, &mb, true);
	if (err) {
		warning("sdp_dup: failed to parse sdp: %m\n", err);
		goto out;
	}

	sdp_session_rattr_apply(sess, NULL, sess_rattr_handler, sess);
	
	medial = sdp_session_medial(sess, false);
	for(le = medial->head; le; le = le->next) {

		struct sdp_media *sdpm = (struct sdp_media *)le->data;
		const char *mname = sdp_media_name(sdpm);
		enum sdp_dir rdir;
		int rport;
		struct conv_sdp csdp;
		struct fmt_mod fmtm;

		rport = sdp_media_rport(sdpm);
		sdp_media_add(NULL, sess, mname,
			      rport, sdp_media_proto(sdpm));

		sdp_media_set_disabled(sdpm, false);
		sdp_media_set_laddr(sdpm, sdp_media_raddr(sdpm));
		sdp_media_set_lport(sdpm, rport);

		fmtm.sdpm = sdpm;
		fmtm.vid = NULL;
		sdp_media_format_apply(sdpm, false, NULL, -1, NULL,
				       -1, -1, fmt_handler, &fmtm);
		mem_deref(fmtm.vid);

		csdp.sdpm = sdpm;
		csdp.conv_type = conv_type;
		sdp_media_rattr_apply(sdpm, NULL,
				      media_rattr_handler, &csdp);
		sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS,
				 sdp_media_rbandwidth(sdpm, SDP_BANDWIDTH_AS));

		rdir = sdp_media_rdir(sdpm); 
		if (strip_video) {
			if (streq(mname, "video")) {
				if (rdir == SDP_SENDONLY) 
					sdp_media_set_rdir(sdpm, SDP_INACTIVE);
				else if (rdir == SDP_RECVONLY)
					sdp_media_set_rdir(sdpm, SDP_SENDONLY);
			}
		}
		else {
			if (rdir == SDP_SENDONLY)
				sdp_media_set_rdir(sdpm, SDP_RECVONLY);
			else if (rdir == SDP_RECVONLY)
				sdp_media_set_rdir(sdpm, SDP_SENDONLY);
		}
	}

	if (sessp)
		*sessp = sess;

 out:
	if (err)
		mem_deref(sess);

	return err;
}

int sdp_dup(struct sdp_session **sessp,
	    enum icall_conv_type conv_type,
	    const char *sdp,
	    bool offer)
{
	return sdp_dup_int(sessp, conv_type, sdp, offer, false);
}

static void sdp_set_bandwidth(struct sdp_media *sdpm, bool screenshare)
{
    if (streq(sdp_media_name(sdpm), "video") && !screenshare) {
        sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS, VIDEO_GROUP_BANDWIDTH);
    }
    else if (streq(sdp_media_name(sdpm), "audio")){
        sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS, AUDIO_GROUP_BANDWIDTH);
        info("sdp_set_bandwidth: group mode, setting ptime\n");
        sdp_media_set_lattr(sdpm, true, "ptime", "40");
    }
}

const char *sdp_modify_offer(struct sdp_session *sess,
			     enum icall_conv_type conv_type,
			     bool screenshare,
			     bool audio_cbr)
{
	char *sdpres = NULL;
	struct mbuf *mbb;
	const struct list *medial;
	struct le *le;
	int err = 0;
	
	medial = sdp_session_medial(sess, false);
	for(le = medial->head; le; le = le->next) {

		struct sdp_media *sdpm = (struct sdp_media *)le->data;

		if (conv_type != ICALL_CONV_TYPE_ONEONONE) {
			sdp_set_bandwidth(sdpm, screenshare);
		}

		if (streq(sdp_media_name(sdpm), "audio")) {
			debug("sdp_modify_offer: audio_cbr=%d\n", audio_cbr);
			if (audio_cbr) {
				const struct list *fmtl;
				struct le *fle;

				fmtl = sdp_media_format_lst(sdpm, true);
				LIST_FOREACH(fmtl, fle) {
					struct sdp_format *fmt = (struct sdp_format *)fle->data;
					char *params;

					str_dup(&params, fmt->params);
					sdp_format_set_params(fmt, "%s;cbr=1", params);
					mem_deref(params);
				}
			}
		}
	}

	sdp_encode(&mbb, sess, false);
	mbuf_strdup(mbb, &sdpres, mbb->end);
	mem_deref(mbb);

	return err ? NULL : sdpres;
}

const char *sdp_sess2str(struct sdp_session *sess)
{
	char *sdpres = NULL;
	struct mbuf *mbb;
	
	sdp_encode(&mbb, sess, false);

	mbuf_strdup(mbb, &sdpres, mbb->end);
	mem_deref(mbb);

	return sdpres;
}

const char *sdp_modify_answer(struct sdp_session *sess,
			      enum icall_conv_type conv_type,
			      bool screenshare,
			      bool audio_cbr)
{
	const struct list *medial;
	struct le *le;
	
	medial = sdp_session_medial(sess, false);
	for(le = medial->head; le; le = le->next) {

		struct sdp_media *sdpm = (struct sdp_media *)le->data;

		if (conv_type != ICALL_CONV_TYPE_ONEONONE) {
			sdp_set_bandwidth(sdpm, screenshare);
		}

		if (streq(sdp_media_name(sdpm), "audio")) {
			if (conv_type != ICALL_CONV_TYPE_ONEONONE) {
                const struct list *fmtl;
				struct le *fle;

				fmtl = sdp_media_format_lst(sdpm, true);

				LIST_FOREACH(fmtl, fle) {
					struct sdp_format *fmt =
						(struct sdp_format *)fle->data;
					char *params;

					str_dup(&params, fmt->params);

					sdp_format_set_params(fmt, "%s;usedtx=1", params);
					mem_deref(params);
				}
			}

			if (audio_cbr) {
				const struct list *fmtl;
				struct le *fle;

				fmtl = sdp_media_format_lst(sdpm, true);
				LIST_FOREACH(fmtl, fle) {
					struct sdp_format *fmt = (struct sdp_format *)fle->data;
					char *params;

					str_dup(&params, fmt->params);

					sdp_format_set_params(fmt, "%s;cbr=1", params);
					mem_deref(params);
				}
			}
		}
	}	

	return sdp_sess2str(sess);
}

void sdp_check(const char *sdp,
	       bool local,
	       bool offer,
	       peerflow_acbr_h *acbrh,
	       peerflow_norelay_h *norelayh,
	       peerflow_tool_h *toolh,
	       void *arg)
{
	struct sdp_session *rsess = NULL;
	struct sa laddr;
	struct mbuf mbsdp;
	const struct list *medial;
	const char *relay;
	struct le *le;
	int err = 0;

	mbsdp.buf = (uint8_t *)sdp;
	mbsdp.pos = 0;
	mbsdp.end = str_len(sdp);
	mbsdp.size = mbsdp.end;

	sa_init(&laddr, AF_INET);
	sa_set_str(&laddr, "127.0.0.1", 9);
	err = sdp_session_alloc(&rsess, &laddr);
	if (err) {
		warning("sdp_dup: failed to alloc sdp_session: %m\n", err);
		goto out;
	}

	err = sdp_decode(rsess, &mbsdp, true);
	if (err) {
		warning("sdp_decode: failed to parse sdp: %m\n", err);
		goto out;
	}

	if (norelayh) {
		relay = sdp_session_rattr_apply(rsess, "candidate", cand_handler, NULL);
		if (!relay) {
			norelayh(local, arg);
		}
	}
	
	medial = sdp_session_medial(rsess, false);
	LIST_FOREACH(medial, le) {
		struct sdp_media *sdpm = (struct sdp_media *)le->data;

		if (streq(sdp_media_name(sdpm), "audio")) {
			const struct list *fmtl;
			struct le *fle;

			fmtl = sdp_media_format_lst(sdpm, false);
			LIST_FOREACH(fmtl, fle) {
				struct sdp_format *fmt = (struct sdp_format *)fle;

				if (fmt && fmt->params && 0 == re_regex(fmt->params, strlen(fmt->params), "cbr=1")) {

					if (acbrh) {
						acbrh(true, offer, arg);
					}

					info("sdp: remote side asking for CBR\n");
				}
			}
		}
	}

	if (toolh) {
		toolh(sdp_session_rattr(rsess, "tool"), arg);
	}
 out:
	mem_deref(rsess);
}

int sdp_strip_video(char **sdp, enum icall_conv_type conv_type,
		    const char *osdp)
{
	struct sdp_session *sess = NULL;
	int err = 0;

	if (!sdp)
		return EINVAL;

	err = sdp_dup_int(&sess, conv_type, osdp, true, true);
	if (!err) {		
		*sdp = (char *)sdp_sess2str(sess);
	}
	mem_deref(sess);

	return err;
}

