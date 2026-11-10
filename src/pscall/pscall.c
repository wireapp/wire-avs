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

#include <assert.h>
#include <sodium.h>
#include <string.h>
#include <sys/time.h>

#include <re.h>
#include <avs.h>

#include "../catalog/catalog.h"
#include "cfrt.h"


#define PSCALL_SFU_USERID     "cloudflare"
#define PSCALL_SFU_CLIENTID   "sfu"
#define PSCALL_MAX_RECONNECTS (2)

/* A participant whose publisher session we pull */
struct peer {
	struct le le;
	char *userid;       /* hashed */
	char *clientid;     /* hashed */
	char *session_id;
	bool sub_audio;
	bool sub_video;
	bool sub_dc;
	int dc_id;          /* -1: not assigned */
	uint32_t ssrca;
	uint32_t ssrcv;
};

struct pscall {
	struct icall icall;

	char *msys_name;
	struct zapi_ice_server *turnv;
	size_t turnc;

	char *convid_real;
	char *convid_hash;

	const struct ecall_conf *conf;
	struct pscall_conf psconf;
	enum pscall_state state;

	struct userlist *userl;
	struct keystore *keystore;
	struct confsig *sig;
	struct catalog *catalog;
	struct cfrt *rt;
	struct list peers;

	struct ecall *pub;
	struct ecall *sub;
	bool sub_ready;        /* sub session negotiated */
	bool media_started;
	int pub_dc_id;
	bool pub_dc_open;
	char *announced_session;

	enum icall_call_type call_type;
	bool audio_cbr;
	enum icall_vstate vstate;
	uint64_t quality_interval;

	bool someone_joined;
	bool checking;
	uint32_t reconnects;
	int error;

	uint64_t ts_start;
	uint64_t epoch_start_ts;
	struct tmr tmr_connect;
	struct tmr tmr_alone;
	struct tmr tmr_rotate_mls;

	struct icall_metrics metrics;
};


static void join_legs_closed(struct pscall *psc);
static void end_with_err(struct pscall *psc, int err);
static int  create_leg(struct pscall *psc, enum cfrt_leg leg);
static void sync_peers(struct pscall *psc);
static void update_userlist(struct pscall *psc);
static int  sync_media_keys(struct pscall *psc);


const char *pscall_state_name(enum pscall_state state)
{
	switch (state) {
	case PSCALL_STATE_NONE:        return "NONE";
	case PSCALL_STATE_IDLE:        return "IDLE";
	case PSCALL_STATE_INCOMING:    return "INCOMING";
	case PSCALL_STATE_JOINING:     return "JOINING";
	case PSCALL_STATE_ACTIVE:      return "ACTIVE";
	case PSCALL_STATE_TERMINATING: return "TERMINATING";
	default:                       return "???";
	}
}


enum pscall_state pscall_get_state(const struct pscall *psc)
{
	return psc ? psc->state : PSCALL_STATE_NONE;
}


static uint64_t wallclock_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


static struct ecall *leg_ecall(const struct pscall *psc, enum cfrt_leg leg)
{
	return leg == CFRT_LEG_PUB ? psc->pub : psc->sub;
}


/*
 * Peers
 */

static void peer_destructor(void *arg)
{
	struct peer *p = arg;

	list_unlink(&p->le);
	mem_deref(p->userid);
	mem_deref(p->clientid);
	mem_deref(p->session_id);
}


static struct peer *peer_find(const struct pscall *psc,
			      const char *userid, const char *clientid)
{
	struct le *le;

	LIST_FOREACH(&psc->peers, le) {
		struct peer *p = le->data;

		if (streq(p->userid, userid) && streq(p->clientid, clientid))
			return p;
	}

	return NULL;
}


static struct peer *peer_find_session(const struct pscall *psc,
				      const char *session_id)
{
	struct le *le;

	LIST_FOREACH(&psc->peers, le) {
		struct peer *p = le->data;

		if (streq(p->session_id, session_id))
			return p;
	}

	return NULL;
}


static struct peer *peer_find_dc(const struct pscall *psc, int dc_id)
{
	struct le *le;

	LIST_FOREACH(&psc->peers, le) {
		struct peer *p = le->data;

		if (p->dc_id >= 0 && p->dc_id == dc_id)
			return p;
	}

	return NULL;
}


static void peer_drop(struct pscall *psc, struct peer *p, bool unsubscribe)
{
	info("pscall(%p): drop peer %s.%s session %s\n",
	     psc, p->userid, p->clientid, p->session_id);

	if (unsubscribe) {
		if (p->dc_id >= 0)
			ecall_catalog_close(psc->sub, p->dc_id);
		if (p->sub_dc)
			cfrt_dc_unsubscribe(psc->rt, p->session_id,
					    PSCALL_DC_NAME);
		if (p->sub_audio || p->sub_video)
			cfrt_unsubscribe(psc->rt, p->session_id);
	}

	mem_deref(p);
}


/*
 * State
 */

static void set_state(struct pscall *psc, enum pscall_state state)
{
	enum pscall_state old_state = psc->state;

	info("pscall(%p): State changed: `%s' --> `%s'\n", psc,
	     pscall_state_name(old_state), pscall_state_name(state));

	psc->state = state;

	if (old_state == PSCALL_STATE_IDLE && psc->ts_start == 0)
		psc->ts_start = tmr_jiffies();

	switch (state) {
	case PSCALL_STATE_IDLE:
		/* only the timestamp is cleared, seqno is kept */
		confsig_set_ts(psc->sig, 0, confsig_seqno(psc->sig));
		keystore_reset(psc->keystore);
		confsig_check_stop(psc->sig);
		psc->checking = false;
		tmr_cancel(&psc->tmr_connect);
		tmr_cancel(&psc->tmr_alone);
		tmr_cancel(&psc->tmr_rotate_mls);
		break;

	case PSCALL_STATE_INCOMING:
		keystore_reset_keys(psc->keystore);
		confsig_check_stop(psc->sig);
		psc->checking = false;
		confsig_ongoing_start(psc->sig);
		tmr_cancel(&psc->tmr_connect);
		tmr_cancel(&psc->tmr_alone);
		tmr_cancel(&psc->tmr_rotate_mls);
		break;

	case PSCALL_STATE_JOINING:
		confsig_ring_cancel(psc->sig);
		break;

	case PSCALL_STATE_ACTIVE:
		tmr_cancel(&psc->tmr_connect);
		break;

	case PSCALL_STATE_TERMINATING:
		confsig_check_stop(psc->sig);
		psc->checking = false;
		tmr_cancel(&psc->tmr_connect);
		tmr_cancel(&psc->tmr_alone);
		break;

	default:
		break;
	}

	if (state != old_state &&
	    (PSCALL_STATE_ACTIVE == state ||
	     PSCALL_STATE_ACTIVE == old_state)) {
		ICALL_CALL_CB(psc->icall, group_changedh,
			      &psc->icall, psc->icall.arg);
	}
}


static int set_secret(struct pscall *psc,
		      const uint8_t *secret,
		      size_t secretlen)
{
	int err;

	psc->convid_hash = mem_deref(psc->convid_hash);
	err = hash_conv(secret, secretlen, psc->convid_real,
			&psc->convid_hash);
	if (err)
		return err;

	err = confsig_set_secret(psc->sig, secret, secretlen,
				 psc->convid_hash);
	if (err)
		return err;

	userlist_set_secret(psc->userl, secret, secretlen);

	/* the catalog id and our hashed ids depend on the secret */
	psc->catalog = mem_deref(psc->catalog);

	return keystore_set_salt(psc->keystore,
				 (const uint8_t *)psc->convid_hash,
				 str_len(psc->convid_hash));
}


/*
 * Catalog
 */

static int send_wire_catalog(struct pscall *psc,
			     enum catalog_msg_type type,
			     struct list *targets)
{
	const struct userinfo *self;
	struct econn_message *msg = NULL;
	char *json = NULL;
	int err;

	self = userlist_get_self(psc->userl);
	if (!self || !psc->catalog)
		return ENOENT;

	err = catalog_encode(&json, type, catalog_id(psc->catalog),
			     catalog_self(psc->catalog));
	if (err)
		return err;

	msg = econn_message_alloc();
	if (!msg) {
		err = ENOMEM;
		goto out;
	}

