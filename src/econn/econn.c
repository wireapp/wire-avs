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

#include <assert.h>
#include <string.h>
#include <re.h>
#include "avs_base.h"
#include "avs_log.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_econn.h"
#include "econn.h"


/* prototypes */


static const struct econn_conf default_conf = {
	.timeout_setup = 60000,
	.timeout_term  =  5000,
};


static int send_cancel(struct econn *conn);
static void tmr_local_handler(void *arg);


/* NOTE: Should only be triggered by async events! */
void econn_close(struct econn *conn, int err, uint32_t msg_time)
{
	econn_close_h *closeh;

	if (!conn)
		return;

	assert(ECONN_MAGIC == conn->magic);

	closeh = conn->closeh;

	if (err) {
		info("econn(%p): connection closed (%m)\n", conn, err);
	}
	else {
		info("econn(%p): connection closed (normal)\n", conn);
	}

	tmr_cancel(&conn->tmr_local);

	conn->setup_err = err;

	if (conn->state == ECONN_PENDING_OUTGOING) {

		send_cancel(conn);
	}

	conn->state = ECONN_TERMINATING;

	/* NOTE: calling the callback handlers MUST be done last,
	 *       to make sure that all states are correct.
	 */
	if (closeh) {
		conn->closeh = NULL;
		closeh(conn, err, msg_time, conn->arg);
	}

	/* NOTE here the app should have destroyed the econn */
}


int econn_transp_send(struct econn *conn, struct econn_message *msg)
{
	int err;

	if (!conn->transp || !conn->transp->sendh)
		return ENOTSUP;

	err = conn->transp->sendh(conn, msg, conn->transp->arg);
	if (err)
		return err;

	return 0;
}


static int econn_send_setup(struct econn *conn, bool resp, const char *sdp,
			    const struct econn_props *props, bool update)
{
	struct econn_message msg;
	enum econn_msg mtype;
	int err = 0;

	if (!conn)
		return EINVAL;

	mtype = update ? ECONN_UPDATE : ECONN_SETUP;
	err = econn_message_init(&msg, mtype, conn->sessid_local);
	if (err)
		return err;

	msg.u.setup.sdp_msg = (char *)sdp;
	msg.u.setup.props = (struct econn_props *)props;

	msg.resp = resp;

	/* NOTE: calling the callback handlers MUST be done last,
	 *       to make sure that all states are correct.
	 */
	err = econn_transp_send(conn, &msg);
	if (err) {
		warning("econn(%p): send_setup: transp_send failed (%m)\n",
			conn, err);
		goto out;
	}

 out:
	if (err) {
		conn->setup_err = err;
		econn_set_state(conn, ECONN_TERMINATING);
	}

	return err;
}


static int send_cancel(struct econn *conn)
{
	struct econn_message msg;
	int err = 0;

	err = econn_message_init(&msg, ECONN_CANCEL, conn->sessid_local);
	if (err)
		return err;

	/* NOTE: calling the callback handlers MUST be done last,
	 *       to make sure that all states are correct.
	 */
	err = econn_transp_send(conn, &msg);
	if (err)
		return err;

	return 0;
}


static int send_hangup(struct econn *conn, bool resp)
{
	struct econn_message msg;
	int err;

	err = econn_message_init(&msg, ECONN_HANGUP, conn->sessid_local);
	if (err)
		return err;
	msg.resp = resp;

	err = econn_transp_send(conn, &msg);
	if (err)
		return err;

	return 0;
}


static void econn_destructor(void *data)
{
	struct econn *conn = data;

	mem_deref(conn->clientid);
	mem_deref(conn->userid_self);

	tmr_cancel(&conn->tmr_local);
	conn->magic = 0;
}


