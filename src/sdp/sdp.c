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

enum {
      AUDIO_ONEONE_BANDWIDTH = 50,
      AUDIO_GROUP_BANDWIDTH = 32,
      VIDEO_ONEONE_BANDWIDTH = 800,
      VIDEO_GROUP_BANDWIDTH = 300,
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


static bool fmt_handler(struct sdp_format *fmt, void *arg)
{
	struct sdp_media *sdpm = (struct sdp_media *)arg;

	if (streq(sdp_media_name(sdpm), "audio")) {
		if (streq(fmt->name, "opus")) {
			sdp_format_add(NULL, sdpm, false,
				       fmt->id, fmt->name, fmt->srate, fmt->ch,
				       NULL, NULL, NULL, false,
				       "%s", fmt->params);
		}
	}
	else if (streq(sdp_media_name(sdpm), "video")) {
		if (strcaseeq(fmt->name, "vp8")) {
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



static bool media_rattr_handler(const char *name, const char *value, void *arg)
{
	struct conv_sdp *csdp = (struct conv_sdp *)arg;
	struct sdp_media *sdpm = csdp->sdpm;

	if (streq(name, "extmap")) {
		int xid;
		char rval[256];
			
		sscanf(value, "%d %255s", &xid, rval);

		if (strstr(rval, "generic-frame-descriptor")) {
			if (csdp->conv_type != ICALL_CONV_TYPE_CONFERENCE)
				return false;
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
			return false;
	}
	else {
		sdp_safe_media_set_lattr(sdpm, false, name, value);
	}
			
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

		rport = sdp_media_rport(sdpm);
		sdp_media_add(NULL, sess, mname,
			      rport, sdp_media_proto(sdpm));

		sdp_media_set_disabled(sdpm, false);
		sdp_media_set_laddr(sdpm, sdp_media_raddr(sdpm));
		sdp_media_set_lport(sdpm, rport);

		sdp_media_format_apply(sdpm, false, NULL, -1, NULL,
				       -1, -1, fmt_handler, sdpm);

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


const char *sdp_modify_offer(struct sdp_session *sess,
			     enum icall_conv_type conv_type,
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

		if (streq(sdp_media_name(sdpm), "video")) {
			uint32_t bw = conv_type == ICALL_CONV_TYPE_ONEONONE ?
				VIDEO_ONEONE_BANDWIDTH : VIDEO_GROUP_BANDWIDTH;

			sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS, bw);
		}
		else if (streq(sdp_media_name(sdpm), "audio")) {
			uint32_t bw = conv_type == ICALL_CONV_TYPE_ONEONONE ?
				AUDIO_ONEONE_BANDWIDTH : AUDIO_GROUP_BANDWIDTH;
			sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS, bw);

			if (conv_type != ICALL_CONV_TYPE_ONEONONE) {

				info("sdp_modify_offer: group mode, "
				     "setting ptime\n");
				sdp_media_set_lattr(sdpm, true, "ptime", "40");
			}

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
			      bool audio_cbr)
{
	const struct list *medial;
	struct le *le;
	
	medial = sdp_session_medial(sess, false);
	for(le = medial->head; le; le = le->next) {

		struct sdp_media *sdpm = (struct sdp_media *)le->data;
		
		if (streq(sdp_media_name(sdpm), "video")) {
			uint32_t bw = conv_type == ICALL_CONV_TYPE_ONEONONE ?
				VIDEO_ONEONE_BANDWIDTH : VIDEO_GROUP_BANDWIDTH;
			sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS, bw);
		}
		else if (streq(sdp_media_name(sdpm), "audio")) {
			uint32_t bw = conv_type == ICALL_CONV_TYPE_ONEONONE ?
				AUDIO_ONEONE_BANDWIDTH : AUDIO_GROUP_BANDWIDTH;
			sdp_media_set_lbandwidth(sdpm, SDP_BANDWIDTH_AS, bw);

			if (conv_type != ICALL_CONV_TYPE_ONEONONE) {
				const struct list *fmtl;
				struct le *fle;

				sdp_media_set_lattr(sdpm, true, "ptime", "40");
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