	econn_message_init(msg, ECONN_CATALOG, psc->convid_hash);
	msg->u.catalog.json = json;
	json = NULL;

	info("pscall(%p): send %s over Wire targets: %u\n", psc,
	     catalog_msg_name(type), targets ? list_count(targets) : 0);

	err = ICALL_CALL_CBE(psc->icall, sendh,
			     &psc->icall, self->userid_real,
			     msg, targets, false, psc->icall.arg);

 out:
	mem_deref(json);
	mem_deref(msg);
	return err;
}


static int send_dc_catalog(struct pscall *psc, enum catalog_msg_type type)
{
	char *json = NULL;
	int err;

	if (!psc->pub_dc_open || !psc->catalog)
		return ENOTCONN;

	err = catalog_encode(&json, type, catalog_id(psc->catalog),
			     catalog_self(psc->catalog));
	if (err)
		return err;

	err = ecall_catalog_send(psc->pub, psc->pub_dc_id,
				 (const uint8_t *)json, str_len(json));
	mem_deref(json);

	return err;
}


/* Our entry changed: tell everyone. A new publisher session cannot be
 * announced on the old data channel, so it goes out as hello over Wire.
 */
static void announce(struct pscall *psc)
{
	struct catalog_entry *self;

	if (!psc->catalog || !psc->pub_dc_open)
		return;

	self = catalog_self(psc->catalog);
	catalog_self_changed(psc->catalog);

	if (!streq(psc->announced_session, self->session_id)) {
		psc->announced_session = mem_deref(psc->announced_session);
		str_dup(&psc->announced_session, self->session_id);
		send_wire_catalog(psc, CATALOG_MSG_HELLO, NULL);
	}

	send_dc_catalog(psc, CATALOG_MSG_UPDATE);
}


static int ensure_catalog(struct pscall *psc)
{
	const struct userinfo *self;
	int err;

	if (psc->catalog)
		return 0;

	self = userlist_get_self(psc->userl);
	if (!self || !psc->convid_hash)
		return ENOENT;

	err = catalog_alloc(&psc->catalog, psc->convid_hash,
			    self->userid_hash, self->clientid_hash);
	if (err)
		return err;

	catalog_self(psc->catalog)->joined = wallclock_ms();
	catalog_self(psc->catalog)->vstate = psc->vstate;

	return 0;
}


/* Apply a received entry to the user list (keys, video, mute) */
static void entry_to_user(struct pscall *psc, const struct catalog_entry *e)
{
	struct userinfo *u;
	bool changed = false;

	u = userlist_find_by_hash(psc->userl, e->userid, e->clientid);
	if (!u)
		return;

	if (e->vstate != (int)u->video_state) {
		u->video_state = (enum icall_vstate)e->vstate;
		ICALL_CALL_CB(psc->icall, vstate_changedh,
			      &psc->icall, u->userid_real, u->clientid_real,
			      u->video_state, psc->icall.arg);
		changed = true;
	}

	if (e->muted != u->muted) {
		u->muted = e->muted;
		changed = true;
	}

	if (e->keysync > 0 && e->keysync != u->latest_epoch) {
		u->latest_epoch = e->keysync;
		sync_media_keys(psc);
	}

	if (changed) {
		ICALL_CALL_CB(psc->icall, group_changedh,
			      &psc->icall, psc->icall.arg);
	}
}


static void handle_entry(struct pscall *psc, enum catalog_msg_type type,
			 const struct catalog_entry *e)
{
	struct catalog_entry *stored = NULL;
	bool changed = false;
	int err;

	err = catalog_apply(psc->catalog, type, e, &changed, &stored);
	if (err) {
		warning("pscall(%p): catalog_apply failed: %m\n", psc, err);
		return;
	}

	info("pscall(%p): catalog %s from %s.%s changed: %d\n", psc,
	     catalog_msg_name(type), e->userid, e->clientid, changed);

	if (!changed)
		return;

	if (stored)
		entry_to_user(psc, stored);

	sync_peers(psc);
	update_userlist(psc);
}


static bool in_call(const struct pscall *psc)
{
	return psc->state == PSCALL_STATE_JOINING ||
	       psc->state == PSCALL_STATE_ACTIVE;
}


static int recv_wire_catalog(struct pscall *psc,
			     const char *userid_sender,
			     const char *clientid_sender,
			     struct econn_message *msg)
{
	struct catalog_entry *e = NULL;
	enum catalog_msg_type type;
	char *uhash = NULL;
	const uint8_t *secret;
	size_t secretlen = 0;
	int err;

	if (!in_call(psc) || !psc->catalog || !msg->u.catalog.json)
		return 0;

	err = catalog_decode(&type, &e, catalog_id(psc->catalog),
			     msg->u.catalog.json,
			     str_len(msg->u.catalog.json));
	if (err == EBADMSG) {
		info("pscall(%p): ignoring catalog of another call\n", psc);
		return 0;
	}
	else if (err) {
		warning("pscall(%p): bad catalog message: %m\n", psc, err);
		return err;
	}

	if (type != CATALOG_MSG_HELLO && type != CATALOG_MSG_WELCOME) {
		warning("pscall(%p): ignoring catalog %s over Wire\n",
			psc, catalog_msg_name(type));
		goto out;
	}

	/* The entry must be the sender's own */
	secret = confsig_secret(psc->sig, &secretlen);
	err = hash_user(secret, secretlen, userid_sender, clientid_sender,
			&uhash);
	if (err)
		goto out;
	if (!streq(uhash, e->userid)) {
		warning("pscall(%p): catalog %s sender mismatch\n",
			psc, catalog_msg_name(type));
		goto out;
	}

	handle_entry(psc, type, e);

	if (type == CATALOG_MSG_HELLO && psc->pub_dc_open) {
		struct list targets = LIST_INIT;
		struct icall_client *c;

		c = icall_client_alloc(userid_sender, clientid_sender);
		if (c) {
			list_append(&targets, &c->le, c);
			send_wire_catalog(psc, CATALOG_MSG_WELCOME, &targets);
			list_flush(&targets);
		}
	}

 out:
	mem_deref(uhash);
	mem_deref(e);
	return err;
}


/* Reconcile pulled sessions with the catalog */
static void sync_peers(struct pscall *psc)
{
	struct list *entries;
	struct le *le;

	if (!psc->catalog)
		return;

	entries = catalog_peers(psc->catalog);

	le = psc->peers.head;
	while (le) {
		struct peer *p = le->data;
		struct catalog_entry *e;

		le = le->next;
		e = catalog_find(psc->catalog, p->userid, p->clientid);
		if (!e || !streq(e->session_id, p->session_id))
			peer_drop(psc, p, true);
	}

	if (!psc->sub_ready)
		return;

	LIST_FOREACH(entries, le) {
		struct catalog_entry *e = le->data;
		struct peer *p;
		const char *audio = NULL;
		const char *video = NULL;

		p = peer_find(psc, e->userid, e->clientid);
		if (!p) {
			p = mem_zalloc(sizeof(*p), peer_destructor);
			if (!p)
				return;
			p->dc_id = -1;
			str_dup(&p->userid, e->userid);
			str_dup(&p->clientid, e->clientid);
			str_dup(&p->session_id, e->session_id);
			list_append(&psc->peers, &p->le, p);
			info("pscall(%p): new peer %s.%s session %s\n",
			     psc, p->userid, p->clientid, p->session_id);
		}

		if (e->audio_track && !p->sub_audio) {
			audio = e->audio_track;
			p->sub_audio = true;
		}
		if (e->video_track && !p->sub_video) {
			video = e->video_track;
			p->sub_video = true;
		}
		if (audio || video)
			cfrt_subscribe(psc->rt, p->session_id, audio, video);

		if (e->dc_name && !p->sub_dc) {
			p->sub_dc = true;
			cfrt_dc_subscribe(psc->rt, p->session_id, e->dc_name);
		}
	}
}


static bool is_leader(struct pscall *psc)
{
	const struct userinfo *self;
	struct le *le;
	char selfid[128];

	self = userlist_get_self(psc->userl);
	if (!self || !psc->catalog)
		return false;

	re_snprintf(selfid, sizeof(selfid), "%s.%s",
		    self->userid_hash, self->clientid_hash);

	LIST_FOREACH(catalog_peers(psc->catalog), le) {
		const struct catalog_entry *e = le->data;
		char id[128];

		re_snprintf(id, sizeof(id), "%s.%s", e->userid, e->clientid);
		if (strcmp(id, selfid) < 0)
			return false;
	}

	return true;
}