static void handle_setup_request(struct econn *econn,
				 const char *userid_sender,
				 const char *clientid_sender,
				 const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	bool is_winner;

	switch (econn->state) {

	case ECONN_IDLE:
		break;

	case ECONN_PENDING_OUTGOING:
		is_winner = econn_iswinner(econn->userid_self, econn->clientid,
					   userid_sender, clientid_sender);

		info("econn(%p): [%s] conflict: is_winner=%d\n",
		     econn,
		     anon_id(userid_anon, econn->userid_remote), is_winner);

		str_ncpy(econn->sessid_remote, msg->sessid_sender,
			 sizeof(econn->sessid_remote));

		if (is_winner) {
			/* We are winner, drop remote offer
			 * and expect new ANSWER from peer */
			econn->conflict = 1;
		}
		else { /* We are Looser -- drop our offer,
		       * we must send a new ANSWER */

			econn->conflict = -1;

			econn_set_state(econn, ECONN_CONFLICT_RESOLUTION);

			/* calling this handler will trigger the
			   sending of a new SETUP */
			if (econn->answerh) {
				econn->answerh(econn, true,
					       msg->u.setup.sdp_msg,
					       msg->u.setup.props,
					       econn->arg);
			}
		}
		return;

	default:
		warning("[ %s.%s ] econn(%p): recv_setup: "
			"ignore received SETUP Request "
			"in wrong state '%s'\n",
			anon_id(userid_anon, econn->userid_self),
			anon_client(clientid_anon, econn->clientid),
			econn,
			econn_state_name(econn->state));
		return;
	}

	econn_set_state(econn, ECONN_PENDING_INCOMING);
	econn->dir = ECONN_DIR_INCOMING;

	str_ncpy(econn->sessid_remote, msg->sessid_sender,
		 sizeof(econn->sessid_remote));

	tmr_start(&econn->tmr_local, econn->conf.timeout_setup,
		  tmr_local_handler, econn);

	if (econn->connh) {
		econn->connh(econn,
			     msg->time,
			     userid_sender,
			     clientid_sender,
			     msg->age,
			     msg->u.setup.sdp_msg, msg->u.setup.props,
			     econn->arg);
	}
}


static void handle_setup_response(struct econn *econn,
				  const char *userid_sender,
				  const char *clientid_sender,
				  const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	/* todo: use a proper state machine */
	if (econn->state != ECONN_PENDING_OUTGOING &&
	    econn->state != ECONN_CONFLICT_RESOLUTION) {

		info("econn(%p): recv_setup: ignore received SETUP(r)"
		     " from %s|%s in wrong state '%s'\n",
		     econn,
		     anon_id(userid_anon, userid_sender),
		     anon_client(clientid_anon, clientid_sender),
		     econn_state_name(econn->state));

		return;
	}

	tmr_cancel(&econn->tmr_local);

	econn_set_state(econn, ECONN_ANSWERED);

	str_ncpy(econn->sessid_remote, msg->sessid_sender,
		 sizeof(econn->sessid_remote));

	if (econn->answerh) {
		econn->answerh(econn, false, msg->u.setup.sdp_msg,
			       msg->u.setup.props, econn->arg);
	}
}


static void recv_setup(struct econn *econn,
		       const char *userid_sender,
		       const char *clientid_sender,
		       const struct econn_message *msg)
{
	char userid_anon1[ANON_ID_LEN];
	char userid_anon2[ANON_ID_LEN];
	char clientid_anon1[ANON_CLIENT_LEN];
	char clientid_anon2[ANON_CLIENT_LEN];

	if (!econn || !msg)
		return;

	/* check if the Remote UserID is set */
	if (str_isset(econn->userid_remote)) {

		if (0 != str_casecmp(econn->userid_remote,
				     userid_sender)) {

			info("econn(%p): recv_setup:"
			     " remote UserID already set to `%s'"
			     " - dropping message with `%s'\n",
			     econn,
			     anon_id(userid_anon1, econn->userid_remote),
			     anon_id(userid_anon2,  userid_sender));
			return;
		}
	}
	else {
		str_ncpy(econn->userid_remote, userid_sender,
			 sizeof(econn->userid_remote));
	}
	/* check if the Remote ClientID is set */
	if (str_isset(econn->clientid_remote)) {

		if (0 != str_casecmp(econn->clientid_remote,
				     clientid_sender)) {

			info("econn(%p): recv_setup:"
			     " remote ClientID already set to `%s'"
			     " - dropping message with `%s'\n",
			     econn,
			     anon_client(clientid_anon1,
					 econn->clientid_remote),
			     anon_client(clientid_anon2, clientid_sender));
			return;
		}
	}
	else {
		str_ncpy(econn->clientid_remote, clientid_sender,
			 sizeof(econn->clientid_remote));
	}

	if (econn_message_isrequest(msg)) {

		handle_setup_request(econn, userid_sender,
				     clientid_sender, msg);
	}
	else { /* Response */

		handle_setup_response(econn, userid_sender,
				      clientid_sender, msg);
	}
}


