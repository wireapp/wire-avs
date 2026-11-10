/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

#include <errno.h>
#include <string.h>

#include <re.h>
#include <avs.h>

#include "cfrt.h"


enum op_type {
	OP_NEW_SESSION = 0,
	OP_TRACKS_LOCAL,
	OP_TRACKS_REMOTE,
	OP_RENEGOTIATE,
	OP_TRACKS_CLOSE,
	OP_DC_NEW,
	OP_DC_CLOSE,
};

struct op {
	struct le le;
	enum op_type type;
	char *sdp;
	char *rsess;    /* remote session id */
	char *name1;    /* audio track / dc name */
	char *name2;    /* video track */
};

struct leg {
	char *session_id;
	struct list ops;
	struct op *inflight;
	bool blocked;      /* SFU offer pending our answer */
	bool negotiated;   /* first answer received */
	struct list tracks; /* PUB: published, SUB: pulled */
};

struct cfrt {
	struct cfrt_handlers h;
	void *arg;
	struct leg legv[CFRT_LEG_COUNT];
};


static const char *op_name(enum op_type type)
{
	switch (type) {
	case OP_NEW_SESSION:   return "new-session";
	case OP_TRACKS_LOCAL:  return "tracks-local";
	case OP_TRACKS_REMOTE: return "tracks-remote";
	case OP_RENEGOTIATE:   return "renegotiate";
	case OP_TRACKS_CLOSE:  return "tracks-close";
	case OP_DC_NEW:        return "dc-new";
	case OP_DC_CLOSE:      return "dc-close";
	default:               return "???";
	}
}


const char *cfrt_leg_name(enum cfrt_leg leg)
{
	switch (leg) {
	case CFRT_LEG_PUB: return "pub";
	case CFRT_LEG_SUB: return "sub";
	default:           return "???";
	}
}


static bool valid_leg(enum cfrt_leg leg)
{
	return leg == CFRT_LEG_PUB || leg == CFRT_LEG_SUB;
}


static void op_destructor(void *arg)
{
	struct op *op = arg;

	list_unlink(&op->le);
	mem_deref(op->sdp);
	mem_deref(op->rsess);
	mem_deref(op->name1);
	mem_deref(op->name2);
}


static void track_destructor(void *arg)
{
	struct cfrt_track *t = arg;

	list_unlink(&t->le);
	mem_deref(t->mid);
	mem_deref(t->session_id);
	mem_deref(t->track_name);
	mem_deref(t->kind);
}


static int track_alloc(struct cfrt_track **tp, const char *mid,
		       const char *session_id, const char *track_name,
		       const char *kind)
{
	struct cfrt_track *t;
	int err = 0;

	t = mem_zalloc(sizeof(*t), track_destructor);
	if (!t)
		return ENOMEM;

	if (mid)
		err |= str_dup(&t->mid, mid);
	if (session_id)
		err |= str_dup(&t->session_id, session_id);
	if (track_name)
		err |= str_dup(&t->track_name, track_name);
	if (kind)
		err |= str_dup(&t->kind, kind);

	if (err)
		mem_deref(t);
	else
		*tp = t;

	return err ? ENOMEM : 0;
}


static void leg_reset(struct leg *leg)
{
	leg->session_id = mem_deref(leg->session_id);
	leg->inflight = mem_deref(leg->inflight);
	list_flush(&leg->ops);
	list_flush(&leg->tracks);
	leg->blocked = false;
	leg->negotiated = false;
}


static void destructor(void *arg)
{
	struct cfrt *rt = arg;
	int i;

	for (i = 0; i < CFRT_LEG_COUNT; ++i)
		leg_reset(&rt->legv[i]);
}


int cfrt_alloc(struct cfrt **rtp, const struct cfrt_handlers *h, void *arg)
{
	struct cfrt *rt;
	int i;

	if (!rtp || !h || !h->httph)
		return EINVAL;

	rt = mem_zalloc(sizeof(*rt), destructor);
	if (!rt)
		return ENOMEM;

	rt->h = *h;
	rt->arg = arg;
	for (i = 0; i < CFRT_LEG_COUNT; ++i) {
		list_init(&rt->legv[i].ops);
		list_init(&rt->legv[i].tracks);
	}

	*rtp = rt;

	return 0;
}