static void alone_timeout(void *arg)
{
	struct pscall *psc = arg;

	info("pscall(%p): alone timeout someone_joined: %d\n",
	     psc, psc->someone_joined);

	end_with_err(psc, psc->someone_joined ? EEVERYONELEFT : ENOONEJOINED);
}


static void update_userlist(struct pscall *psc)
{
	const struct userinfo *self;
	struct list partl = LIST_INIT;
	struct econn_group_part *part;
	bool changed = false;
	bool self_changed = false;
	bool missing = false;
	size_t npeers = 0;
	struct le *le;

	self = userlist_get_self(psc->userl);
	if (!self || !psc->catalog)
		return;

	part = econn_part_alloc(self->userid_hash, self->clientid_hash);
	if (!part)
		return;
	part->authorized = true;
	list_append(&partl, &part->le, part);

	LIST_FOREACH(catalog_peers(psc->catalog), le) {
		const struct catalog_entry *e = le->data;
		const struct peer *p;

		part = econn_part_alloc(e->userid, e->clientid);
		if (!part)
			break;

		part->authorized = true;
		part->muted_state = e->muted ? MUTED_STATE_MUTED :
					       MUTED_STATE_UNMUTED;
		p = peer_find(psc, e->userid, e->clientid);
		if (p) {
			part->ssrca = p->ssrca;
			part->ssrcv = p->ssrcv;
		}
		list_append(&partl, &part->le, part);
		++npeers;
	}

	userlist_update_from_sftlist(psc->userl, &partl, &changed,
				     &self_changed, &missing);
	list_flush(&partl);

	if (changed) {
		ICALL_CALL_CB(psc->icall, group_changedh,
			      &psc->icall, psc->icall.arg);
	}

	if (psc->state != PSCALL_STATE_ACTIVE)
		return;

	if (npeers > 0) {
		psc->someone_joined = true;
		tmr_cancel(&psc->tmr_alone);
	}
	else if (!tmr_isrunning(&psc->tmr_alone)) {
		tmr_start(&psc->tmr_alone,
			  psc->someone_joined ? PSCALL_EVERYONE_LEFT_TIMEOUT :
						PSCALL_NOONE_JOINED_TIMEOUT,
			  alone_timeout, psc);
	}

	if (is_leader(psc)) {
		if (!psc->checking) {
			psc->checking = true;
			confsig_check_start(psc->sig, CONFSIG_SEND_CHECK_TIMEOUT);
		}
	}
	else if (psc->checking) {
		psc->checking = false;
		confsig_check_stop(psc->sig);
	}

	sync_media_keys(psc);
}


/*
 * MLS media keys
 */

static void rotate_mls_timeout(void *arg)
{
	struct pscall *psc = arg;
	uint64_t now;

	if (psc->state != PSCALL_STATE_ACTIVE)
		return;

	now = tmr_jiffies();
	if (now <= PSCALL_MLS_KEY_AGE)
		return;

	if (keystore_rotate_by_time(psc->keystore, now - PSCALL_MLS_KEY_AGE)) {
		tmr_start(&psc->tmr_rotate_mls, PSCALL_ROTATE_MLS_TIMEOUT,
			  rotate_mls_timeout, psc);
	}
}


static int sync_media_keys(struct pscall *psc)
{
	uint32_t min_key;
	uint32_t curr;
	uint64_t ts;
	int err;

	min_key = userlist_get_key_index(psc->userl);
	if (min_key == 0)
		return 0;

	err = keystore_get_current(psc->keystore, &curr, &ts);
	if (err)
		return err;

	info("pscall(%p): sync_media_keys min:%u curr:%u\n",
	     psc, min_key, curr);
	if (min_key > curr)
		keystore_set_current(psc->keystore, min_key);

	rotate_mls_timeout(psc);

	return 0;
}


static int pscall_set_media_key(struct icall *icall,
				uint32_t epochid,
				const uint8_t *key_data,
				uint32_t key_size)
{
	struct pscall *psc = (struct pscall *)icall;
	int err;

	if (!psc)
		return EINVAL;

	info("pscall(%p): set_media_key epoch %u with key %u bytes\n",
	     psc, epochid, key_size);

	err = keystore_set_session_key(psc->keystore, epochid,
				       key_data, key_size);
	if (err)
		return err;

	psc->epoch_start_ts = tmr_jiffies();
	userlist_set_latest_epoch(psc->userl, epochid);

	if (psc->catalog) {
		catalog_self(psc->catalog)->keysync = epochid;
		announce(psc);
	}

	sync_media_keys(psc);

	return 0;
}


/*
 * Legs: Cloudflare <-> ecall
 */

static int cfrt_http_handler(enum cfrt_leg leg, const char *method,
			     const char *path, const char *body, void *arg)
{
	struct pscall *psc = arg;

	if (!psc->psconf.httph)
		return ENOSYS;

	return psc->psconf.httph(psc, (enum pscall_leg)leg, method, path,
				 body, psc->psconf.arg);
}


static void cfrt_session_handler(enum cfrt_leg leg, const char *session_id,
				 void *arg)
{
	struct pscall *psc = arg;

	info("pscall(%p): %s session %s\n", psc, cfrt_leg_name(leg),
	     session_id);
}


/* Our publisher session and tracks go into our catalog entry */
static void update_self_session(struct pscall *psc)
{
	const char *audio = NULL;
	const char *video = NULL;
	struct le *le;

	if (!psc->catalog)
		return;

	LIST_FOREACH(cfrt_tracks(psc->rt, CFRT_LEG_PUB), le) {
		const struct cfrt_track *t = le->data;

		if (streq(t->kind, "audio"))
			audio = t->track_name;
		else if (streq(t->kind, "video"))
			video = t->track_name;
	}

	catalog_entry_set_session(catalog_self(psc->catalog),
				  cfrt_session_id(psc->rt, CFRT_LEG_PUB),
				  audio, video, PSCALL_DC_NAME);

	/* published tracks changed while connected */
	announce(psc);
}


static const char *leg_sessid(enum cfrt_leg leg)
{
	return leg == CFRT_LEG_PUB ? "cfrt-pub" : "cfrt-sub";
}


static void cfrt_sdp_handler(enum cfrt_leg leg, bool offer, bool initial,
			     const char *sdp, void *arg)
{
	struct pscall *psc = arg;
	struct ecall *ecall = leg_ecall(psc, leg);
	struct econn_message *msg;
	int err;

	if (!ecall)
		return;

	msg = econn_message_alloc();
	if (!msg)
		return;

	econn_message_init(msg, initial ? ECONN_SETUP : ECONN_UPDATE,
			   leg_sessid(leg));
	msg->resp = !offer;
	err = str_dup(&msg->u.setup.sdp_msg, sdp);
	if (!err) {
		err = ecall_msg_recv(ecall, 0, 0, PSCALL_SFU_USERID,
				     PSCALL_SFU_CLIENTID, msg);
	}
	mem_deref(msg);

	if (err) {
		warning("pscall(%p): %s sdp handling failed: %m\n",
			psc, cfrt_leg_name(leg), err);
		return;
	}

	if (leg == CFRT_LEG_PUB && !offer)
		update_self_session(psc);

	if (leg == CFRT_LEG_SUB && initial) {
		psc->sub_ready = true;
		sync_peers(psc);
	}
}


static void cfrt_tracks_handler(enum cfrt_leg leg, const struct list *tracks,
				void *arg)
{
	struct pscall *psc = arg;
	struct le *le;

	(void)leg;

	LIST_FOREACH(tracks, le) {
		const struct cfrt_track *t = le->data;
		struct peer *p = peer_find_session(psc, t->session_id);

		if (!p)
			continue;

		if (streq(t->kind, "video"))
			p->ssrcv = t->ssrc;
		else
			p->ssrca = t->ssrc;

		info("pscall(%p): peer %s.%s %s mid %s ssrc %u\n", psc,
		     p->userid, p->clientid, t->kind, t->mid, t->ssrc);
	}

	update_userlist(psc);
}