static void handle_update_request(struct econn *econn,
				  const char *userid_sender,
				  const char *clientid_sender,
				  const struct econn_message *msg)
{
	bool is_winner;
	bool should_reset = false;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon1[ANON_CLIENT_LEN];
	char clientid_anon2[ANON_CLIENT_LEN];

	/* check if the Remote ClientID is correct */
	if (0 != str_casecmp(econn->clientid_remote, clientid_sender)) {
		warning("econn(%p): ignoring  UPDATE-resp from wrong client. "
			"Expected: %s got: %s\n",
			econn,
		        anon_client(clientid_anon1, econn->clientid_remote), 
			anon_client(clientid_anon2, clientid_sender));

		return;
	}

	switch (econn->state) {

	case ECONN_ANSWERED:
	case ECONN_DATACHAN_ESTABLISHED:
		econn_set_state(econn, ECONN_UPDATE_RECV);
		break;

	case ECONN_UPDATE_SENT:
		is_winner = econn_iswinner(econn->userid_self, econn->clientid,
					   userid_sender, clientid_sender);

		info("econn(%p): handle_update_request: "
		     "[%s] conflict: is_winner=%d\n",
		     econn,
		     anon_id(userid_anon, econn->userid_remote), is_winner);

		if (is_winner) {
			/* We are winner, drop remote offer
			 * and expect new ANSWER from peer */
			/*econn->conflict = 1;*/

			return;
		}
		else { /* We are Looser -- drop our offer,
		       * we must send a new ANSWER */

			econn_set_state(econn, ECONN_UPDATE_RECV);
			should_reset = true;
		}
		break;

	default:
		warning("[ %s.%s ] econn(%p): recv_update: "
			"ignore received UPDATE Request "
			"in wrong state '%s'\n",
			anon_id(userid_anon, econn->userid_self),
			anon_client(clientid_anon1, econn->clientid),
			econn,
			econn_state_name(econn->state));
		return;
	}

	tmr_start(&econn->tmr_local, econn->conf.timeout_setup,
		  tmr_local_handler, econn);

	if (econn->update_reqh) {
		econn->update_reqh(econn,
				   userid_sender,
				   clientid_sender,
				   msg->u.setup.sdp_msg,
				   msg->u.setup.props,
				   should_reset,
				   econn->arg);
	}
}


static void handle_update_response(struct econn *econn,
				   const char *userid_sender,
				   const char *clientid_sender,
				   const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon1[ANON_CLIENT_LEN];
	char clientid_anon2[ANON_CLIENT_LEN];

	/* check if the Remote ClientID is correct */
	if (0 != str_casecmp(econn->clientid_remote, clientid_sender)) {
		warning("econn(%p): ignoring UPDATE-resp from wrong client. "
		        "Expected: %s got: %s\n",
			econn,
		        anon_client(clientid_anon1, econn->clientid_remote),
		        anon_client(clientid_anon2, clientid_sender));
		return;
	}

	/* todo: use a proper state machine */
	if (econn->state != ECONN_UPDATE_SENT) {
		info("econn(%p): recv_setup: ignore received UPDATE(r)"
		     " from %s|%s in wrong state '%s'\n",
		     econn,
		     anon_id(userid_anon, userid_sender),
		     anon_client(clientid_anon1, clientid_sender),
		     econn_state_name(econn->state));

		return;
	}

	tmr_cancel(&econn->tmr_local);

	econn_set_state(econn, ECONN_ANSWERED);

	if (econn->update_resph) {
		econn->update_resph(econn,
				    msg->u.setup.sdp_msg,
				    msg->u.setup.props,
				    econn->arg);
	}
}


