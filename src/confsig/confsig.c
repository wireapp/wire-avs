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

#include <re.h>
#include <avs.h>


struct confsig {
	struct userlist *userl;
	bool is_mls;
	bool meeting;

	struct confsig_handlers h;
	void *arg;

	uint8_t *secret;
	size_t secret_len;
	char *sessid;

	uint64_t timestamp;
	uint32_t seqno;

	bool is_ringing;
	enum confsig_stop_ringing stop_ringing_reason;

	struct tmr tmr_ring;
	struct tmr tmr_ongoing;
	struct tmr tmr_send_check;
};


static enum confsig_phase get_phase(const struct confsig *sig)
{
	return sig->h.phaseh(sig->arg);
}


static bool is_leader(const struct confsig *sig)
{
	return sig->h.leaderh(sig->arg);
}


static void destructor(void *arg)
{
	struct confsig *sig = arg;

	tmr_cancel(&sig->tmr_ring);
	tmr_cancel(&sig->tmr_ongoing);
	tmr_cancel(&sig->tmr_send_check);

	mem_deref(sig->secret);
	mem_deref(sig->sessid);
}


const char *confsig_phase_name(enum confsig_phase phase)
{
	switch (phase) {
	case CONFSIG_PHASE_NONE:
		return "NONE";
	case CONFSIG_PHASE_IDLE:
		return "IDLE";
	case CONFSIG_PHASE_INCOMING:
		return "INCOMING";
	case CONFSIG_PHASE_JOINING:
		return "JOINING";
	case CONFSIG_PHASE_ACTIVE:
		return "ACTIVE";
	default:
		return "???";
	}
}


static int send_msg(struct confsig *sig,
		    enum econn_msg type,
		    bool resp,
		    struct list *targets,
		    bool my_clients_only)
{
	struct econn_message *msg = NULL;
	const struct userinfo *self = NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	self = userlist_get_self(sig->userl);
	if (!self)
		return ENOENT;

	if (targets || my_clients_only) {
		info("confsig(%p): send_msg type: %s targets: %zu "
		     "my_clients_only: %s\n",
		     sig,
		     econn_msg_name(type),
		     list_count(targets),
		     my_clients_only ? "YES" : "NO");
	}
	else {
		info("confsig(%p): send_msg type: %s userid=%s clientid=%s\n",
		     sig,
		     econn_msg_name(type),
		     anon_id(userid_anon, self->userid_real),
		     anon_client(clientid_anon, self->clientid_real));
	}

	msg = econn_message_alloc();
	if (!msg)
		return ENOMEM;

	str_ncpy(msg->src_userid, self->userid_real, ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, self->clientid_real, ECONN_ID_LEN);
	msg->msg_type = type;
	msg->resp = resp;
	msg->transient = false;

	switch (type) {
	case ECONN_CONF_START:
		msg->u.confstart.timestamp = sig->timestamp;
		msg->u.confstart.seqno = sig->seqno;
		msg->u.confstart.secret = mem_zalloc(sig->secret_len, NULL);
		if (!msg->u.confstart.secret) {
			err = ENOMEM;
			goto out;
		}
		memcpy(msg->u.confstart.secret, sig->secret, sig->secret_len);
		msg->u.confstart.secretlen = sig->secret_len;
		str_ncpy(msg->sessid_sender, sig->sessid, ECONN_ID_LEN);

		err = sig->h.encodeh(msg, sig->arg);
		break;

	case ECONN_CONF_CHECK:
		msg->u.confcheck.timestamp = sig->timestamp;
		msg->u.confcheck.seqno = sig->seqno;
		msg->u.confcheck.secret = mem_alloc(sig->secret_len, NULL);
		if (!msg->u.confcheck.secret) {
			err = ENOMEM;
			goto out;
		}
		memcpy(msg->u.confcheck.secret, sig->secret, sig->secret_len);
		msg->u.confcheck.secretlen = sig->secret_len;
		str_ncpy(msg->sessid_sender, sig->sessid, ECONN_ID_LEN);

		err = sig->h.encodeh(msg, sig->arg);
		break;

	case ECONN_CONF_END:
		str_ncpy(msg->sessid_sender, sig->sessid, ECONN_ID_LEN);
		break;

	default:
		break;
	}
	if (err) {
		warning("confsig(%p): failed to alloc message: %m\n", sig, err);
		goto out;
	}

	err = sig->h.sendh(msg, targets, my_clients_only, sig->arg);

 out:
	mem_deref(msg);

	return err;
}