static void cfrt_dc_handler(enum cfrt_leg leg, const char *session_id,
			    const char *name, int id, void *arg)
{
	struct pscall *psc = arg;
	int err;

	if (leg == CFRT_LEG_PUB) {
		psc->pub_dc_id = id;
		err = ecall_catalog_open(psc->pub, id, name);
	}
	else {
		struct peer *p = peer_find_session(psc, session_id);

		if (!p)
			return;
		p->dc_id = id;
		err = ecall_catalog_open(psc->sub, id, name);
	}

	if (err) {
		warning("pscall(%p): %s catalog_open %d failed: %m\n",
			psc, cfrt_leg_name(leg), id, err);
	}
}


static void cfrt_error_handler(enum cfrt_leg leg, int err, void *arg)
{
	struct pscall *psc = arg;

	warning("pscall(%p): %s request failed: %m\n",
		psc, cfrt_leg_name(leg), err);

	end_with_err(psc, err);
}


static const struct cfrt_handlers cfrt_handlers = {
	.httph    = cfrt_http_handler,
	.sessionh = cfrt_session_handler,
	.sdph     = cfrt_sdp_handler,
	.tracksh  = cfrt_tracks_handler,
	.dch      = cfrt_dc_handler,
	.errorh   = cfrt_error_handler,
};


int pscall_http_response(struct pscall *psc, enum pscall_leg leg,
			 int status, const char *body)
{
	if (!psc)
		return EINVAL;

	return cfrt_response(psc->rt, (enum cfrt_leg)leg, status, body);
}


/*
 * ecall handlers. icall->arg is the pscall for both legs; the ecall
 * pointer tells them apart.
 */

static enum cfrt_leg icall_leg(struct pscall *psc, struct icall *icall,
			       bool *found)
{
	*found = true;
	if (psc->pub && icall == ecall_get_icall(psc->pub))
		return CFRT_LEG_PUB;
	if (psc->sub && icall == ecall_get_icall(psc->sub))
		return CFRT_LEG_SUB;

	*found = false;
	return CFRT_LEG_PUB;
}


static int ecall_send_handler(struct icall *icall, const char *userid,
			      struct econn_message *msg, struct list *targets,
			      bool my_clients_only, void *arg)
{
	struct pscall *psc = arg;
	enum cfrt_leg leg;
	bool found;
	int err = 0;

	(void)userid;
	(void)targets;
	(void)my_clients_only;

	leg = icall_leg(psc, icall, &found);
	if (!found)
		return ENOENT;

	switch (msg->msg_type) {
	case ECONN_SETUP:
	case ECONN_UPDATE:
		if (msg->resp)
			err = cfrt_answer(psc->rt, leg, msg->u.setup.sdp_msg);
		else
			err = cfrt_offer(psc->rt, leg, msg->u.setup.sdp_msg);
		break;

	default:
		/* nobody to send to on the other side */
		info("pscall(%p): %s dropping %s\n", psc, cfrt_leg_name(leg),
		     econn_msg_name(msg->msg_type));
		break;
	}

	return err;
}


static void ecall_media_estab_handler(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      bool update,
				      void *arg)
{
	struct pscall *psc = arg;
	enum cfrt_leg leg;
	bool found;

	leg = icall_leg(psc, icall, &found);
	if (!found)
		return;

	info("pscall(%p): %s media established\n", psc, cfrt_leg_name(leg));

	if (leg == CFRT_LEG_SUB) {
		if (psc->media_started)
			ecall_media_start(psc->sub);
		return;
	}

	psc->reconnects = 0;

	ICALL_CALL_CB(psc->icall, media_estabh,
		      &psc->icall, userid, clientid, update, psc->icall.arg);

	ICALL_CALL_CB(psc->icall, req_new_epochh,
		      &psc->icall, psc->icall.arg);

	if (psc->state == PSCALL_STATE_JOINING)
		set_state(psc, PSCALL_STATE_ACTIVE);

	/* now our data channel transport exists */
	if (psc->pub_dc_id < 0)
		cfrt_dc_publish(psc->rt, PSCALL_DC_NAME);

	update_userlist(psc);
}


static void ecall_audio_estab_handler(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      bool update,
				      void *arg)
{
	struct pscall *psc = arg;
	enum cfrt_leg leg;
	bool found;

	leg = icall_leg(psc, icall, &found);
	if (!found || leg != CFRT_LEG_PUB)
		return;

	ICALL_CALL_CB(psc->icall, audio_estabh,
		      &psc->icall, userid, clientid, update, psc->icall.arg);
}


static void ecall_aulevel_handler(struct icall *icall, struct list *levell,
				  void *arg)
{
	struct pscall *psc = arg;
	bool changed = false;

	(void)icall;

	userlist_update_audio_level(psc->userl, levell, &changed);
	if (changed) {
		ICALL_CALL_CB(psc->icall, group_changedh,
			      &psc->icall, psc->icall.arg);
	}

	ICALL_CALL_CB(psc->icall, audio_levelh,
		      &psc->icall, levell, psc->icall.arg);
}


static void ecall_quality_handler(struct icall *icall,
				  const char *userid,
				  const char *clientid,
				  struct stats_report stats,
				  enum icall_conv_type peer,
				  void *arg)
{
	struct pscall *psc = arg;

	(void)icall;

	if (psc->state != PSCALL_STATE_ACTIVE)
		return;

	ICALL_CALL_CB(psc->icall, qualityh,
		      &psc->icall, userid, clientid, stats, peer,
		      psc->icall.arg);
}


static void reconnect_leg(struct pscall *psc, enum cfrt_leg leg)
{
	int err;

	info("pscall(%p): reconnect %s attempt %u\n", psc,
	     cfrt_leg_name(leg), psc->reconnects + 1);

	if (++psc->reconnects > PSCALL_MAX_RECONNECTS) {
		end_with_err(psc, ETIMEDOUT);
		return;
	}

	cfrt_reset(psc->rt, leg);
	if (leg == CFRT_LEG_PUB) {
		psc->pub_dc_id = -1;
		psc->pub_dc_open = false;
	}
	else {
		psc->sub_ready = false;
		list_flush(&psc->peers);
		update_userlist(psc);
	}

	err = create_leg(psc, leg);
	if (!err) {
		err = ecall_start(leg_ecall(psc, leg),
				  leg == CFRT_LEG_SUB ? ICALL_CALL_TYPE_VIDEO :
							psc->call_type,
				  psc->audio_cbr, false);
	}
	if (err)
		end_with_err(psc, err);
}


static void ecall_close_handler(struct icall *icall,
				int err,
				struct icall_metrics *metrics,
				uint32_t msg_time,
				const char *userid,
				const char *clientid,
				void *arg)
{
	struct pscall *psc = arg;
	struct ecall *ecall = (struct ecall *)icall;
	enum cfrt_leg leg;
	bool found;

	(void)msg_time;
	(void)userid;
	(void)clientid;

	leg = icall_leg(psc, icall, &found);

	info("pscall(%p): ecall_close_handler %s err=%d state=%s\n", psc,
	     found ? cfrt_leg_name(leg) : "old", err,
	     pscall_state_name(psc->state));

	if (!found) {
		mem_deref(ecall);
		return;
	}

	if (leg == CFRT_LEG_PUB) {
		psc->pub = NULL;
		if (metrics) {
			psc->metrics.duration_active += metrics->duration_call;
			psc->metrics.packetloss_last = metrics->packetloss_last;
			psc->metrics.packetloss_max =
				MAX(metrics->packetloss_max,
				    psc->metrics.packetloss_max);
			psc->metrics.rtt_last = metrics->rtt_last;
			psc->metrics.rtt_max = MAX(metrics->rtt_max,
						   psc->metrics.rtt_max);
		}
	}
	else {
		psc->sub = NULL;
	}
	mem_deref(ecall);

	if ((err == EAGAIN || err == ENOTCONN) && in_call(psc)) {
		reconnect_leg(psc, leg);
		return;
	}

	if (psc->state != PSCALL_STATE_TERMINATING) {
		/* one leg failed: end the other one too */
		if (!psc->error)
			psc->error = err;
		set_state(psc, PSCALL_STATE_TERMINATING);
		if (psc->pub)
			ecall_end(psc->pub);
		if (psc->sub)
			ecall_end(psc->sub);
	}

	join_legs_closed(psc);
}