static void recv_update(struct econn *econn,
			const char *userid_sender,
			const char *clientid_sender,
			const struct econn_message *msg)
{
	if (!econn || !msg)
		return;

	if (0 != str_casecmp(econn->sessid_remote, msg->sessid_sender)) {
		warning("econn(%p): recv_update: remote SESSIONID does"
		        " not match (%s vs %s)\n",
			econn,
		        econn->sessid_remote, msg->sessid_sender);
		return;
	}

	if (econn_message_isrequest(msg)) {

		handle_update_request(econn, userid_sender,
				      clientid_sender, msg);
	}
	else { /* Response */

		handle_update_response(econn, userid_sender,
				       clientid_sender, msg);
	}
}


static void recv_cancel(struct econn *conn, const char *clientid_sender,
			const struct econn_message *msg)
{
	char clientid_anon1[ANON_CLIENT_LEN];
	char clientid_anon2[ANON_CLIENT_LEN];

	if (0 != str_casecmp(clientid_sender, conn->clientid_remote)) {
		info("econn(%p): recv_cancel: clientid does not match"
		     " (remote=%s, sender=%s)\n",
		     conn,
		     anon_client(clientid_anon1, conn->clientid_remote),
		     anon_client(clientid_anon2, clientid_sender));
		return;
	}

	if (conn->state != ECONN_PENDING_INCOMING &&
	    conn->state != ECONN_ANSWERED &&
	    conn->state != ECONN_DATACHAN_ESTABLISHED) {
		info("econn(%p): recv_cancel: ignore received CANCEL"
		     " in state `%s'\n",
		     conn, econn_state_name(conn->state));
		return;
	}

	if (0 != str_casecmp(conn->sessid_remote, msg->sessid_sender)) {
		warning("econn(%p): recv_cancel: remote SESSIONID does"
			" not match\n", conn);
		return;
	}

	econn_set_state(conn, ECONN_TERMINATING);

	/* NOTE: must be done last */
	econn_close(conn, conn->err ? conn->err : ECANCELED,
		msg ? msg->time : ECONN_MESSAGE_TIME_UNKNOWN);
}


static void recv_hangup(struct econn *conn, const struct econn_message *msg)
{
	if (0 != str_casecmp(conn->sessid_remote, msg->sessid_sender)) {
		warning("econn(%p): recv_hangup: remote SESSIONID does"
			" not match (%s vs %s)\n",
			conn,
			conn->sessid_remote, msg->sessid_sender);
		return;
	}

	if (conn->state != ECONN_DATACHAN_ESTABLISHED &&
	    conn->state != ECONN_HANGUP_SENT) {
		warning("econn(%p): channel_recv: ignore HANGUP in state %s\n",
			conn, econn_state_name(conn->state));
		return;
	}

	econn_set_state(conn, ECONN_HANGUP_RECV);

	/* If the incoming HANGUP is a Request, we must respond
	 * with HANGUP Response.
	 */
	if (econn_message_isrequest(msg)) {
		int err = send_hangup(conn, true);
		if (err) {
			warning("econn(%p): send_hangup failed (%m)\n",
				conn, err);
		}
	}

	econn_set_state(conn, ECONN_TERMINATING);

	/* NOTE: must be done last */
	econn_close(conn, conn->err,
		    msg ? msg->time : ECONN_MESSAGE_TIME_UNKNOWN);
}


static void recv_alert(struct econn *econn,
		       const char *userid_sender,
		       const char *clientid_sender,
		       const struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (econn->alerth) {
		econn->alerth(econn, msg->u.alert.level,
			      msg->u.alert.descr, econn->arg);
	}
	else {
		warning("econn(%p): received ALERT from %s.%s (%s)\n",
			econn,
			anon_id(userid_anon, userid_sender),
			anon_client(clientid_anon, clientid_sender),
			msg->u.alert.descr);
	}
}


void econn_recv_message(struct econn *conn,
			const char *userid_sender,
			const char *clientid_sender,
			const struct econn_message *msg)
{
	if (!conn || !msg)
		return;

	assert(ECONN_MAGIC == conn->magic);

	switch (msg->msg_type) {

	case ECONN_SETUP:
		recv_setup(conn, userid_sender, clientid_sender, msg);
		break;

	case ECONN_UPDATE:
		recv_update(conn, userid_sender, clientid_sender, msg);
		break;

	case ECONN_CANCEL:
		recv_cancel(conn, clientid_sender, msg);
		break;

	case ECONN_HANGUP:
		recv_hangup(conn, msg);
		break;

	case ECONN_REJECT:
		break;

	case ECONN_DEVPAIR_PUBLISH:
		break;

	case ECONN_DEVPAIR_ACCEPT:
		break;

	case ECONN_ALERT:
		recv_alert(conn, userid_sender, clientid_sender, msg);
		break;

	default:
		warning("econn(%p): recv: message not supported (%s)\n",
			conn, econn_msg_name(msg->msg_type));
		break;
	}
}