/*
 * SDP scanning
 */

struct sdp_section {
	char kind[16];
	char mid[64];
	char dir[16];
	uint32_t ssrc;
};


static void section_init(struct sdp_section *s, const struct pl *kind)
{
	memset(s, 0, sizeof(*s));
	pl_strcpy(kind, s->kind, sizeof(s->kind));
	str_ncpy(s->dir, "sendrecv", sizeof(s->dir));
}


int cfrt_sdp_media_apply(const char *sdp, cfrt_sdp_media_h *mediah,
			 void *arg)
{
	struct sdp_section sec;
	bool in_media = false;
	const char *p;

	if (!sdp || !mediah)
		return EINVAL;

	p = sdp;
	while (*p) {
		const char *eol = strchr(p, '\n');
		struct pl line, kind, val;
		size_t n = eol ? (size_t)(eol - p) : strlen(p);

		line.p = p;
		line.l = n;
		if (line.l && line.p[line.l - 1] == '\r')
			line.l--;

		if (0 == re_regex(line.p, line.l, "m=[a-z]+ ", &kind)) {
			if (in_media && mediah(sec.kind, sec.mid, sec.dir,
					       sec.ssrc, arg))
				return 0;
			section_init(&sec, &kind);
			in_media = true;
		}
		else if (in_media &&
			 0 == re_regex(line.p, line.l, "a=mid:[^ ]+", &val)) {
			pl_strcpy(&val, sec.mid, sizeof(sec.mid));
		}
		else if (in_media && !sec.ssrc &&
			 0 == re_regex(line.p, line.l, "a=ssrc:[0-9]+", &val)) {
			sec.ssrc = pl_u32(&val);
		}
		else if (in_media &&
			 (0 == pl_strcmp(&line, "a=sendrecv") ||
			  0 == pl_strcmp(&line, "a=sendonly") ||
			  0 == pl_strcmp(&line, "a=recvonly") ||
			  0 == pl_strcmp(&line, "a=inactive"))) {
			val.p = line.p + 2;
			val.l = line.l - 2;
			pl_strcpy(&val, sec.dir, sizeof(sec.dir));
		}

		p += n;
		if (*p == '\n')
			++p;
	}

	if (in_media)
		mediah(sec.kind, sec.mid, sec.dir, sec.ssrc, arg);

	return 0;
}


struct ssrc_find {
	const char *mid;
	uint32_t ssrc;
};


static bool ssrc_find_handler(const char *kind, const char *mid,
			      const char *dir, uint32_t ssrc, void *arg)
{
	struct ssrc_find *sf = arg;

	(void)kind;
	(void)dir;

	if (streq(mid, sf->mid)) {
		sf->ssrc = ssrc;
		return true;
	}

	return false;
}


uint32_t cfrt_sdp_ssrc(const char *sdp, const char *mid)
{
	struct ssrc_find sf = {
		.mid = mid,
		.ssrc = 0,
	};

	if (!sdp || !mid)
		return 0;

	cfrt_sdp_media_apply(sdp, ssrc_find_handler, &sf);

	return sf.ssrc;
}


/*
 * Request bodies
 */

static struct json_object *sdesc_alloc(const char *type, const char *sdp)
{
	struct json_object *jsd;

	jsd = jzon_alloc_object();
	if (!jsd)
		return NULL;

	jzon_add_str(jsd, "type", "%s", type);
	jzon_add_str(jsd, "sdp", "%s", sdp);

	return jsd;
}


static struct cfrt_track *find_track_mid(const struct list *tracks,
					 const char *mid)
{
	struct le *le;

	LIST_FOREACH(tracks, le) {
		struct cfrt_track *t = le->data;

		if (streq(t->mid, mid))
			return t;
	}

	return NULL;
}


struct local_tracks {
	struct leg *leg;
	struct json_object *jtracks;
	int count;
	int err;
};