static void ecall_catalog_state_handler(struct ecall *ecall, int id,
					bool open, void *arg)
{
	struct pscall *psc = arg;

	info("pscall(%p): catalog dc %d on %s %s\n", psc, id,
	     ecall == psc->pub ? "pub" : "sub", open ? "open" : "closed");

	if (ecall == psc->pub && id == psc->pub_dc_id) {
		psc->pub_dc_open = open;
		if (open)
			announce(psc);
	}
	else if (ecall == psc->sub && !open) {
		struct peer *p = peer_find_dc(psc, id);
		struct catalog_entry *e;

		/* the publisher went away */
		if (p && psc->catalog) {
			e = catalog_find(psc->catalog, p->userid, p->clientid);
			if (e)
				catalog_remove(psc->catalog, e);
			p->dc_id = -1;
			sync_peers(psc);
			update_userlist(psc);
		}
	}
}


static void ecall_catalog_recv_handler(struct ecall *ecall, int id,
				       const uint8_t *data, size_t len,
				       void *arg)
{
	struct pscall *psc = arg;
	struct catalog_entry *e = NULL;
	enum catalog_msg_type type;
	struct peer *p;
	int err;

	if (ecall != psc->sub || !psc->catalog)
		return;

	p = peer_find_dc(psc, id);
	if (!p) {
		warning("pscall(%p): catalog data on unknown dc %d\n",
			psc, id);
		return;
	}

	err = catalog_decode(&type, &e, catalog_id(psc->catalog),
			     (const char *)data, len);
	if (err) {
		warning("pscall(%p): bad catalog data on dc %d: %m\n",
			psc, id, err);
		return;
	}

	/* a data channel only carries its publisher's entry */
	if ((type == CATALOG_MSG_UPDATE || type == CATALOG_MSG_BYE) &&
	    streq(e->userid, p->userid) && streq(e->clientid, p->clientid)) {
		handle_entry(psc, type, e);
	}
	else {
		warning("pscall(%p): ignoring catalog %s on dc %d\n",
			psc, catalog_msg_name(type), id);
	}

	mem_deref(e);
}


static int create_leg(struct pscall *psc, enum cfrt_leg leg)
{
	struct msystem *msys = msystem_instance();
	const struct userinfo *self;
	struct ecall *ecall = NULL;
	enum icall_call_type call_type;
	size_t i;
	int err;

	self = userlist_get_self(psc->userl);
	if (!self)
		return ENOENT;

	assert(leg_ecall(psc, leg) == NULL);

	call_type = leg == CFRT_LEG_SUB ? ICALL_CALL_TYPE_VIDEO :
					  psc->call_type;

	err = ecall_alloc(&ecall, NULL, ICALL_CONV_TYPE_CONFERENCE,
			  call_type, psc->conf, psc->msys_name, msys,
			  psc->convid_real,
			  self->userid_hash, self->clientid_hash);
	if (err)
		return err;

	icall_set_callbacks(ecall_get_icall(ecall),
			    ecall_send_handler,
			    NULL, // sfth
			    NULL, // starth
			    NULL, // answerh
			    ecall_media_estab_handler,
			    ecall_audio_estab_handler,
			    NULL, // datachan_estabh
			    NULL, // media_stoppedh
			    NULL, // group_changedh
			    NULL, // leaveh
			    ecall_close_handler,
			    NULL, // metricsh
			    NULL, // vstate_changedh
			    NULL, // acbr_changedh
			    NULL, // muted_changedh
			    ecall_quality_handler,
			    NULL, // norelayh
			    NULL, // req_clientsh
			    ecall_aulevel_handler,
			    NULL, // req_new_epochh
			    psc);

	err = ecall_set_media_direction(ecall, leg == CFRT_LEG_PUB ?
					ECALL_MEDIA_SENDONLY :
					ECALL_MEDIA_RECVONLY);
	if (err)
		goto out;

	/* SFU offers renegotiate the existing PeerConnection */
	ecall_set_update_inplace(ecall, true);
	ecall_set_catalog_handlers(ecall, ecall_catalog_state_handler,
				   ecall_catalog_recv_handler);
	ecall_set_keystore(ecall, psc->keystore);

	err = ecall_set_quality_interval(ecall, psc->quality_interval);
	if (err)
		goto out;

	err = ecall_set_real_clientid(ecall, self->clientid_real);
	if (err)
		goto out;

	for (i = 0; i < psc->turnc; ++i) {
		err = ecall_add_turnserver(ecall, &psc->turnv[i]);
		if (err)
			goto out;
	}

	if (psc->convid_hash)
		ecall_set_sessid(ecall, psc->convid_hash);

	if (leg == CFRT_LEG_PUB) {
		psc->pub = ecall;
		if (psc->vstate != ICALL_VIDEO_STATE_STOPPED)
			ecall_set_video_send_state(ecall, psc->vstate);
	}
	else {
		psc->sub = ecall;
	}

	info("pscall(%p): created %s ecall: %p\n", psc, cfrt_leg_name(leg),
	     ecall);

 out:
	if (err)
		mem_deref(ecall);

	return err;
}


/*
 * Joining and leaving
 */

static void connect_timeout(void *arg)
{
	struct pscall *psc = arg;

	info("pscall(%p): connect timeout state=%s\n", psc,
	     pscall_state_name(psc->state));

	if (psc->state != PSCALL_STATE_ACTIVE)
		end_with_err(psc, ETIMEDOUT);
}


static int join(struct pscall *psc)
{
	struct catalog_entry *self;
	int err;

	err = ensure_catalog(psc);
	if (err)
		return err;

	self = catalog_self(psc->catalog);
	self->joined = wallclock_ms();
	self->version = 0;
	self->vstate = psc->vstate;
	self->muted = msystem_get_muted();

	if (psc->call_type == ICALL_CALL_TYPE_NORMAL &&
	    (psc->vstate == ICALL_VIDEO_STATE_STARTED ||
	     psc->vstate == ICALL_VIDEO_STATE_SCREENSHARE)) {
		psc->call_type = ICALL_CALL_TYPE_VIDEO;
	}

	psc->error = 0;
	psc->reconnects = 0;
	psc->someone_joined = false;
	psc->pub_dc_id = -1;
	psc->pub_dc_open = false;
	psc->sub_ready = false;
	psc->announced_session = mem_deref(psc->announced_session);
	cfrt_reset(psc->rt, CFRT_LEG_PUB);
	cfrt_reset(psc->rt, CFRT_LEG_SUB);

	set_state(psc, PSCALL_STATE_JOINING);
	tmr_start(&psc->tmr_connect, PSCALL_CONNECT_TIMEOUT,
		  connect_timeout, psc);

	err = create_leg(psc, CFRT_LEG_PUB);
	if (!err)
		err = create_leg(psc, CFRT_LEG_SUB);
	if (!err)
		err = ecall_start(psc->pub, psc->call_type,
				  psc->audio_cbr, false);
	if (!err)
		err = ecall_start(psc->sub, ICALL_CALL_TYPE_VIDEO,
				  psc->audio_cbr, false);

	return err;
}


/* Drop all legs without notifying the app (switching calls) */
static void teardown(struct pscall *psc)
{
	struct ecall *pub = psc->pub;
	struct ecall *sub = psc->sub;

	send_dc_catalog(psc, CATALOG_MSG_BYE);

	/* closing ecalls are no longer ours: the close handler derefs them */
	psc->pub = NULL;
	psc->sub = NULL;
	if (pub) {
		if (ecall_get_econn(pub))
			ecall_end(pub);
		else
			mem_deref(pub);
	}
	if (sub) {
		if (ecall_get_econn(sub))
			ecall_end(sub);
		else
			mem_deref(sub);
	}

	cfrt_reset(psc->rt, CFRT_LEG_PUB);
	cfrt_reset(psc->rt, CFRT_LEG_SUB);
	list_flush(&psc->peers);
	catalog_flush(psc->catalog);
	psc->media_started = false;
	psc->pub_dc_id = -1;
	psc->pub_dc_open = false;
	psc->sub_ready = false;
	userlist_incall_clear(psc->userl, false, false);
}