/*
 * Allocate a new ECONN instance
 *
 * @param transp Transport object (optional)
 */
int  econn_alloc(struct econn **connp,
		 const struct econn_conf *conf,
		 const char *userid_self,
		 const char *clientid,
		 struct econn_transp *transp,
		 econn_conn_h *connh,
		 econn_answer_h *answerh,
		 econn_update_req_h *update_reqh,
		 econn_update_resp_h *update_resph,
		 econn_alert_h *alerth,
		 econn_close_h *closeh,
		 void *arg)
{
	struct econn *conn;
	int err = 0;

	if (!str_isset(userid_self) || !str_isset(clientid))
		return EINVAL;

	conn = mem_zalloc(sizeof(*conn), econn_destructor);
	if (!conn)
		return ENOMEM;

	conn->magic = ECONN_MAGIC;

	conn->conf = conf ? *conf : default_conf;

	err |= str_dup(&conn->userid_self, userid_self);
	err |= str_dup(&conn->clientid, clientid);
	if (err)
		goto out;

	conn->transp = transp;

	conn->connh        = connh;
	conn->answerh      = answerh;
	conn->update_reqh  = update_reqh;
	conn->update_resph = update_resph;
	conn->alerth       = alerth;
	conn->closeh       = closeh;
	conn->arg          = arg;

	/* Generate a new random (unique) local Session-ID */
	rand_str(conn->sessid_local, 5);

	conn->err = 0;
 out:
	if (err)
		mem_deref(connp);
	else if (connp)
		*connp = conn;

	return err;
}


static void tmr_local_handler(void *arg)
{
	struct econn *conn = arg;

	assert(ECONN_MAGIC == conn->magic);

	info("econn(%p): setup timeout (state = %s)\n",
	     conn, econn_state_name(econn_current_state(conn)));

	econn_close(conn, conn->err ? conn->err : ETIMEDOUT_ECONN,
		    ECONN_MESSAGE_TIME_UNKNOWN);
}


/*
 * Start a new outgoing call.
 *
 * 1. first check the current state
 * 2. send a new SETUP message
 * 3. check the new state
 */
int econn_start(struct econn *conn, const char *sdp,
		const struct econn_props *props)
{
	int err = 0;

	if (!conn)
		return EINVAL;

	switch (conn->state) {

	case ECONN_IDLE:
	case ECONN_PENDING_OUTGOING:
		break;

	default:
		warning("econn(%p): start: invalid state '%s'\n",
			conn, econn_state_name(conn->state));
		return EPROTO;
	}

	econn_set_state(conn, ECONN_PENDING_OUTGOING);
	conn->dir = ECONN_DIR_OUTGOING;

	/* note: handlers called syncronously */
	err = econn_send_setup(conn, false, sdp, props, false);
	if (err) {
		warning("econn(%p): connect: send_setup failed (%m)\n",
			conn, err);
		return err;
	}

	if (!conn->conf.timeout_setup) {
		warning("econn(%p): start: illegal timer value 0\n", conn);
		return EPROTO;
	}

	tmr_start(&conn->tmr_local, conn->conf.timeout_setup,
		  tmr_local_handler, conn);

	return err;
}


int econn_update_req(struct econn *conn, const char *sdp,
		     const struct econn_props *props)
{
	int err = 0;

	if (!conn)
		return EINVAL;

	switch (conn->state) {

	case ECONN_ANSWERED:
	case ECONN_DATACHAN_ESTABLISHED:
		break;

	default:
		return EPROTO;
	}

	econn_set_state(conn, ECONN_UPDATE_SENT);

	/* note: handlers called syncronously */
	err = econn_send_setup(conn, false, sdp, props, true);
	if (err) {
		warning("econn(%p): connect: send_setup failed (%m)\n",
			conn, err);
		return err;
	}

	if (!conn->conf.timeout_setup) {
		warning("econn(%p): start: illegal timer value 0\n", conn);
		return EPROTO;
	}

	tmr_start(&conn->tmr_local, conn->conf.timeout_setup,
		  tmr_local_handler, conn);

	return err;
}