static bool local_track_handler(const char *kind, const char *mid,
				const char *dir, uint32_t ssrc, void *arg)
{
	struct local_tracks *lt = arg;
	struct json_object *jt;
	struct cfrt_track *t;
	const char *name;

	(void)ssrc;

	if (streq(kind, "audio"))
		name = CFRT_TRACK_AUDIO;
	else if (streq(kind, "video"))
		name = CFRT_TRACK_VIDEO;
	else
		return false;

	if (!streq(dir, "sendonly") && !streq(dir, "sendrecv"))
		return false;

	if (!str_isset(mid) || find_track_mid(&lt->leg->tracks, mid))
		return false;

	jt = jzon_alloc_object();
	if (!jt) {
		lt->err = ENOMEM;
		return true;
	}
	jzon_add_str(jt, "location", "local");
	jzon_add_str(jt, "mid", "%s", mid);
	jzon_add_str(jt, "trackName", "%s", name);
	json_object_array_add(lt->jtracks, jt);

	lt->err = track_alloc(&t, mid, NULL, name, kind);
	if (lt->err)
		return true;
	list_append(&lt->leg->tracks, &t->le, t);
	lt->count++;

	return false;
}


static int remote_track_add(struct json_object *jtracks,
			    const char *session_id,
			    const char *name,
			    const char *kind)
{
	struct json_object *jt;

	jt = jzon_alloc_object();
	if (!jt)
		return ENOMEM;

	jzon_add_str(jt, "location", "remote");
	jzon_add_str(jt, "sessionId", "%s", session_id);
	jzon_add_str(jt, "trackName", "%s", name);
	jzon_add_str(jt, "kind", "%s", kind);
	json_object_array_add(jtracks, jt);

	return 0;
}


static int dc_body(struct json_object *jobj, const struct op *op)
{
	struct json_object *jdcs, *jdc;

	jdcs = jzon_alloc_array();
	jdc = jzon_alloc_object();
	if (!jdcs || !jdc) {
		mem_deref(jdcs);
		mem_deref(jdc);
		return ENOMEM;
	}

	jzon_add_str(jdc, "location", op->rsess ? "remote" : "local");
	if (op->rsess)
		jzon_add_str(jdc, "sessionId", "%s", op->rsess);
	jzon_add_str(jdc, "dataChannelName", "%s", op->name1);
	json_object_array_add(jdcs, jdc);
	json_object_object_add(jobj, "dataChannels", jdcs);

	return 0;
}