/* Both legs are gone: finish leaving */
static void join_legs_closed(struct pscall *psc)
{
	size_t npeers;

	if (psc->pub || psc->sub)
		return;

	/* already finished (an ecall may close inside ecall_end) */
	if (psc->state != PSCALL_STATE_TERMINATING)
		return;

	npeers = psc->catalog ? list_count(catalog_peers(psc->catalog)) : 0;

	info("pscall(%p): legs closed err=%d peers=%zu\n",
	     psc, psc->error, npeers);

	cfrt_reset(psc->rt, CFRT_LEG_PUB);
	cfrt_reset(psc->rt, CFRT_LEG_SUB);
	list_flush(&psc->peers);
	catalog_flush(psc->catalog);
	psc->media_started = false;
	userlist_incall_clear(psc->userl, false, false);

	psc->metrics.duration_call = psc->ts_start ?
		(tmr_jiffies() - psc->ts_start) / 1000 : 0;

	if (npeers == 0) {
		switch (psc->error) {
		case 0:
		case ENOONEJOINED:
		case EEVERYONELEFT:
			confsig_end(psc->sig);
			break;
		default:
			break;
		}

		set_state(psc, PSCALL_STATE_IDLE);
		/* closeh may destroy us */
		ICALL_CALL_CB(psc->icall, closeh,
			      &psc->icall, psc->error, &psc->metrics,
			      (uint32_t)time(NULL), NULL, NULL,
			      psc->icall.arg);
		return;
	}

	psc->error = 0;
	set_state(psc, PSCALL_STATE_INCOMING);
	ICALL_CALL_CB(psc->icall, leaveh,
		      &psc->icall, ICALL_REASON_STILL_ONGOING,
		      (uint32_t)time(NULL), psc->icall.arg);
}


static void end_with_err(struct pscall *psc, int err)
{
	int reason;

	info("pscall(%p): end err=%d state=%s\n", psc, err,
	     pscall_state_name(psc->state));

	switch (err) {
	case 0:             reason = ICALL_REASON_NORMAL; break;
	case ETIMEDOUT:     reason = ICALL_REASON_TIMEOUT; break;
	case ENOONEJOINED:  reason = ICALL_REASON_NOONE_JOINED; break;
	case EEVERYONELEFT: reason = ICALL_REASON_EVERYONE_LEFT; break;
	case EACCES:        reason = ICALL_REASON_AUTH_FAILED; break;
	default:            reason = ICALL_REASON_ERROR; break;
	}

	switch (psc->state) {
	case PSCALL_STATE_NONE:
	case PSCALL_STATE_IDLE:
	case PSCALL_STATE_TERMINATING:
		return;

	case PSCALL_STATE_INCOMING:
		set_state(psc, PSCALL_STATE_IDLE);
		ICALL_CALL_CB(psc->icall, leaveh, &psc->icall, reason,
			      (uint32_t)time(NULL), psc->icall.arg);
		return;

	case PSCALL_STATE_JOINING:
	case PSCALL_STATE_ACTIVE:
		break;
	}

	psc->error = err;
	set_state(psc, PSCALL_STATE_TERMINATING);
	send_dc_catalog(psc, CATALOG_MSG_BYE);

	/* without an econn an ecall never reports closing */
	if (psc->pub && !ecall_get_econn(psc->pub))
		psc->pub = mem_deref(psc->pub);
	if (psc->sub && !ecall_get_econn(psc->sub))
		psc->sub = mem_deref(psc->sub);

	mem_ref(psc);
	if (psc->pub)
		ecall_end(psc->pub);
	if (psc->sub)
		ecall_end(psc->sub);

	join_legs_closed(psc);
	mem_deref(psc);
}


/*
 * confsig handlers
 */

static enum confsig_phase confsig_phase_handler(void *arg)
{
	struct pscall *psc = arg;

	switch (psc->state) {
	case PSCALL_STATE_IDLE:     return CONFSIG_PHASE_IDLE;
	case PSCALL_STATE_INCOMING: return CONFSIG_PHASE_INCOMING;
	case PSCALL_STATE_JOINING:  return CONFSIG_PHASE_JOINING;
	case PSCALL_STATE_ACTIVE:   return CONFSIG_PHASE_ACTIVE;
	default:                    return CONFSIG_PHASE_NONE;
	}
}


static bool confsig_leader_handler(void *arg)
{
	return is_leader(arg);
}


static int confsig_send_handler(struct econn_message *msg,
				struct list *targets,
				bool my_clients_only,
				void *arg)
{
	struct pscall *psc = arg;
	const struct userinfo *self;

	self = userlist_get_self(psc->userl);
	if (!self)
		return ENOENT;

	return ICALL_CALL_CBE(psc->icall, sendh,
			      &psc->icall, self->userid_real,
			      msg, targets, my_clients_only,
			      psc->icall.arg);
}


static int confsig_encode_handler(struct econn_message *msg, void *arg)
{
	struct pscall *psc = arg;
	int err;

	if (ECONN_CONF_START == msg->msg_type) {
		err = str_dup(&msg->u.confstart.backend, PSCALL_BACKEND);
		if (err)
			return err;

		err = econn_props_alloc(&msg->u.confstart.props, NULL);
		if (err)
			return err;

		return econn_props_add(msg->u.confstart.props, "videosend",
				       psc->call_type == ICALL_CALL_TYPE_VIDEO ?
				       "true" : "false");
	}
	else if (ECONN_CONF_CHECK == msg->msg_type) {
		return str_dup(&msg->u.confcheck.backend, PSCALL_BACKEND);
	}

	return 0;
}


static int confsig_adopt_handler(const struct econn_message *msg,
				 const uint8_t *secret,
				 size_t secretlen,
				 void *arg)
{
	struct pscall *psc = arg;

	(void)msg;

	info("pscall(%p): set_secret from %s\n", psc,
	     econn_msg_name(msg->msg_type));

	keystore_reset(psc->keystore);

	return set_secret(psc, secret, secretlen);
}


static void confsig_update_handler(const struct econn_message *msg,
				   void *arg)
{
	(void)msg;
	(void)arg;
}


static void confsig_incoming_handler(uint32_t msg_time,
				     const char *userid,
				     const char *clientid,
				     bool should_ring,
				     void *arg)
{
	struct pscall *psc = arg;

	set_state(psc, PSCALL_STATE_INCOMING);

	ICALL_CALL_CB(psc->icall, starth,
		      &psc->icall, msg_time, userid, clientid,
		      false, should_ring, ICALL_CONV_TYPE_CONFERENCE_MLS,
		      psc->icall.arg);
}


static int confsig_switch_handler(const struct econn_message *msg,
				  void *arg)
{
	struct pscall *psc = arg;

	(void)msg;

	info("pscall(%p): switching to earlier call\n", psc);

	teardown(psc);
	if (psc->state != PSCALL_STATE_ACTIVE)
		psc->metrics.initiator = false;

	return join(psc);
}


static void confsig_leave_handler(int reason, uint32_t msg_time, void *arg)
{
	struct pscall *psc = arg;

	ICALL_CALL_CB(psc->icall, leaveh,
		      &psc->icall, reason, msg_time, psc->icall.arg);
}


static void confsig_close_handler(uint32_t msg_time, void *arg)
{
	struct pscall *psc = arg;

	set_state(psc, PSCALL_STATE_IDLE);
	ICALL_CALL_CB(psc->icall, closeh,
		      &psc->icall, 0, &psc->metrics, msg_time,
		      NULL, NULL, psc->icall.arg);
}


static void confsig_check_handler(void *arg)
{
	struct pscall *psc = arg;
	uint64_t now = tmr_jiffies();

	if (psc->epoch_start_ts > 0 &&
	    now - psc->epoch_start_ts >= PSCALL_REQ_NEW_EPOCH_TIMEOUT) {
		info("pscall(%p): epoch too old, calling req_new_epochh\n",
		     psc);
		ICALL_CALL_CB(psc->icall, req_new_epochh,
			      &psc->icall, psc->icall.arg);
	}
}


static const struct confsig_handlers confsig_handlers = {
	.phaseh    = confsig_phase_handler,
	.leaderh   = confsig_leader_handler,
	.sendh     = confsig_send_handler,
	.encodeh   = confsig_encode_handler,
	.adopth    = confsig_adopt_handler,
	.updateh   = confsig_update_handler,
	.incomingh = confsig_incoming_handler,
	.switchh   = confsig_switch_handler,
	.leaveh    = confsig_leave_handler,
	.closeh    = confsig_close_handler,
	.checkh    = confsig_check_handler,
};