int econn_update_resp(struct econn *conn, const char *sdp,
		      const struct econn_props *props)
{
	int err;

	if (!conn)
		return EINVAL;

	if (conn->state != ECONN_UPDATE_RECV) {
		warning("econn(%p): update_resp: cannot send UPDATE-resp "
			"answer in wrong state '%s'\n",
			conn, econn_state_name(conn->state));
		return EPROTO;
	}

	tmr_cancel(&conn->tmr_local);

	err = econn_send_setup(conn, true, sdp, props, true);
	if (err)
		return err;

	/* ??? */
	econn_set_state(conn, ECONN_ANSWERED);

	return 0;
}


/* Answer an incoming call */
int econn_answer(struct econn *conn, const char *sdp,
		 const struct econn_props *props)
{
	int err;

	if (!conn)
		return EINVAL;

	if (conn->state != ECONN_PENDING_INCOMING &&
	    conn->state != ECONN_CONFLICT_RESOLUTION) {
		warning("econn(%p): cannot answer in wrong state '%s'\n",
			conn, econn_state_name(conn->state));
		return EPROTO;
	}

	tmr_cancel(&conn->tmr_local);

	err = econn_send_setup(conn, true, sdp, props, false);
	if (err)
		return err;

	econn_set_state(conn, ECONN_ANSWERED);

	return 0;
}