static void send_check_timeout(void *arg)
{
	struct confsig *sig = arg;
	enum confsig_phase phase = get_phase(sig);
	int err;

	if (CONFSIG_PHASE_ACTIVE == phase && is_leader(sig)) {
		info("confsig(%p): send_check phase=%s\n",
		     sig, confsig_phase_name(phase));

		err = send_msg(sig, ECONN_CONF_CHECK, true, NULL, false);
		if (err) {
			warning("confsig(%p): send_check failed to send msg "
				"err=%d\n", sig, err);
		}

		if (sig->h.checkh)
			sig->h.checkh(sig->arg);
	}

	tmr_start(&sig->tmr_send_check,
		  CONFSIG_SEND_CHECK_TIMEOUT,
		  send_check_timeout, sig);
}


static void ongoing_call_timeout(void *arg)
{
	struct confsig *sig = arg;

	if (CONFSIG_PHASE_INCOMING == get_phase(sig)) {
		info("confsig(%p): ongoing_call_timeout\n", sig);

		/* owner may be destroyed here */
		sig->h.closeh(time(NULL), sig->arg);
	}
}


static void stop_ringing_timeout(void *arg)
{
	struct confsig *sig = arg;

	if (CONFSIG_PHASE_INCOMING == get_phase(sig)) {
		info("confsig(%p): stop_ringing_timeout\n", sig);
		sig->is_ringing = false;
		sig->h.leaveh(ICALL_REASON_STILL_ONGOING, time(NULL),
			      sig->arg);
	}
}


int confsig_alloc(struct confsig **sigp,
		  struct userlist *userl,
		  bool is_mls,
		  const struct confsig_handlers *h,
		  void *arg)
{
	struct confsig *sig;

	if (!sigp || !userl || !h)
		return EINVAL;

	if (!h->phaseh || !h->leaderh || !h->sendh || !h->encodeh ||
	    !h->adopth || !h->updateh || !h->incomingh || !h->switchh ||
	    !h->leaveh || !h->closeh)
		return EINVAL;

	sig = mem_zalloc(sizeof(*sig), destructor);
	if (!sig)
		return ENOMEM;

	sig->userl = userl;
	sig->is_mls = is_mls;
	sig->h = *h;
	sig->arg = arg;
	sig->stop_ringing_reason = CONFSIG_STOP_RINGING_NONE;

	tmr_init(&sig->tmr_ring);
	tmr_init(&sig->tmr_ongoing);
	tmr_init(&sig->tmr_send_check);

	*sigp = sig;

	return 0;
}


int confsig_set_secret(struct confsig *sig,
		       const uint8_t *secret,
		       size_t secretlen,
		       const char *sessid)
{
	int err = 0;

	if (!sig || !secret)
		return EINVAL;

	sig->secret = mem_deref(sig->secret);
	sig->secret_len = 0;

	sig->secret = mem_zalloc(secretlen, NULL);
	if (!sig->secret)
		return ENOMEM;

	memcpy(sig->secret, secret, secretlen);
	sig->secret_len = secretlen;

	sig->sessid = mem_deref(sig->sessid);
	if (sessid)
		err = str_dup(&sig->sessid, sessid);

	return err;
}


const uint8_t *confsig_secret(const struct confsig *sig, size_t *lenp)
{
	if (!sig)
		return NULL;

	if (lenp)
		*lenp = sig->secret_len;

	return sig->secret;
}


void confsig_set_meeting(struct confsig *sig, bool meeting)
{
	if (sig)
		sig->meeting = meeting;
}


uint64_t confsig_timestamp(const struct confsig *sig)
{
	return sig ? sig->timestamp : 0;
}


uint32_t confsig_seqno(const struct confsig *sig)
{
	return sig ? sig->seqno : 0;
}


void confsig_set_ts(struct confsig *sig, uint64_t timestamp, uint32_t seqno)
{
	if (!sig)
		return;

	sig->timestamp = timestamp;
	sig->seqno = seqno;
}


int confsig_start(struct confsig *sig, uint64_t timestamp, uint32_t seqno)
{
	if (!sig)
		return EINVAL;

	sig->timestamp = timestamp;
	sig->seqno = seqno;

	info("confsig(%p): sending CONFSTART\n", sig);
	return send_msg(sig, ECONN_CONF_START, false, NULL, false);
}


int confsig_end(struct confsig *sig)
{
	if (!sig)
		return EINVAL;

	return send_msg(sig, ECONN_CONF_END, false, NULL, false);
}


static int handle_confstart_check(struct confsig *sig,
				  const char *userid_sender,
				  const char *clientid_sender,
				  struct econn_message *msg)
{
	enum confsig_phase phase;
	int ts_cmp = 0;
	uint64_t msg_ts;
	uint32_t msg_seqno;
	size_t msg_secretlen;
	const uint8_t *msg_secret;
	bool valid_call, should_ring;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	const struct userinfo *self = NULL;
	int err = 0;