/* Build method, path and body of an op. *bodyp may be NULL (no body). */
static int op_request(struct leg *leg, struct op *op,
		      const char **methodp, char **pathp, char **bodyp)
{
	struct json_object *jobj = NULL;
	struct json_object *jtracks = NULL;
	const char *method = "POST";
	char *path = NULL;
	int err = 0;

	*bodyp = NULL;

	if (op->type != OP_NEW_SESSION && !leg->session_id)
		return ENOTCONN;

	jobj = jzon_alloc_object();
	if (!jobj)
		return ENOMEM;

	switch (op->type) {

	case OP_NEW_SESSION:
		err = re_sdprintf(&path, "/sessions/new");
		if (!err && op->sdp)
			json_object_object_add(jobj, "sessionDescription",
					       sdesc_alloc("offer", op->sdp));
		break;

	case OP_TRACKS_LOCAL: {
		struct local_tracks lt = {
			.leg = leg,
			.jtracks = jzon_alloc_array(),
		};

		if (!lt.jtracks) {
			err = ENOMEM;
			break;
		}
		cfrt_sdp_media_apply(op->sdp, local_track_handler, &lt);
		err = lt.err;
		if (err) {
			mem_deref(lt.jtracks);
			break;
		}
		if (lt.count == 0)
			warning("cfrt: tracks-local: no new tracks in offer\n");

		err = re_sdprintf(&path, "/sessions/%s/tracks/new",
				  leg->session_id);
		json_object_object_add(jobj, "sessionDescription",
				       sdesc_alloc("offer", op->sdp));
		json_object_object_add(jobj, "tracks", lt.jtracks);
	}
		break;

	case OP_TRACKS_REMOTE:
		jtracks = jzon_alloc_array();
		if (!jtracks) {
			err = ENOMEM;
			break;
		}
		if (op->name1)
			err |= remote_track_add(jtracks, op->rsess,
						op->name1, "audio");
		if (op->name2)
			err |= remote_track_add(jtracks, op->rsess,
						op->name2, "video");
		json_object_object_add(jobj, "tracks", jtracks);
		if (!err)
			err = re_sdprintf(&path, "/sessions/%s/tracks/new",
					  leg->session_id);
		break;

	case OP_RENEGOTIATE:
		method = "PUT";
		err = re_sdprintf(&path, "/sessions/%s/renegotiate",
				  leg->session_id);
		json_object_object_add(jobj, "sessionDescription",
				       sdesc_alloc("answer", op->sdp));
		break;

	case OP_TRACKS_CLOSE: {
		struct le *le;
		int count = 0;

		method = "PUT";
		jtracks = jzon_alloc_array();
		if (!jtracks) {
			err = ENOMEM;
			break;
		}
		LIST_FOREACH(&leg->tracks, le) {
			struct cfrt_track *t = le->data;
			struct json_object *jt;

			if (!streq(t->session_id, op->rsess) || !t->mid)
				continue;

			jt = jzon_alloc_object();
			if (!jt) {
				err = ENOMEM;
				break;
			}
			jzon_add_str(jt, "mid", "%s", t->mid);
			json_object_array_add(jtracks, jt);
			++count;
		}
		json_object_object_add(jobj, "tracks", jtracks);
		jzon_add_bool(jobj, "force", true);
		if (!err && count == 0)
			err = ENOENT;
		if (!err)
			err = re_sdprintf(&path, "/sessions/%s/tracks/close",
					  leg->session_id);
	}
		break;

	case OP_DC_NEW:
		err = dc_body(jobj, op);
		if (!err)
			err = re_sdprintf(&path, "/sessions/%s/datachannels/new",
					  leg->session_id);
		break;

	case OP_DC_CLOSE:
		method = "PUT";
		err = dc_body(jobj, op);
		if (!err)
			err = re_sdprintf(&path,
					  "/sessions/%s/datachannels/close",
					  leg->session_id);
		break;
	}

	if (err)
		goto out;

	/* sessions/new without an offer has no body */
	if (op->type != OP_NEW_SESSION || op->sdp) {
		err = jzon_encode(bodyp, jobj);
		if (err)
			goto out;
	}

	*methodp = method;
	*pathp = path;
	path = NULL;

 out:
	mem_deref(path);
	mem_deref(jobj);
	return err;
}


static void leg_fail(struct cfrt *rt, enum cfrt_leg leg, int err)
{
	struct leg *l = &rt->legv[leg];

	warning("cfrt(%p): %s failed: %m\n", rt, cfrt_leg_name(leg), err);

	l->inflight = mem_deref(l->inflight);
	list_flush(&l->ops);

	if (rt->h.errorh)
		rt->h.errorh(leg, err, rt->arg);
}


static void dispatch(struct cfrt *rt, enum cfrt_leg leg)
{
	struct leg *l = &rt->legv[leg];

	while (!l->inflight && l->ops.head) {
		struct op *op = l->ops.head->data;
		const char *method = NULL;
		char *path = NULL;
		char *body = NULL;
		int err;

		if (l->blocked && op->type != OP_RENEGOTIATE)
			return;

		list_unlink(&op->le);

		err = op_request(l, op, &method, &path, &body);
		if (err == ENOENT) {
			/* nothing to do (e.g. no tracks to close) */
			mem_deref(op);
			continue;
		}
		else if (err) {
			mem_deref(op);
			leg_fail(rt, leg, err);
			return;
		}

		info("cfrt(%p): %s %s %s %s\n", rt, cfrt_leg_name(leg),
		     op_name(op->type), method, path);

		l->inflight = op;
		if (op->type == OP_RENEGOTIATE)
			l->blocked = false;

		err = rt->h.httph(leg, method, path, body, rt->arg);
		mem_deref(path);
		mem_deref(body);
		if (err) {
			leg_fail(rt, leg, err);
			return;
		}
	}
}