/*
 * icall functions
 */

static int pscall_add_turnserver(struct icall *icall,
				 struct zapi_ice_server *srv)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc || !srv)
		return EINVAL;

	if (psc->turnc >= MAX_TURN_SERVERS)
		return EOVERFLOW;

	psc->turnv[psc->turnc++] = *srv;

	return 0;
}


static int pscall_start(struct icall *icall,
			enum icall_call_type call_type,
			bool audio_cbr,
			bool meeting)
{
	struct pscall *psc = (struct pscall *)icall;
	int err;

	if (!psc)
		return EINVAL;

	confsig_set_meeting(psc->sig, meeting);

	switch (psc->state) {
	case PSCALL_STATE_INCOMING:
		warning("pscall(%p): start in state %s, answering instead\n",
			psc, pscall_state_name(psc->state));
		return ICALL_CALLE(icall, answer, call_type, audio_cbr);

	case PSCALL_STATE_IDLE:
		break;

	default:
		warning("pscall(%p): start in state %s\n",
			psc, pscall_state_name(psc->state));
		return 0;
	}

	psc->call_type = call_type;
	psc->audio_cbr = audio_cbr;
	psc->metrics.initiator = true;

	err = join(psc);
	if (err)
		goto out;

	/* We start the call: no SFU hands out the timestamp */
	err = confsig_start(psc->sig, wallclock_ms(), 0);

 out:
	if (err)
		end_with_err(psc, err);

	return err;
}


static int pscall_answer(struct icall *icall,
			 enum icall_call_type call_type,
			 bool audio_cbr)
{
	struct pscall *psc = (struct pscall *)icall;
	int err;

	if (!psc)
		return EINVAL;

	switch (psc->state) {
	case PSCALL_STATE_IDLE:
		warning("pscall(%p): answer in state %s, trying anyway\n",
			psc, pscall_state_name(psc->state));
		break;

	case PSCALL_STATE_INCOMING:
		break;

	default:
		warning("pscall(%p): answer in state %s\n",
			psc, pscall_state_name(psc->state));
		return EINVAL;
	}

	psc->call_type = call_type;
	psc->audio_cbr = audio_cbr;
	psc->metrics.initiator = false;

	confsig_set_stop_ringing(psc->sig, CONFSIG_STOP_RINGING_ANSWERED);
	confsig_stop_others_ringing(psc->sig);
	confsig_ongoing_stop(psc->sig);

	err = join(psc);
	if (err)
		end_with_err(psc, err);

	return err;
}


static void pscall_end(struct icall *icall)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc)
		return;

	end_with_err(psc, 0);
}


static void pscall_reject(struct icall *icall)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc)
		return;

	info("pscall(%p): reject state=%s\n", psc,
	     pscall_state_name(psc->state));

	if (psc->state == PSCALL_STATE_INCOMING) {
		confsig_set_stop_ringing(psc->sig,
					 CONFSIG_STOP_RINGING_REJECTED);
		confsig_stop_others_ringing(psc->sig);
	}
}


static int pscall_media_start(struct icall *icall)
{
	struct pscall *psc = (struct pscall *)icall;

	psc->media_started = true;
	ecall_media_start(psc->pub);
	ecall_media_start(psc->sub);

	return 0;
}


static void pscall_media_stop(struct icall *icall)
{
	struct pscall *psc = (struct pscall *)icall;

	psc->media_started = false;
	ecall_media_stop(psc->pub);
	ecall_media_stop(psc->sub);
}


static int pscall_set_vstate(struct icall *icall, enum icall_vstate state)
{
	struct pscall *psc = (struct pscall *)icall;
	const struct userinfo *self;
	int err = 0;

	if (!psc)
		return EINVAL;

	info("pscall(%p): set_vstate %s\n", psc, icall_vstate_name(state));

	psc->vstate = state;

	self = userlist_get_self(psc->userl);
	if (self) {
		ICALL_CALL_CB(psc->icall, vstate_changedh, &psc->icall,
			      self->userid_real, self->clientid_real,
			      state, psc->icall.arg);
	}
	ICALL_CALL_CB(psc->icall, group_changedh,
		      &psc->icall, psc->icall.arg);

	/* TODO: video turned on in an audio-only publisher needs a video
	 * track and a renegotiation (tracks/new local)
	 */
	if (psc->pub)
		err = ecall_set_video_send_state(psc->pub, state);

	if (psc->catalog) {
		catalog_self(psc->catalog)->vstate = state;
		announce(psc);
	}

	return err;
}


static int pscall_msg_recv(struct icall *icall,
			   uint32_t curr_time,
			   uint32_t msg_time,
			   const char *userid_sender,
			   const char *clientid_sender,
			   struct econn_message *msg)
{
	struct pscall *psc = (struct pscall *)icall;

	(void)curr_time;

	if (!psc || !msg)
		return EINVAL;

	switch (msg->msg_type) {
	case ECONN_CONF_START:
	case ECONN_CONF_CHECK:
		if (!streq(confsig_msg_backend(msg), PSCALL_BACKEND)) {
			info("pscall(%p): msg_recv ignoring %s of backend %s\n",
			     psc, econn_msg_name(msg->msg_type),
			     confsig_msg_backend(msg) ?
			     confsig_msg_backend(msg) : "sft");
			return 0;
		}
		/* fall through */
	case ECONN_CONF_END:
	case ECONN_REJECT:
		return confsig_recv(psc->sig, msg_time,
				    userid_sender, clientid_sender, msg);

	case ECONN_CATALOG:
		return recv_wire_catalog(psc, userid_sender,
					 clientid_sender, msg);

	case ECONN_CONF_KEY:
		/* Proteus only */
		return 0;

	default:
		info("pscall(%p): msg_recv ignoring %s\n", psc,
		     econn_msg_name(msg->msg_type));
		return 0;
	}
}


static int pscall_get_members(struct icall *icall,
			      struct wcall_members **mmp)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc)
		return EINVAL;

	return userlist_get_members(psc->userl, mmp,
				    psc->state == PSCALL_STATE_ACTIVE ?
				    ICALL_AUDIO_STATE_ESTABLISHED :
				    ICALL_AUDIO_STATE_CONNECTING,
				    psc->vstate);
}


static int pscall_set_quality_interval(struct icall *icall,
				       uint64_t interval)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc)
		return EINVAL;

	psc->quality_interval = interval;
	if (psc->pub)
		ecall_set_quality_interval(psc->pub, interval);
	if (psc->sub)
		ecall_set_quality_interval(psc->sub, interval);

	return 0;
}


static void pscall_set_clients(struct icall *icall,
			       struct list *clientl,
			       uint32_t epoch)
{
	struct pscall *psc = (struct pscall *)icall;
	bool changed = false;
	bool removed = false;
	const uint8_t *secret;
	size_t secretlen = 0;

	if (!psc)
		return;

	secret = confsig_secret(psc->sig, &secretlen);
	userlist_update_from_selist(psc->userl, clientl, epoch,
				    (uint8_t *)secret, secretlen,
				    &changed, &removed);

	/* clients that left the subconversation are out of the call */
	if (removed && psc->catalog) {
		struct le *le = catalog_peers(psc->catalog)->head;

		while (le) {
			struct catalog_entry *e = le->data;
			const struct userinfo *u;

			le = le->next;
			u = userlist_find_by_hash(psc->userl,
						  e->userid, e->clientid);
			if (!u || !u->in_subconv)
				catalog_remove(psc->catalog, e);
		}
		sync_peers(psc);
	}

	if (changed || removed)
		update_userlist(psc);

	if (changed) {
		ICALL_CALL_CB(psc->icall, group_changedh,
			      &psc->icall, psc->icall.arg);
	}

	if (confsig_stop_ringing_pending(psc->sig))
		confsig_stop_others_ringing(psc->sig);
}


static int pscall_update_mute_state(const struct icall *icall)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc)
		return EINVAL;

	ICALL_CALL_CB(psc->icall, group_changedh,
		      &psc->icall, psc->icall.arg);

	if (psc->catalog) {
		catalog_self(psc->catalog)->muted = msystem_get_muted();
		announce(psc);
	}

	return psc->pub ? ecall_update_mute_state(psc->pub) : 0;
}