	self = userlist_get_self(sig->userl);
	if (!self)
		return ENOENT;

	if (ECONN_CONF_START == msg->msg_type) {
		msg_ts = msg->u.confstart.timestamp;
		msg_seqno = msg->u.confstart.seqno;
		msg_secret = msg->u.confstart.secret;
		msg_secretlen = msg->u.confstart.secretlen;

		valid_call = msg->age < CONFSIG_CONFSTART_TIMEOUT_S;
	}
	else {
		msg_ts = msg->u.confcheck.timestamp;
		msg_seqno = msg->u.confcheck.seqno;
		msg_secret = msg->u.confcheck.secret;
		msg_secretlen = msg->u.confcheck.secretlen;

		valid_call = !econn_message_isrequest(msg) &&
			(msg->age < CONFSIG_CONFSTART_TIMEOUT_S);
	}

	phase = get_phase(sig);

	if (!valid_call) {
		info("confsig(%p): handle_confstart_check ignore %s of age %u "
		     "phase: %s\n", sig, econn_msg_name(msg->msg_type),
		     msg->age, confsig_phase_name(phase));
		return 0;
	}

	if (sig->timestamp == 0 || msg_ts < sig->timestamp) {
		ts_cmp = 1;
	}
	else if (msg_ts > sig->timestamp) {
		ts_cmp = -1;
	}
	else {
		ts_cmp = sig->seqno - msg_seqno;
	}

	info("confsig(%p): handle_confstart_check %s "
	     "from: %s.%s phase: %s ts_cmp: %d "
	     "(msg %llu.%u < local %llu.%u) is_leader: %s\n",
	     sig,
	     econn_msg_name(msg->msg_type),
	     anon_id(userid_anon, userid_sender),
	     anon_client(clientid_anon, clientid_sender),
	     confsig_phase_name(phase),
	     ts_cmp,
	     msg_ts, msg_seqno,
	     sig->timestamp, sig->seqno,
	     is_leader(sig) ? "YES" : "NO");

	if (ts_cmp > 0) {
		err = sig->h.adopth(msg, msg_secret, msg_secretlen, sig->arg);
		if (err)
			return err;

		sig->timestamp = msg_ts;
		sig->seqno = msg_seqno;
	}

	switch (phase) {
	case CONFSIG_PHASE_IDLE:
		should_ring = econn_message_isrequest(msg) &&
			ECONN_CONF_START == msg->msg_type &&
			((msg->age * 1000) < CONFSIG_SHOULD_RING_TIMEOUT);
		if (strcaseeq(userid_sender, self->userid_real))
			should_ring = false;

		if (sig->meeting)
			should_ring = false;

		sig->is_ringing = should_ring;
		sig->h.incomingh(msg->time, userid_sender, clientid_sender,
				 should_ring, sig->arg);

		if (should_ring) {
			tmr_start(&sig->tmr_ring,
				  CONFSIG_RINGER_TIMEOUT,
				  stop_ringing_timeout, sig);
		}

		if (ts_cmp >= 0)
			sig->h.updateh(msg, sig->arg);
		break;

	case CONFSIG_PHASE_INCOMING:
		if (strcaseeq(userid_sender, self->userid_real)) {
			if (sig->is_ringing) {
				tmr_cancel(&sig->tmr_ring);
				sig->is_ringing = false;
				sig->h.leaveh(ICALL_REASON_ANSWERED_ELSEWHERE,
					      msg->time, sig->arg);
			}
		}
		confsig_ongoing_start(sig);

		if (ts_cmp >= 0)
			sig->h.updateh(msg, sig->arg);
		break;

	case CONFSIG_PHASE_JOINING:
		/* If remote call is earlier, drop connection and
		   reconnect to the earlier call */
		if (ts_cmp > 0)
			return sig->h.switchh(msg, sig->arg);
		break;

	case CONFSIG_PHASE_ACTIVE:
		if (ts_cmp > 0) {
			/* If remote call is earlier, drop connection and
			   reconnect to the earlier call */
			return sig->h.switchh(msg, sig->arg);
		}
		else if (ts_cmp < 0 && is_leader(sig)) {
			/* If local call is earlier. send new CONFSTART to
			   assert dominance */
			info("confsig(%p): sending CONFSTART from "
			     "incoming CONFSTART\n", sig);
			send_msg(sig, ECONN_CONF_START, true, NULL, false);
		}
		break;

	case CONFSIG_PHASE_NONE:
		break;
	}

	return 0;
}


static void handle_confend(struct confsig *sig,
			   uint32_t msg_time,
			   struct econn_message *msg)
{
	enum confsig_phase phase = get_phase(sig);