static int enqueue(struct cfrt *rt, enum cfrt_leg leg, enum op_type type,
		   const char *sdp, const char *rsess,
		   const char *name1, const char *name2, bool head)
{
	struct leg *l = &rt->legv[leg];
	struct op *op;
	int err = 0;

	op = mem_zalloc(sizeof(*op), op_destructor);
	if (!op)
		return ENOMEM;

	op->type = type;
	if (sdp)
		err |= str_dup(&op->sdp, sdp);
	if (rsess)
		err |= str_dup(&op->rsess, rsess);
	if (name1)
		err |= str_dup(&op->name1, name1);
	if (name2)
		err |= str_dup(&op->name2, name2);
	if (err) {
		mem_deref(op);
		return ENOMEM;
	}

	if (head)
		list_prepend(&l->ops, &op->le, op);
	else
		list_append(&l->ops, &op->le, op);

	dispatch(rt, leg);

	return 0;
}


static bool has_op(const struct leg *l, enum op_type type)
{
	struct le *le;

	if (l->inflight && l->inflight->type == type)
		return true;

	LIST_FOREACH(&l->ops, le) {
		const struct op *op = le->data;

		if (op->type == type)
			return true;
	}

	return false;
}


int cfrt_offer(struct cfrt *rt, enum cfrt_leg leg, const char *sdp)
{
	struct leg *l;
	int err;

	if (!rt || !valid_leg(leg) || !sdp)
		return EINVAL;

	l = &rt->legv[leg];

	if (leg == CFRT_LEG_SUB) {
		if (l->session_id || has_op(l, OP_NEW_SESSION))
			return EALREADY;

		return enqueue(rt, leg, OP_NEW_SESSION, sdp,
			       NULL, NULL, NULL, false);
	}

	if (!l->session_id && !has_op(l, OP_NEW_SESSION)) {
		err = enqueue(rt, leg, OP_NEW_SESSION, NULL,
			      NULL, NULL, NULL, false);
		if (err)
			return err;
	}

	return enqueue(rt, leg, OP_TRACKS_LOCAL, sdp, NULL, NULL, NULL, false);
}


int cfrt_answer(struct cfrt *rt, enum cfrt_leg leg, const char *sdp)
{
	if (!rt || !valid_leg(leg) || !sdp)
		return EINVAL;

	return enqueue(rt, leg, OP_RENEGOTIATE, sdp, NULL, NULL, NULL, true);
}


int cfrt_subscribe(struct cfrt *rt, const char *session_id,
		   const char *audio_track, const char *video_track)
{
	if (!rt || !session_id || (!audio_track && !video_track))
		return EINVAL;

	return enqueue(rt, CFRT_LEG_SUB, OP_TRACKS_REMOTE, NULL, session_id,
		       audio_track, video_track, false);
}


int cfrt_unsubscribe(struct cfrt *rt, const char *session_id)
{
	if (!rt || !session_id)
		return EINVAL;

	return enqueue(rt, CFRT_LEG_SUB, OP_TRACKS_CLOSE, NULL, session_id,
		       NULL, NULL, false);
}


int cfrt_dc_publish(struct cfrt *rt, const char *name)
{
	if (!rt || !name)
		return EINVAL;

	return enqueue(rt, CFRT_LEG_PUB, OP_DC_NEW, NULL, NULL,
		       name, NULL, false);
}


int cfrt_dc_subscribe(struct cfrt *rt, const char *session_id,
		      const char *name)
{
	if (!rt || !session_id || !name)
		return EINVAL;

	return enqueue(rt, CFRT_LEG_SUB, OP_DC_NEW, NULL, session_id,
		       name, NULL, false);
}


int cfrt_dc_unsubscribe(struct cfrt *rt, const char *session_id,
			const char *name)
{
	if (!rt || !session_id || !name)
		return EINVAL;

	return enqueue(rt, CFRT_LEG_SUB, OP_DC_CLOSE, NULL, session_id,
		       name, NULL, false);
}


/*
 * Responses
 */

static int handle_sdesc(struct cfrt *rt, enum cfrt_leg leg,
			struct json_object *jobj,
			const char **typep, const char **sdpp)
{
	struct json_object *jsd;

	(void)rt;
	(void)leg;

	*typep = NULL;
	*sdpp = NULL;

	if (jzon_object(&jsd, jobj, "sessionDescription"))
		return 0;

	*typep = jzon_str(jsd, "type");
	*sdpp = jzon_str(jsd, "sdp");
	if (!*typep || !*sdpp)
		return EPROTO;

	return 0;
}