static void tmr_term_handler(void *arg)
{
	struct econn *econn = arg;

	debug("econn(%p): timeout waiting for HANGUP(r)\n", econn);

	econn_close(econn, econn->err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void tmr_cancel_handler(void *arg)
{
	struct econn *econn = arg;

	debug("econn(%p): closing econn after sending CANCEL\n", econn);

	econn_close(econn, econn->err, ECONN_MESSAGE_TIME_UNKNOWN);
}


void econn_end(struct econn *conn)
{
	int err;

	if (!conn)
		return;

	info("econn(%p): end (state=%s)\n",
	     conn, econn_state_name(conn->state));

	switch (conn->state) {

	case ECONN_UPDATE_RECV:
	case ECONN_PENDING_INCOMING:
		/* ignore the incoming call */

		econn_set_state(conn, ECONN_TERMINATING);

		tmr_start(&conn->tmr_local, 1, tmr_cancel_handler, conn);
		break;

	case ECONN_UPDATE_SENT:
	case ECONN_PENDING_OUTGOING:
	case ECONN_ANSWERED:
	case ECONN_CONFLICT_RESOLUTION:

		err = send_cancel(conn);
		if (err) {
			warning("econn(%p): end: send_cancel failed (%m)\n",
				conn, err);
		}

		econn_set_state(conn, ECONN_TERMINATING);

		tmr_start(&conn->tmr_local, 1, tmr_cancel_handler, conn);
		break;

	case ECONN_DATACHAN_ESTABLISHED:

		err = send_hangup(conn, false);
		if (err) {
			warning("econn(%p): send_hangup failed (%m)\n",
				conn, err);
			conn->err = err;
			tmr_start(&conn->tmr_local, 1, tmr_term_handler, conn);
		}
		else {
			econn_set_state(conn, ECONN_HANGUP_SENT);
			tmr_start(&conn->tmr_local, conn->conf.timeout_term,
				  tmr_term_handler, conn);
		}
		break;

	case ECONN_TERMINATING:
		break;

	default:
		warning("econn(%p): cannot end in state '%s'\n",
			conn, econn_state_name(conn->state));

		econn_set_state(conn, ECONN_TERMINATING);

		tmr_start(&conn->tmr_local, conn->conf.timeout_term,
			  tmr_term_handler, conn);
		break;
	}
}


enum econn_state econn_current_state(const struct econn *conn)
{
	return conn ? conn->state : ECONN_IDLE;
}


enum econn_dir econn_current_dir(const struct econn *conn)
{
	return conn ? conn->dir : ECONN_DIR_UNKNOWN;
}


const char *econn_userid_remote(const struct econn *conn)
{
	return conn ? conn->userid_remote : NULL;
}

const char *econn_clientid_remote(const struct econn *conn)
{
	return conn ? conn->clientid_remote : NULL;
}


const char *econn_sessid_local(const struct econn *conn)
{
	return conn ? conn->sessid_local : NULL;
}


const char *econn_sessid_remote(const struct econn *conn)
{
	return conn ? conn->sessid_remote : NULL;
}


bool econn_can_send_propsync(const struct econn *econn)
{
	if (!econn)
		return false;

	return (econn->state == ECONN_DATACHAN_ESTABLISHED);
}


int econn_send_propsync(struct econn *econn, bool resp,
			struct econn_props *props)
{
	struct econn_message msg;
	int err;

	if (!econn || !props)
		return EINVAL;

	if (econn->state != ECONN_DATACHAN_ESTABLISHED) {
		warning("econn(%p): send_propsync: cannot send Propsync %s"
			" in wrong state `%s'\n",
			econn, resp ? "Response" : "Request",
			econn_state_name(econn->state));
		return EPROTO;
	}

	err = econn_message_init(&msg, ECONN_PROPSYNC, econn->sessid_local);
	if (err) {
		warning("econn_message_init error %m\n", err);
		return err;
	}

	msg.resp = resp;
	msg.u.propsync.props = props;

	err = econn_transp_send(econn, &msg);
	if (err) {
		warning("econn(%p): transp_send failed (%m)\n", econn, err);
		return err;
	}

	return 0;
}


int econn_debug(struct re_printf *pf, const struct econn *conn)
{
	int err = 0;
	char clientid_anon[ANON_CLIENT_LEN];

	if (!conn)
		return 0;

	err |= re_hprintf(pf, "~~~~~ econn <%p> ~~~~~\n", conn);

	err |= re_hprintf(pf, "state:            %s",
			  econn_state_name(econn_current_state(conn)));

	if (econn_current_dir(conn) != ECONN_DIR_UNKNOWN) {
		err |= re_hprintf(pf, "  (%s)",
				  econn_dir_name(econn_current_dir(conn)));
	}

	err |= re_hprintf(pf, "\n");

	err |= re_hprintf(pf, "clientid_remote:  %s\n",
			  anon_client(clientid_anon, conn->clientid_remote));
	err |= re_hprintf(pf, "session:          %s|%s\n",
			  conn->sessid_local, conn->sessid_remote);

	if (tmr_isrunning(&conn->tmr_local)) {
		err |= re_hprintf(pf, "timer_local:      %u seconds\n",
				  tmr_get_expire(&conn->tmr_local)/1000);
	}
	else {
		err |= re_hprintf(pf, "timer_local:      (not running)\n");
	}

	if (conn->setup_err) {
		err |= re_hprintf(pf, "setup_error:      \"%m\"\n",
				  conn->setup_err);
	}

	if (conn->conflict != 0) {
		err |= re_hprintf(pf, "conflict:         %s\n",
				  conn->conflict == 1 ? "Winner" : "Looser");
	}
	else {
		err |= re_hprintf(pf, "conflict:         None\n");
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/* This function should be called when the DataChannel is established */
void econn_set_datachan_established(struct econn *econn)
{
	if (!econn)
		return;

	if (econn->state == ECONN_ANSWERED) {

		econn_set_state(econn, ECONN_DATACHAN_ESTABLISHED);
	}
	else {
		warning("econn(%p): set_datachan_established: "
			"illegal state %s\n",
			econn, econn_state_name(econn->state));
	}
}


void econn_set_error(struct econn *econn, int err)
{
	if (!econn)
		return;

	econn->err = err;
}


int econn_send_alert(struct econn *conn, uint32_t level, const char *descr)
{
	struct econn_message msg;
	int err = 0;

	if (!conn)
		return EINVAL;

	err = econn_message_init(&msg, ECONN_ALERT, conn->sessid_local);
	if (err)
		return err;

	msg.transient = true;
	msg.u.alert.level = level;
	err = str_dup(&msg.u.alert.descr, descr);
	if (err)
		goto out;

	err = econn_transp_send(conn, &msg);
	if (err)
		goto out;

 out:
	econn_message_reset(&msg);

	return err;
}