	switch (phase) {
	case CONFSIG_PHASE_INCOMING:
		if (sig->sessid &&
		    strncmp(msg->sessid_sender, sig->sessid,
			    ECONN_ID_LEN) == 0) {
			/* owner may be destroyed here */
			sig->h.closeh(msg_time, sig->arg);
		}
		else {
			info("confsig(%p): msg_recv ignoring CONFEND "
			     "from wrong session\n", sig);
		}
		break;

	case CONFSIG_PHASE_ACTIVE:
		/* Inform clients that the call is in fact still ongoing */
		if (is_leader(sig))
			confsig_check_start(sig, 0);
		break;

	default:
		info("confsig(%p): msg_recv ignoring CONFEND "
		     "due to phase %s\n", sig, confsig_phase_name(phase));
		break;
	}
}


static void handle_reject(struct confsig *sig,
			  uint32_t msg_time,
			  const char *userid_sender)
{
	const struct userinfo *self = userlist_get_self(sig->userl);

	if (self && CONFSIG_PHASE_INCOMING == get_phase(sig) &&
	    strcaseeq(userid_sender, self->userid_real)) {
		tmr_cancel(&sig->tmr_ring);
		sig->is_ringing = false;
		sig->h.leaveh(ICALL_REASON_STILL_ONGOING, msg_time, sig->arg);
	}
}


int confsig_recv(struct confsig *sig,
		 uint32_t msg_time,
		 const char *userid_sender,
		 const char *clientid_sender,
		 struct econn_message *msg)
{
	if (!sig || !msg || !userid_sender || !clientid_sender)
		return EINVAL;

	switch (msg->msg_type) {
	case ECONN_CONF_START:
	case ECONN_CONF_CHECK:
		return handle_confstart_check(sig, userid_sender,
					      clientid_sender, msg);

	case ECONN_CONF_END:
		handle_confend(sig, msg_time, msg);
		return 0;

	case ECONN_REJECT:
		handle_reject(sig, msg_time, userid_sender);
		return 0;

	default:
		return EPROTO;
	}
}


void confsig_check_start(struct confsig *sig, uint64_t delay)
{
	if (!sig)
		return;

	tmr_cancel(&sig->tmr_send_check);
	tmr_start(&sig->tmr_send_check, delay, send_check_timeout, sig);
}


void confsig_check_now(struct confsig *sig)
{
	if (sig)
		send_check_timeout(sig);
}


void confsig_check_stop(struct confsig *sig)
{
	if (sig)
		tmr_cancel(&sig->tmr_send_check);
}


void confsig_ongoing_start(struct confsig *sig)
{
	if (!sig)
		return;

	tmr_cancel(&sig->tmr_ongoing);
	tmr_start(&sig->tmr_ongoing, CONFSIG_ONGOING_CALL_TIMEOUT,
		  ongoing_call_timeout, sig);
}


void confsig_ongoing_stop(struct confsig *sig)
{
	if (sig)
		tmr_cancel(&sig->tmr_ongoing);
}


void confsig_ring_cancel(struct confsig *sig)
{
	if (sig)
		tmr_cancel(&sig->tmr_ring);
}


void confsig_set_stop_ringing(struct confsig *sig,
			      enum confsig_stop_ringing reason)
{
	if (sig)
		sig->stop_ringing_reason = reason;
}


bool confsig_stop_ringing_pending(const struct confsig *sig)
{
	return sig && sig->stop_ringing_reason != CONFSIG_STOP_RINGING_NONE;
}


void confsig_stop_others_ringing(struct confsig *sig)
{
	struct list targets = LIST_INIT;
	enum econn_msg msgtype;
	int err = 0;

	if (!sig)
		return;

	switch (sig->stop_ringing_reason) {
	case CONFSIG_STOP_RINGING_ANSWERED:
		msgtype = ECONN_CONF_START;
		break;

	case CONFSIG_STOP_RINGING_REJECTED:
		msgtype = ECONN_REJECT;
		break;

	default:
		return;
	}

	info("confsig(%p): stop_others_ringing reason=%d\n",
	     sig, sig->stop_ringing_reason);

	sig->stop_ringing_reason = CONFSIG_STOP_RINGING_NONE;

	err = userlist_get_my_clients(sig->userl, &targets);
	if (err) {
		warning("confsig(%p): stop_others_ringing get_my_clients "
			"failed (%m)\n", sig, err);
		goto out;
	}
	info("confsig(%p): stop_others_ringing phase=%s targets=%u\n",
	     sig,
	     confsig_phase_name(get_phase(sig)),
	     list_count(&targets));

	if (sig->is_mls || list_count(&targets) > 0)
		send_msg(sig, msgtype, true, &targets, true);

 out:
	list_flush(&targets);
}