static int handle_remote_tracks(struct cfrt *rt, struct json_object *jobj,
				const char *offer)
{
	struct leg *l = &rt->legv[CFRT_LEG_SUB];
	struct json_object *jtracks;
	struct list added = LIST_INIT;
	int i, n;
	int err = 0;

	if (jzon_array(&jtracks, jobj, "tracks"))
		return EPROTO;

	n = json_object_array_length(jtracks);
	for (i = 0; i < n; ++i) {
		struct json_object *jt = json_object_array_get_idx(jtracks, i);
		const char *mid = jzon_str(jt, "mid");
		const char *name = jzon_str(jt, "trackName");
		const char *errc = jzon_str(jt, "errorCode");
		struct cfrt_track *t;

		if (errc || !mid) {
			warning("cfrt(%p): pull %s.%s failed: %s %s\n",
				rt, jzon_str(jt, "sessionId"), name,
				errc ? errc : "no mid",
				jzon_str(jt, "errorDescription"));
			continue;
		}

		err = track_alloc(&t, mid, jzon_str(jt, "sessionId"), name,
				  streq(name, CFRT_TRACK_VIDEO) ? "video" :
								  "audio");
		if (err)
			break;

		t->ssrc = cfrt_sdp_ssrc(offer, mid);
		list_append(&added, &t->le, t);
	}

	if (!err && rt->h.tracksh && added.head)
		rt->h.tracksh(CFRT_LEG_SUB, &added, rt->arg);

	/* move to the leg's list */
	while (added.head) {
		struct cfrt_track *t = added.head->data;

		list_unlink(&t->le);
		list_append(&l->tracks, &t->le, t);
	}

	return err;
}


static void remove_tracks(struct leg *l, const char *session_id)
{
	struct le *le = l->tracks.head;

	while (le) {
		struct cfrt_track *t = le->data;

		le = le->next;
		if (streq(t->session_id, session_id))
			mem_deref(t);
	}
}