static int pscall_request_video_streams(struct icall *icall,
					struct list *clientl,
					enum icall_stream_mode mode)
{
	(void)icall;
	(void)clientl;
	(void)mode;

	/* TODO: pull only the requested video tracks */
	return 0;
}


static int pscall_debug(struct re_printf *pf, const struct icall *icall)
{
	const struct pscall *psc = (const struct pscall *)icall;
	int err = 0;

	if (!psc)
		return 0;

	err |= re_hprintf(pf, "\npscall(%p): state=%s convid_hash=%s\n",
			  psc, pscall_state_name(psc->state),
			  psc->convid_hash ? psc->convid_hash : "-");
	err |= re_hprintf(pf, "\tpub=%p sub=%p sub_ready=%d pub_dc=%d/%d "
			  "peers=%u\n", psc->pub, psc->sub, psc->sub_ready,
			  psc->pub_dc_id, psc->pub_dc_open,
			  list_count(&psc->peers));
	err |= cfrt_debug(pf, psc->rt);
	err |= catalog_debug(pf, psc->catalog);
	err |= userlist_debug(pf, psc->userl);

	return err;
}


static int pscall_stats(struct re_printf *pf, const struct icall *icall)
{
	const struct pscall *psc = (const struct pscall *)icall;
	int err = 0;

	if (!psc)
		return 0;

	if (psc->pub)
		err |= ecall_stats(pf, psc->pub);
	if (psc->sub)
		err |= ecall_stats(pf, psc->sub);

	return err;
}


static int pscall_set_background(struct icall *icall, bool background)
{
	struct pscall *psc = (struct pscall *)icall;

	if (!psc)
		return EINVAL;

	if (psc->state == PSCALL_STATE_INCOMING) {
		if (background)
			confsig_ongoing_stop(psc->sig);
		else
			confsig_ongoing_start(psc->sig);
	}

	return 0;
}


static int pscall_activate(struct icall *icall, bool active)
{
	struct pscall *psc = (struct pscall *)icall;

	if (psc->pub)
		ecall_activate(psc->pub, active);
	if (psc->sub)
		ecall_activate(psc->sub, active);

	return 0;
}


static int pscall_restart(struct icall *icall)
{
	struct pscall *psc = (struct pscall *)icall;

	info("pscall(%p): restart\n", psc);

	/* conference ecalls report ENOTCONN through closeh -> reconnect */
	if (psc->pub)
		ecall_restart(psc->pub, psc->call_type, false);
	if (psc->sub)
		ecall_restart(psc->sub, ICALL_CALL_TYPE_VIDEO, false);

	return 0;
}


/*
 * Allocation
 */

static void destructor(void *arg)
{
	struct pscall *psc = arg;

	tmr_cancel(&psc->tmr_connect);
	tmr_cancel(&psc->tmr_alone);
	tmr_cancel(&psc->tmr_rotate_mls);

	/* no requests from a dying call */
	cfrt_reset(psc->rt, CFRT_LEG_PUB);
	cfrt_reset(psc->rt, CFRT_LEG_SUB);

	mem_deref(psc->pub);
	mem_deref(psc->sub);
	list_flush(&psc->peers);

	mem_deref(psc->rt);
	mem_deref(psc->catalog);
	mem_deref(psc->sig);
	mem_deref(psc->userl);
	mem_deref(psc->keystore);

	mem_deref(psc->announced_session);
	mem_deref(psc->turnv);
	mem_deref(psc->convid_real);
	mem_deref(psc->convid_hash);
	mem_deref(psc->msys_name);
}


static void userlist_add_user_handler(const struct userinfo *user, void *arg)
{
	struct pscall *psc = arg;

	if (psc->sub) {
		ecall_add_decoders_for_user(psc->sub,
					    user->userid_real,
					    user->clientid_real,
					    user->userid_hash,
					    user->ssrca, user->ssrcv);
	}

	if (user->video_state != ICALL_VIDEO_STATE_STOPPED &&
	    user->ssrcv > 0) {
		ICALL_CALL_CB(psc->icall, vstate_changedh,
			      &psc->icall, user->userid_real,
			      user->clientid_real, user->video_state,
			      psc->icall.arg);
	}
}


static void userlist_remove_user_handler(const struct userinfo *user,
					 bool call_vstateh,
					 void *arg)
{
	struct pscall *psc = arg;

	if (psc->sub) {
		ecall_remove_decoders_for_user(psc->sub,
					       user->userid_real,
					       user->clientid_real,
					       user->ssrca, user->ssrcv);
	}

	if (call_vstateh && user->video_state != ICALL_VIDEO_STATE_STOPPED) {
		ICALL_CALL_CB(psc->icall, vstate_changedh,
			      &psc->icall, user->userid_real,
			      user->clientid_real, ICALL_VIDEO_STATE_STOPPED,
			      psc->icall.arg);
	}
}


static void userlist_sync_users_handler(void *arg)
{
	struct pscall *psc = arg;

	if (psc->sub)
		ecall_sync_decoders(psc->sub);
}


int pscall_alloc(struct pscall **pscp,
		 const struct ecall_conf *econf,
		 const char *msys_name,
		 const char *convid,
		 const char *userid_self,
		 const char *clientid,
		 const struct pscall_conf *conf)
{
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	uint8_t secret[PSCALL_SECRET_LEN];
	struct pscall *psc;
	int err;

	if (!pscp || !convid || !userid_self || !clientid || !conf)
		return EINVAL;

	psc = mem_zalloc(sizeof(*psc), destructor);
	if (!psc)
		return ENOMEM;

	info("pscall(%p): alloc msys: %s convid: %s userid: %s clientid: %s\n",
	     psc, msys_name,
	     anon_id(convid_anon, convid),
	     anon_id(userid_anon, userid_self),
	     anon_client(clientid_anon, clientid));

	psc->conf = econf;
	psc->psconf = *conf;
	psc->state = PSCALL_STATE_IDLE;
	psc->pub_dc_id = -1;
	psc->metrics.conv_type = ICALL_CONV_TYPE_CONFERENCE_MLS;
	list_init(&psc->peers);
	tmr_init(&psc->tmr_connect);
	tmr_init(&psc->tmr_alone);
	tmr_init(&psc->tmr_rotate_mls);

	err = str_dup(&psc->convid_real, convid);
	if (!err && msys_name)
		err = str_dup(&psc->msys_name, msys_name);
	if (err)
		goto out;

	psc->turnv = mem_zalloc(MAX_TURN_SERVERS * sizeof(*psc->turnv), NULL);
	if (!psc->turnv) {
		err = ENOMEM;
		goto out;
	}

	err = userlist_alloc(&psc->userl, userid_self, clientid,
			     userlist_add_user_handler,
			     userlist_remove_user_handler,
			     userlist_sync_users_handler,
			     NULL, // kgchangeh: no key generator in MLS
			     NULL, // vstateh
			     psc);
	if (err)
		goto out;

	err = keystore_alloc(&psc->keystore, false);
	if (err)
		goto out;

	err = confsig_alloc(&psc->sig, psc->userl, true,
			    &confsig_handlers, psc);
	if (err)
		goto out;

	err = cfrt_alloc(&psc->rt, &cfrt_handlers, psc);
	if (err)
		goto out;

	randombytes_buf(secret, sizeof(secret));
	err = set_secret(psc, secret, sizeof(secret));
	if (err)
		goto out;

	icall_set_functions(&psc->icall,
			    pscall_add_turnserver,
			    NULL, // add_sft
			    pscall_start,
			    pscall_answer,
			    pscall_end,
			    pscall_reject,
			    pscall_media_start,
			    pscall_media_stop,
			    NULL, // set_media_laddr
			    pscall_set_vstate,
			    pscall_msg_recv,
			    NULL, // sft_msg_recv
			    pscall_get_members,
			    pscall_set_quality_interval,
			    NULL, // dce_send
			    pscall_set_clients,
			    pscall_update_mute_state,
			    pscall_request_video_streams,
			    pscall_set_media_key,
			    pscall_debug,
			    pscall_stats,
			    pscall_set_background,
			    pscall_activate,
			    NULL, // set_duration
			    pscall_restart);

 out:
	if (err)
		mem_deref(psc);
	else
		*pscp = psc;

	return err;
}


struct icall *pscall_get_icall(struct pscall *psc)
{
	return psc ? &psc->icall : NULL;
}