int cfrt_response(struct cfrt *rt, enum cfrt_leg leg,
		  int status, const char *body)
{
	struct json_object *jobj = NULL;
	const char *sdtype = NULL;
	const char *sdp = NULL;
	const char *errc;
	struct leg *l;
	struct op *op;
	bool reneg;
	int err = 0;

	if (!rt || !valid_leg(leg))
		return EINVAL;

	l = &rt->legv[leg];
	op = l->inflight;
	if (!op) {
		warning("cfrt(%p): %s response without request\n",
			rt, cfrt_leg_name(leg));
		return EPROTO;
	}

	/* keep op alive until we are done, handlers may reset the leg
	 * or destroy the owner (and us)
	 */
	l->inflight = NULL;
	mem_ref(rt);

	info("cfrt(%p): %s %s response: %d\n", rt, cfrt_leg_name(leg),
	     op_name(op->type), status);

	if (status < 200 || status >= 300) {
		err = status == 401 || status == 403 ? EACCES : EPROTO;
		goto out;
	}

	if (str_isset(body)) {
		err = jzon_decode(&jobj, body, str_len(body));
		if (err) {
			err = EPROTO;
			goto out;
		}
	}
	else if (op->type != OP_RENEGOTIATE && op->type != OP_DC_CLOSE &&
		 op->type != OP_TRACKS_CLOSE) {
		err = EPROTO;
		goto out;
	}

	if (jobj) {
		errc = jzon_str(jobj, "errorCode");
		if (errc) {
			warning("cfrt(%p): %s %s error: %s %s\n", rt,
				cfrt_leg_name(leg), op_name(op->type), errc,
				jzon_str(jobj, "errorDescription"));
			err = EPROTO;
			goto out;
		}

		err = handle_sdesc(rt, leg, jobj, &sdtype, &sdp);
		if (err)
			goto out;
	}

	switch (op->type) {

	case OP_NEW_SESSION: {
		const char *sid = jzon_str(jobj, "sessionId");

		if (!sid) {
			err = EPROTO;
			goto out;
		}
		err = str_dup(&l->session_id, sid);
		if (err)
			goto out;

		if (rt->h.sessionh)
			rt->h.sessionh(leg, sid, rt->arg);
	}
		break;

	case OP_TRACKS_LOCAL:
		if (!sdp || !streq(sdtype, "answer")) {
			err = EPROTO;
			goto out;
		}
		break;

	case OP_TRACKS_REMOTE:
		reneg = jzon_bool_opt(jobj, "requiresImmediateRenegotiation",
				      false);
		if (reneg && (!sdp || !streq(sdtype, "offer"))) {
			err = EPROTO;
			goto out;
		}
		err = handle_remote_tracks(rt, jobj, sdp);
		if (err)
			goto out;
		if (reneg)
			l->blocked = true;
		break;

	case OP_TRACKS_CLOSE:
		remove_tracks(l, op->rsess);
		break;

	case OP_DC_NEW: {
		struct json_object *jdcs, *jdc;
		int id;

		if (jzon_array(&jdcs, jobj, "dataChannels") ||
		    json_object_array_length(jdcs) < 1) {
			err = EPROTO;
			goto out;
		}
		jdc = json_object_array_get_idx(jdcs, 0);
		if (jzon_str(jdc, "errorCode") || jzon_int(&id, jdc, "id")) {
			warning("cfrt(%p): dc %s failed: %s\n", rt, op->name1,
				jzon_str(jdc, "errorDescription"));
			err = EPROTO;
			goto out;
		}
		if (rt->h.dch)
			rt->h.dch(leg, op->rsess, op->name1, id, rt->arg);
	}
		break;

	case OP_RENEGOTIATE:
	case OP_DC_CLOSE:
		break;
	}

	/* Deliver SDP last: the owner may immediately answer it */
	if (sdp && (op->type == OP_NEW_SESSION ||
		    op->type == OP_TRACKS_LOCAL ||
		    op->type == OP_TRACKS_REMOTE)) {
		bool offer = streq(sdtype, "offer");
		bool initial = !offer && !l->negotiated;

		if (!offer)
			l->negotiated = true;

		if (rt->h.sdph)
			rt->h.sdph(leg, offer, initial, sdp, rt->arg);
	}

 out:
	mem_deref(jobj);
	mem_deref(op);

	if (err)
		leg_fail(rt, leg, err);
	else
		dispatch(rt, leg);

	mem_deref(rt);

	return err;
}


void cfrt_reset(struct cfrt *rt, enum cfrt_leg leg)
{
	if (!rt || !valid_leg(leg))
		return;

	leg_reset(&rt->legv[leg]);
}


const char *cfrt_session_id(const struct cfrt *rt, enum cfrt_leg leg)
{
	if (!rt || !valid_leg(leg))
		return NULL;

	return rt->legv[leg].session_id;
}


bool cfrt_busy(const struct cfrt *rt, enum cfrt_leg leg)
{
	if (!rt || !valid_leg(leg))
		return false;

	return rt->legv[leg].inflight || rt->legv[leg].ops.head;
}


const struct list *cfrt_tracks(const struct cfrt *rt, enum cfrt_leg leg)
{
	if (!rt || !valid_leg(leg))
		return NULL;

	return &rt->legv[leg].tracks;
}


int cfrt_debug(struct re_printf *pf, const struct cfrt *rt)
{
	int err = 0;
	int i;

	if (!rt)
		return 0;

	for (i = 0; i < CFRT_LEG_COUNT; ++i) {
		const struct leg *l = &rt->legv[i];
		struct le *le;

		err |= re_hprintf(pf, "\tcfrt %s: session=%s inflight=%s "
				  "queued=%u blocked=%d\n",
				  cfrt_leg_name(i),
				  l->session_id ? l->session_id : "-",
				  l->inflight ? op_name(l->inflight->type) : "-",
				  list_count(&l->ops), l->blocked);
		LIST_FOREACH(&l->tracks, le) {
			const struct cfrt_track *t = le->data;

			err |= re_hprintf(pf, "\t\ttrack mid=%s %s.%s ssrc=%u\n",
					  t->mid,
					  t->session_id ? t->session_id : "self",
					  t->track_name, t->ssrc);
		}
	}

	return err;
}
