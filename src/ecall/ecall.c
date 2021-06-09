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
#include "avs_log.h"
#include "avs_base.h"
#include "avs_zapi.h"
#include "avs_icall.h"
#include "avs_keystore.h"
#include "avs_iflow.h"
#include "avs_peerflow.h"
#include "avs_uuid.h"
#include "avs_turn.h"
#include "avs_cert.h"
#include "avs_msystem.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_ecall.h"
#include "avs_conf_pos.h"
#include "avs_jzon.h"
#include "avs_ztime.h"
#include "avs_version.h"
#include "avs_wcall.h"
#include "avs_string.h"
#include "avs_audio_level.h"
#include "ecall.h"


#define SDP_MAX_LEN 16384
#define ECALL_MAGIC 0xeca1100f

#define TIMEOUT_DC_CLOSE     10000
#define TIMEOUT_MEDIA_START  10000
#define TIMEOUT_CONNECTION    5000
#define TIMEOUT_AUDIO_LEVEL   3000

//#define ECALL_CBR_ALWAYS_ON 1


static struct list g_ecalls = LIST_INIT;

static const struct ecall_conf default_conf = {
	.econf = {
		.timeout_setup = 60000,
		.timeout_term  =  5000,
	},
};


static int alloc_flow(struct ecall *ecall, enum async_sdp role,
		      enum icall_call_type call_type,
		      bool audio_cbr);
static int generate_answer(struct ecall *ecall, struct econn *econn);
static int handle_propsync(struct ecall *ecall, struct econn_message *msg);
static void propsync_handler(struct ecall *ecall);
static void econn_ping_handler(struct econn *econn,
			       bool response,
			       void *arg);
static void connection_timeout_handler(void *arg);


static void set_offer_sdp(struct ecall *ecall, const char *sdp)
{
	ecall->sdp.offer = mem_deref(ecall->sdp.offer);
	str_dup(&ecall->sdp.offer, sdp);	
}

static const char *async_sdp_name(enum async_sdp sdp)
{
	switch (sdp) {

	case ASYNC_NONE:     return "None";
	case ASYNC_OFFER:    return "Offer";
	case ASYNC_ANSWER:   return "Answer";
	case ASYNC_COMPLETE: return "Complete";
	default: return "???";
	}
}

/* NOTE: Should only be triggered by async events! */
void ecall_close(struct ecall *ecall, int err, uint32_t msg_time)
{
	icall_close_h *closeh;
	struct iflow *flow;

	if (!ecall)
		return;
	
	ecall->icall.qualityh = NULL;
	tmr_cancel(&ecall->quality.tmr);
	tmr_cancel(&ecall->dc_tmr);
	tmr_cancel(&ecall->media_start_tmr);
	tmr_cancel(&ecall->connection_tmr);

	closeh = ecall->icall.closeh;

	if (err) {
		info("ecall(%p): closed (%m)\n", ecall, err);
	}
	else {
		info("ecall(%p): closed (normal)\n", ecall);
	}

	char *json_str = NULL;

	/* Keep flow reference, but indicate that it's gone */
	flow = ecall->flow;
	ecall->flow = NULL;
	ecall->conf_part = mem_deref(ecall->conf_part);
	IFLOW_CALL(flow, close);

	flow = ecall->oldflow;
	ecall->oldflow = NULL;
	IFLOW_CALL(flow, close);

	/* NOTE: calling the callback handlers MUST be done last,
	 *       to make sure that all states are correct.
	 */
	if (ecall->video.recv_state != ICALL_VIDEO_STATE_STOPPED) {
		ICALL_CALL_CB(ecall->icall, vstate_changedh,
			&ecall->icall, ecall->userid_peer, ecall->clientid_peer,
			ICALL_VIDEO_STATE_STOPPED, ecall->icall.arg);
		ecall->video.recv_state = ICALL_VIDEO_STATE_STOPPED;
	}

	if (closeh) {
		ecall->icall.closeh = NULL;
		closeh(&ecall->icall, err, json_str, msg_time,
			ecall->userid_peer, ecall->clientid_peer, ecall->icall.arg);
	}

	/* NOTE here the app should have destroyed the econn */
}


static void econn_conn_handler(struct econn *econn,
			       uint32_t msg_time,
			       const char *userid_sender,
			       const char *clientid_sender,
			       uint32_t age,
			       const char *sdp,
			       struct econn_props *props,
			       bool reset,
			       void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;
	const char *vr;
	bool video_active = false;
	char userid_anon1[ANON_ID_LEN];
	char userid_anon2[ANON_ID_LEN];
	//char clientid_anon[ANON_CLIENT_LEN];

	assert(ECALL_MAGIC == ecall->magic);

	/* check if the Peer UserID is set */
	if (str_isset(ecall->userid_peer)) {

		if (0 != str_casecmp(ecall->userid_peer,
				     userid_sender)) {

			warning("ecall: conn_handler:"
			     " peer UserID already set to `%s'"
			     " - dropping message with `%s'\n",
			     anon_id(userid_anon1, ecall->userid_peer),
			     anon_id(userid_anon2, userid_sender));
		}
	}
	else {
		err = str_dup(&ecall->userid_peer, userid_sender);
		if (err)
			goto error;
	}

	if (reset || !ecall->flow) {
		err = alloc_flow(ecall, ASYNC_ANSWER, ecall->call_type, false);
		if (err)
			goto error;
	}

	IFLOW_CALL(ecall->flow, set_remote_userclientid,
		userid_sender, clientid_sender);

	set_offer_sdp(ecall, sdp);
	
#if 0
	err = mediaflow_handle_offer(ecall->flow, sdp);
	if (err) {
		warning("ecall[%s]: handle_offer error (%m)\n",
			anon_id(userid_anon1, ecall->userid_self), err);
		goto error;
	}
#endif

/*
	if (!IFLOW_CALLE(ecall->flow, has_data)) {
		warning("ecall: conn_handler: remote peer does not"
			" support datachannels (%s|%s)\n",
			anon_id(userid_anon1, userid_sender),
			anon_client(clientid_anon, clientid_sender));
		return;
	}
*/
	/* check for remote properties */
	if (reset) {
		ecall->props_remote = mem_deref(ecall->props_remote);
	}

	if (ecall->props_remote) {
		warning("ecall: conn_handler: remote props already set");
		err = EPROTO;
		goto error;
	}
	ecall->props_remote = mem_ref(props);

	vr = ecall_props_get_remote(ecall, "videosend");
	if (vr) {
		if (strcmp(vr, "true") == 0) {
			video_active = true;
		}
	}

	info("ecall(%p): conn_handler: message age is %u seconds\n",
	     ecall, age);

	if (reset) {
		ecall->sdp.async = ASYNC_NONE;
		ecall->update = false;
		err = ecall_answer(ecall, ecall->call_type, ecall->audio_cbr);
		if (err)
			goto error;
	}
	else {
		ICALL_CALL_CB(ecall->icall, starth,
			&ecall->icall, msg_time, userid_sender, clientid_sender,
			video_active, true, ICALL_CONV_TYPE_ONEONONE,
			ecall->icall.arg);

		ecall->ts_started = tmr_jiffies();
		ecall->call_setup_time = -1;
	}

	return;

 error:
	ecall_close(ecall, err, msg_time);
}

static void gather_all(struct ecall *ecall, bool offer)
{
	info("ecall(%p): gather_all: ifs:%s turn:%s role=%s\n",
	     ecall,
	     ecall->ifs_added ? "no" : "yes",
	     ecall->turn_added ? "yes" : "no",
	     offer ? "offer" : "answer");

	IFLOW_CALL(ecall->flow, gather_all_turn, offer);
}


static int generate_or_gather_answer(struct ecall *ecall, struct econn *econn)
{
	int err;

	if (ecall->sdp.offer) {
		err = IFLOW_CALLE(ecall->flow, handle_offer,
			ecall->sdp.offer);
		if (err) {
			warning("ecall(%p): handle_offer error (%m)\n",
				ecall, err);
			return EBADMSG;
		}
		ecall->sdp.offer = mem_deref(ecall->sdp.offer);
	}

	if (IFLOW_CALLE(ecall->flow, is_gathered)) {
		return generate_answer(ecall, econn);
	}
	else {
		if (ecall->sdp.async == ASYNC_NONE) {
			ecall->sdp.async = ASYNC_ANSWER;
			gather_all(ecall, false);
			ecall->econn_pending = econn;
		}
	}

	return 0;
}


static void econn_update_req_handler(struct econn *econn,
				     const char *userid_sender,
				     const char *clientid_sender,
				     const char *sdp,
				     struct econn_props *props,
				     bool should_reset,
				     void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;
	bool strm_chg;
	bool muted;
	//char userid_anon[ANON_ID_LEN];
	//char clientid_anon[ANON_CLIENT_LEN];

	assert(ECALL_MAGIC == ecall->magic);

	ecall->update = true;

	strm_chg = strstr(sdp, "x-streamchange") != NULL;

	muted = msystem_get_muted();
	
	if (ecall->flow && strm_chg) {
		info("ecall(%p): update: x-streamchange\n", ecall);
		IFLOW_CALL(ecall->flow, stop_media);
	}
	else {
		err = alloc_flow(ecall, ASYNC_ANSWER, ecall->call_type, ecall->audio_cbr);
		if (err)
			goto error;

		//if (ecall->conf_part)
		//	ecall->conf_part->data = ecall->flow;

		IFLOW_CALL(ecall->flow, set_remote_userclientid,
			userid_sender,
			econn_clientid_remote(econn));
	}

	msystem_set_muted(muted);
/*
	if (!IFLOW_CALLE(ecall->flow, has_data)) {
		warning("ecall: update_req_handler: remote peer does not"
			" support datachannels (%s|%s)\n",
			anon_id(userid_anon, userid_sender),
			anon_client(clientid_anon, clientid_sender));
		return;
	}
*/
	ecall->props_remote = mem_deref(ecall->props_remote);
	ecall->props_remote = mem_ref(props);

	propsync_handler(ecall);
	
	ecall->sdp.async = ASYNC_NONE;
	set_offer_sdp(ecall, sdp);
	err = generate_or_gather_answer(ecall, econn);
	if (err) {
		warning("ecall(%p): generate_or_gather_answer failed (%m)\n",
			ecall, err);
		goto error;
	}

	return;

 error:
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void econn_answer_handler(struct econn *conn, bool reset,
				 const char *sdp, struct econn_props *props,
				 void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): [ %s.%s ] ecall: answered (reset=%d, sdp=%p)\n",
	     ecall, anon_id(userid_anon, ecall->userid_self),
	     anon_client(clientid_anon, ecall->clientid_self), reset, sdp);

	ecall->audio_setup_time = -1;
	ecall->call_estab_time = -1;
	ecall->ts_answered = tmr_jiffies();

	if (reset) {
		// Reset state replaced with full re-reation of mf
		
		bool muted = msystem_get_muted();
		err = alloc_flow(ecall, ASYNC_ANSWER, ecall->call_type, false);
		msystem_set_muted(muted);
		if (err) {
			warning("ecall: re-start: alloc_flow failed: %m\n", err);
			goto error;
		}
		
		IFLOW_CALL(ecall->flow, set_remote_userclientid,
			econn_userid_remote(conn),
			econn_clientid_remote(conn));

		ecall->sdp.async = ASYNC_NONE;
		set_offer_sdp(ecall, sdp);
		err = generate_or_gather_answer(ecall, conn);
		if (err) {
			warning("ecall: generate_answer\n");
			goto error;
		}

		ecall->answered = true;

		return;
	}

	if (ecall->answered) {
		warning("ecall: answer_handler: already connected\n");
		return;
	}

	IFLOW_CALL(ecall->flow, set_remote_userclientid,
		econn_userid_remote(conn),
		econn_clientid_remote(conn));

	IFLOW_CALL(ecall->flow, handle_answer, sdp);
	if (err) {
		warning("ecall: answer_handler: handle_answer failed"
			" (%m)\n", err);
		goto error;
	}

	ecall->props_remote = mem_ref(props);

	ecall->answered = true;

	ICALL_CALL_CB(ecall->icall, answerh,
		&ecall->icall, ecall->icall.arg);

	if (!ecall->established) {
		tmr_start(&ecall->connection_tmr, TIMEOUT_CONNECTION,
			  connection_timeout_handler, ecall);
	}
	return;

 error:
	/* if error, close the call */
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void econn_update_resp_handler(struct econn *econn,
				      const char *sdp,
				      struct econn_props *props,
				      void *arg)
{
	struct ecall *ecall = arg;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	assert(ECALL_MAGIC == ecall->magic);

	if (!ecall->update) {
		warning("ecall(%p): received UPDATE-resp with no update\n",
			ecall);
		return;
	}

	info("ecall(%p): [%s.%s] UPDATE-resp (sdp=%p)\n",
	     anon_id(userid_anon, ecall->userid_self),
	     anon_client(clientid_anon, ecall->clientid_self), sdp);

	err = IFLOW_CALLE(ecall->flow, handle_answer, sdp);
	if (err) {
		warning("ecall: answer_handler: handle_answer failed"
			" (%m)\n", err);
		goto error;
	}

	ecall->props_remote = mem_deref(ecall->props_remote);
	ecall->props_remote = mem_ref(props);

	if (!ecall->established) {
		tmr_start(&ecall->connection_tmr, TIMEOUT_CONNECTION,
			  connection_timeout_handler, ecall);
	}
	return;

 error:
	/* if error, close the call */
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void econn_alert_handler(struct econn *econn, uint32_t level,
				const char *descr, void *arg)
{
}


static void econn_confpart_handler(struct econn *econn,
				   const struct econn_message *msg,
				   void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (ecall->confparth) {
		info("ecall(%p): confpart: parts: %u should_start %s\n",
		     ecall, list_count(&msg->u.confpart.partl),
		     msg->u.confpart.should_start ? "YES" : "NO");
		ecall->confparth(ecall, msg, ecall->icall.arg);
	}
}


int ecall_set_confpart_handler(struct ecall *ecall,
			       ecall_confpart_h confparth)
{
	if (!ecall) {
		return EINVAL;
	}

	ecall->confparth = confparth;
	return 0;
}


int ecall_set_propsync_handler(struct ecall *ecall,
			       ecall_propsync_h propsynch)
{
	if (!ecall) {
		return EINVAL;
	}

	ecall->propsynch = propsynch;
	return 0;
}


static void econn_close_handler(struct econn *econn, int err,
				uint32_t msg_time, void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (err)
		info("ecall(%p): econn closed (%m)\n", ecall, err);
	else
		info("ecall(%p): econn closed (normal)\n", ecall);

	ecall_media_stop(ecall);
	ecall_close(ecall, err, msg_time);
}


static void ecall_destructor(void *data)
{
	struct ecall *ecall = data;
	struct iflow *flow = NULL;

#if 1
	info("--------------------------------------\n");
	info("%H\n", ecall_debug, ecall);
	info("--------------------------------------\n");
#endif

	list_unlink(&ecall->le);
	list_unlink(&ecall->ecall_le);

	tmr_cancel(&ecall->dc_tmr);
	tmr_cancel(&ecall->media_start_tmr);
	tmr_cancel(&ecall->connection_tmr);

	tmr_cancel(&ecall->quality.tmr);
	tmr_cancel(&ecall->audio.level.tmr);

	if (ecall->conf_part) {
		//ecall->conf_part->data = NULL;
		mem_deref(ecall->conf_part);
	}
	flow = ecall->flow;
	ecall->flow = NULL;
	IFLOW_CALL(flow, close);
	flow = ecall->oldflow;
	ecall->oldflow = NULL;
	IFLOW_CALL(flow, close);

	mem_deref(ecall->userid_self);
	mem_deref(ecall->userid_peer);
	mem_deref(ecall->clientid_self);
	mem_deref(ecall->clientid_peer);
	mem_deref(ecall->clientid_real);
	mem_deref(ecall->sessid);
	mem_deref(ecall->convid);
	mem_deref(ecall->msys);
	mem_deref(ecall->usrd);

	mem_deref(ecall->props_remote);
	mem_deref(ecall->props_local);
	mem_deref(ecall->keystore);

	mem_deref(ecall->econn);

	mem_deref(ecall->sdp.offer);
	mem_deref(ecall->media_laddr);

	//list_flush(&ecall->audio.level.l);
	
	list_flush(&ecall->tracel);

	/* last thing to do */
	ecall->magic = 0;
}


static int send_handler(struct econn *conn,
			struct econn_message *msg, void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;
	int try_otr = 0, try_dce = 0;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char dest_userid_anon[ANON_ID_LEN];
	char dest_clientid_anon[ANON_CLIENT_LEN];
	int silent = 0;

	assert(ECALL_MAGIC == ecall->magic);

	if (ecall->userid_self) {
		str_ncpy(msg->src_userid,
			 ecall->userid_self,
			 sizeof(msg->src_userid));
	}

	if (ecall->clientid_self) {
		str_ncpy(msg->src_clientid,
			 ecall->clientid_self,
			 sizeof(msg->src_clientid));
	}
	if (ecall->userid_peer) {
		str_ncpy(msg->dest_userid,
			 ecall->userid_peer,
			 sizeof(msg->dest_userid));
	}

	if (ecall->clientid_peer) {
		str_ncpy(msg->dest_clientid,
			 ecall->clientid_peer,
			 sizeof(msg->dest_clientid));
	}

	// todo: resolve transport-type instead ?

	switch (msg->msg_type) {

	case ECONN_SETUP:
	case ECONN_UPDATE:
	case ECONN_CANCEL:
	case ECONN_ALERT:
		try_dce = 0;
		try_otr = 1;
		break;

	case ECONN_PROPSYNC:
		try_dce = 1;
		try_otr = 1;
		break;

	case ECONN_HANGUP:
		try_dce = 1;
		try_otr = 0;
		break;

	case ECONN_PING:
		try_dce = 1;
		try_otr = 0;
		silent = 1;
		break;

	default:
		warning("ecall: send_handler: message not supported (%s)\n",
			econn_msg_name(msg->msg_type));
		err = EPROTO;
		goto out;
		break;
	}

	//if (try_dce && IFLOW_CALLE(ecall->flow, has_data)) {
	if (try_dce) {
		if (!silent) {
			info("ecall(%p): dce_message_send: convid=%s from=%s.%s to=%s.%s "
			     "msg=%H\n",
			     ecall, anon_id(convid_anon, ecall->convid),
			     anon_id(userid_anon, ecall->userid_self),
			     anon_client(clientid_anon, ecall->clientid_self),
			     anon_id(dest_userid_anon, ecall->userid_peer),
			     anon_client(dest_clientid_anon, ecall->clientid_peer),
			     econn_message_print, msg);
		}
		ecall_dce_sendmsg(ecall, msg);
	}
	else if (try_otr) {
		ecall_trace(ecall, msg, true, ECONN_TRANSP_BACKEND,
			    "SE %H\n", econn_message_brief, msg);
		err = ICALL_CALL_CBE(ecall->icall, sendh,
			&ecall->icall, ecall->userid_self, msg, NULL, ecall->icall.arg);
	}

out:
	return err;
}


static int _icall_add_turnserver(struct icall *icall, struct zapi_ice_server *srv)
{
	return ecall_add_turnserver((struct ecall*)icall, srv);
}


static int _icall_start(struct icall *icall, enum icall_call_type call_type,
			bool audio_cbr)
{
	return ecall_start((struct ecall*)icall, call_type,
			   audio_cbr);
}


static int _icall_answer(struct icall *icall, enum icall_call_type call_type,
			 bool audio_cbr)
{
	return ecall_answer((struct ecall*)icall, call_type,
			    audio_cbr);
}


static void _icall_reject(struct icall *icall)
{
	ecall_reject((struct ecall*)icall);
}


static void _icall_end(struct icall *icall)
{
	return ecall_end((struct ecall*)icall);
}


static int _icall_media_start(struct icall *icall)
{
	return ecall_media_start((struct ecall*)icall);
}


static void _icall_media_stop(struct icall *icall)
{
	ecall_media_stop((struct ecall*)icall);
}

static int _icall_set_media_laddr(struct icall *icall, struct sa *laddr)
{
	return ecall_set_media_laddr((struct ecall *)icall, laddr);
}


static int _icall_set_video_send_state(struct icall *icall, enum icall_vstate vstate)
{
	return ecall_set_video_send_state((struct ecall*)icall, vstate);
}


static void _icall_set_clients(struct icall* icall,
			       struct list *clientl)
{
	ecall_set_clients((struct ecall*)icall, clientl);
}


static void members_destructor(void *arg)
{
	struct wcall_members *mm = arg;
	size_t i;

	for (i = 0; i < mm->membc; ++i) {
		mem_deref(mm->membv[i].userid);
		mem_deref(mm->membv[i].clientid);
	}

	mem_deref(mm->membv);
}

static int _icall_get_members(struct icall *icall, struct wcall_members **mmp)
{
	struct ecall *ecall = (struct ecall *) icall;
	struct wcall_members *mm = NULL;
	struct wcall_member *memb = NULL;
	int n = 1;
	int err = 0;

	if (!ecall)
		return EINVAL;

	if (ecall->userid_peer && ecall->clientid_peer)
		n++;
	
	mm = mem_zalloc(sizeof(*mm), members_destructor);
	if (!mm) {
		err = ENOMEM;
		goto out;
	}

	mm->membv = mem_zalloc(n * sizeof(*memb), NULL);
	if (!mm->membv) {
		err = ENOMEM;
		goto out;
	}
	mm->membc = n;	

	memb = &mm->membv[0];
	str_dup(&memb->userid, ecall->userid_self);
	str_dup(&memb->clientid, ecall->clientid_self);
	memb->audio_state = ecall->audio.estab ?
		ICALL_AUDIO_STATE_ESTABLISHED : ICALL_AUDIO_STATE_CONNECTING;
	memb->video_recv = (int)ecall->vstate;
	memb->muted = msystem_get_muted();
	
	if (mm->membc > 1) {
		memb = &mm->membv[1];

		str_dup(&memb->userid, ecall->userid_peer);
		str_dup(&memb->clientid, ecall->clientid_peer);
		memb->audio_state = ecall->audio.estab ?
			ICALL_AUDIO_STATE_ESTABLISHED : ICALL_AUDIO_STATE_CONNECTING;
		memb->video_recv = (int)ecall->video.recv_state;
		memb->muted = ecall->audio.remote_muted_state;
	}

 out:
	if (err) {
		mem_deref(memb);
		mem_deref(mm);
	}
	else {
		if (mmp)
			*mmp = mm;
	}

	return err;
}


static int _icall_msg_recv(struct icall *icall,
			   uint32_t curr_time, /* in seconds */
			   uint32_t msg_time, /* in seconds */
			   const char *userid_sender,
			   const char *clientid_sender,
			   struct econn_message *msg)
{
	return ecall_msg_recv((struct ecall*)icall, curr_time, msg_time,
		userid_sender, clientid_sender, msg);
}


int ecall_dce_send(struct ecall *ecall, struct mbuf *mb)
{
	int err;
	
	if (!ecall)
		return EINVAL;

	err = IFLOW_CALLE(ecall->flow, dce_send,
		mbuf_buf(mb), mbuf_get_left(mb));

	return err;
}

int ecall_dce_sendmsg(struct ecall *ecall, struct econn_message *msg)
{
	char *str = NULL;
	int err;

	err = econn_message_encode(&str, msg);
	if (err) {
		warning("ecall: dce_sendmsg: econn_message_encode"
			" failed (%m)\n", err);
		goto out;
	}

	if (msg->msg_type != ECONN_PING) {
		ecall_trace(ecall, msg, true, ECONN_TRANSP_DIRECT,
			    "DataChan %H\n",
			    econn_message_brief, msg);
	}

	err = IFLOW_CALLE(ecall->flow, dce_send,
			  (const uint8_t *)str, str_len(str));
 out:
	mem_deref(str);

	return err;
}


static int _icall_dce_send(struct icall *icall, struct mbuf *mb)
{
	return ecall_dce_send((struct ecall *)icall, mb);
}

static int _icall_set_quality_interval(struct icall *icall,
				       uint64_t interval)
{
	return ecall_set_quality_interval((struct ecall*)icall, interval);
}

static int _icall_update_mute_state(const struct icall* icall)
{
	return ecall_update_mute_state((struct ecall*)icall);
}

static int _icall_debug(struct re_printf *pf, const struct icall *icall)
{
	return ecall_debug(pf, (const struct ecall*)icall);
}

static int _icall_stats(struct re_printf *pf, const struct icall *icall)
{
	return ecall_stats(pf, (const struct ecall*)icall);
}


int ecall_alloc(struct ecall **ecallp, struct list *ecalls,
		enum icall_conv_type conv_type,
		const struct ecall_conf *conf,
		struct msystem *msys,
		const char *convid,
		const char *userid_self,
		const char *clientid)
{
	struct ecall *ecall;
	bool muted;
	int err = 0;

	if (!msys || !str_isset(convid))
		return EINVAL;

	ecall = mem_zalloc(sizeof(*ecall), ecall_destructor);
	if (!ecall)
		return ENOMEM;

	ecall->magic = ECALL_MAGIC;
	ecall->conf = conf ? *conf : default_conf;
	ecall->conv_type = conv_type;
	switch(conv_type) {
	case ICALL_CONV_TYPE_CONFERENCE:
	case ICALL_CONV_TYPE_GROUP:
		ecall->max_retries = 2;
		break;
	case ICALL_CONV_TYPE_ONEONONE:
	default:
		ecall->max_retries = 0;
		break;
	}
	ecall->num_retries = 0;

	/* Add some properties */
	err = econn_props_alloc(&ecall->props_local, NULL);
	if (err)
		goto out;

	err = econn_props_add(ecall->props_local, "videosend", "false");
	if (err)
		goto out;

	err = econn_props_add(ecall->props_local, "screensend", "false");
	if (err)
		goto out;

	err = econn_props_add(ecall->props_local, "audiocbr", "false");
	if (err)
		goto out;

	muted = msystem_get_muted();
	err = econn_props_add(ecall->props_local, "muted", muted ? "true" : "false");
	if (err)
		goto out;

	err |= str_dup(&ecall->convid, convid);
	err |= str_dup(&ecall->userid_self, userid_self);
	err |= str_dup(&ecall->clientid_self, clientid);
	if (err)
		goto out;

	ecall->msys = mem_ref(msys);

	ecall->transp.sendh = send_handler;
	ecall->transp.arg = ecall;

	icall_set_functions(&ecall->icall,
			    _icall_add_turnserver,
			    NULL, // _icall_add_sft
			    _icall_start,
			    _icall_answer,
			    _icall_end,
			    _icall_reject,
			    _icall_media_start,
			    _icall_media_stop,
			    _icall_set_media_laddr,
			    _icall_set_video_send_state,
			    _icall_msg_recv,
			    NULL, // _icall_sft_msg_recv
			    _icall_get_members,
			    _icall_set_quality_interval,
			    _icall_dce_send,
			    _icall_set_clients,
			    _icall_update_mute_state,
			    _icall_debug,
			    _icall_stats);

	list_append(ecalls, &ecall->le, ecall);
	list_append(&g_ecalls, &ecall->ecall_le, ecall);

	ecall->ts_start = tmr_jiffies();

	info("ecall(%p): allocated\n", ecall);
 out:
	if (err)
		mem_deref(ecall);
	else if (ecallp)
		*ecallp = ecall;

	return err;
}


struct icall *ecall_get_icall(struct ecall *ecall)
{
	return &ecall->icall;
}


int ecall_add_turnserver(struct ecall *ecall,
			 struct zapi_ice_server *srv)
{
	int err = 0;

	if (!ecall || !srv)
		return EINVAL;

	info("ecall(%p): add turnserver: %s\n", ecall, srv->url);

	if (ecall->turnc >= ARRAY_SIZE(ecall->turnv)) {
		warning("ecall: maximum %zu turn servers\n",
			ARRAY_SIZE(ecall->turnv));
		return 0;
	}

	ecall->turnv[ecall->turnc] = *srv;
	++ecall->turnc;

	ecall->turn_added = true;
	
	return err;
}


static int offer_and_connect(struct ecall *ecall)
{
	char *sdp = NULL;
	size_t sdp_len = SDP_MAX_LEN;
	int err = 0;

	sdp = mem_zalloc(sdp_len, NULL);
	if (!sdp)
		return ENOMEM;

	err = IFLOW_CALLE(ecall->flow, generate_offer, sdp, sdp_len);
	if (err) {
		warning("ecall(%p): offer_and_connect:"
			" mf=%p generate_offer failed (%m)\n",
			ecall, ecall->flow, err);
		err = EPROTO;
		goto out;
	}

	if (ecall->update) {
		err = econn_update_req(ecall->econn, sdp, ecall->props_local);
		if (err) {
			warning("ecall: offer_and_connect: "
				"econn_restart failed (%m)\n",
				err);
			goto out;
		}
	}
	else {
		err = econn_start(ecall->econn, sdp, ecall->props_local);
		if (err) {
			warning("ecall: offer_and_connect: "
				"econn_start failed (%m)\n",
				err);
			goto out;
		}
	}

 out:
	mem_deref(sdp);
	return err;
}


static int generate_offer(struct ecall *ecall)
{
	int err = 0;

	if (!ecall)
		return EINVAL;

	info("ecall(%p): generate_offer\n", ecall);

	if (IFLOW_CALLE(ecall->flow, is_gathered)) {

		err = offer_and_connect(ecall);
		if (err)
			return err;
	}
	else {
		info("ecall(%p): generate_offer: mf=%p: not gathered "
		     ".. wait ..\n", ecall, ecall->flow);

		if (ecall->sdp.async != ASYNC_NONE) {
			warning("ecall: offer: invalid async sdp (%s)\n",
				async_sdp_name(ecall->sdp.async));
			return EPROTO;
		}

		ecall->sdp.async = ASYNC_OFFER;
	}

	return err;
}


static int generate_answer(struct ecall *ecall, struct econn *econn)
{
	size_t sdp_len = SDP_MAX_LEN;
	char *sdp = NULL;
	int err;

	if (!econn) {
		warning("ecall: generate_answer: no pending econn\n");
		err = EPROTO;
		goto out;
	}

	sdp = mem_zalloc(sdp_len, NULL);
	if (!sdp)
		return ENOMEM;

	err = IFLOW_CALLE(ecall->flow, generate_answer, sdp, sdp_len);
	if (err) {
		warning("ecall: generate answer failed (%m)\n", err);
		goto out;
	}

	if (ecall->update) {
		ecall->num_retries++;
		err = econn_update_resp(econn, sdp, ecall->props_local);
	}
	else {
		err = econn_answer(econn, sdp, ecall->props_local);
	}
	if (err)
		goto out;

	if (!ecall->established) {
		tmr_start(&ecall->connection_tmr, TIMEOUT_CONNECTION,
			  connection_timeout_handler, ecall);
	}
 out:
	mem_deref(sdp);
	return err;
}


static void connection_timeout_handler(void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	warning("ecall(%p): connection timeout after %d milliseconds\n",
		ecall, TIMEOUT_CONNECTION);

	if (ecall->conv_type == ICALL_CONV_TYPE_GROUP) {
		ecall_restart(ecall, ecall->call_type);
	}
	/* If we don't have a media_estabh the app will never get
	 * notified about the closed call, so trigger a close for this
	 * specific case.
	 */
	else if (!ecall->icall.media_estabh) {
		ecall_close(ecall, ETIMEDOUT, ECONN_MESSAGE_TIME_UNKNOWN);
	}
}


static void media_start_timeout_handler(void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	warning("ecall(%p): media_start timeout after %d milliseconds\n",
		ecall, TIMEOUT_MEDIA_START);

	if (ecall->econn) {
		econn_set_error(ecall->econn, EIO);

		ecall_end(ecall);
	}
	else {
		/* notify upwards */
		ecall_close(ecall, EIO, ECONN_MESSAGE_TIME_UNKNOWN);
	}
}


static void mf_estab_handler(struct iflow *iflow,
			     const char *crypto,
			     const char *codec,
			     void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}

	info("ecall(%p): flow established (crypto=%s)\n",
	     ecall, crypto);

	if (ecall->call_estab_time < 0 && ecall->ts_answered) {
		ecall->call_estab_time = tmr_jiffies() - ecall->ts_answered;
	}

	if (!ecall->audio.estab)
		ecall->audio.estab = true;

	ecall->established = true;
	tmr_cancel(&ecall->connection_tmr);
	if (ecall->icall.media_estabh) {

		/* Start a timer to check that we do start Audio Later */
		tmr_start(&ecall->media_start_tmr, TIMEOUT_MEDIA_START,
			  media_start_timeout_handler, ecall);

		ICALL_CALL_CB(ecall->icall, media_estabh, &ecall->icall, ecall->userid_peer,
			      ecall->clientid_peer, ecall->update, ecall->icall.arg);
	}
	else {
		int err = ecall_media_start(ecall);
		if (err) {
			ecall_end(ecall);
		}
	}	
}


static void mf_restart_handler(struct iflow *iflow,
			       bool force_cbr,
			       void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}
	if (ECONN_ANSWERED == econn_current_state(ecall->econn) && force_cbr) {
		info("ecall(%p): mf_restart_handler: triggering restart due to CBR request\n",
			ecall);
		ecall->audio_cbr = true;
		ecall->delayed_restart = true;
	}
	if (ECONN_DATACHAN_ESTABLISHED == econn_current_state(ecall->econn)) {
		info("ecall(%p): mf_restart_handler: triggering restart due to network drop\n",
			ecall);
		ecall_restart(ecall, ecall->call_type);
	}
}


static void mf_close_handler(struct iflow *iflow, int err, void *arg)
{
	struct ecall *ecall = arg;
	char userid_anon[ANON_ID_LEN];

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): flow(%p) closed user=%s err=%m\n", ecall, iflow,
		anon_id(userid_anon, ecall->userid_self), err);

	if (iflow == ecall->oldflow) {
		struct iflow *oldflow = ecall->oldflow;

		info("ecall(%p): %s oldflow: %p closed\n", ecall, __FUNCTION__, oldflow);
		ecall->oldflow = NULL;
		IFLOW_CALL(oldflow, close);
		return;
	}
	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}

	enum econn_state state = econn_current_state(ecall->econn);
	if (ECONN_ANSWERED == state && ETIMEDOUT == err &&
		ecall->num_retries < ecall->max_retries) {
		ecall_restart(ecall, ecall->call_type);
	}
	else if (ecall->econn) {
		econn_set_error(ecall->econn, err);

		ecall_end(ecall);
	}
	else {
		/* notify upwards */
		ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
	}
}


static void mf_stopped_handler(struct iflow *iflow, void *arg)
{
	struct ecall *ecall = arg;

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}
	ICALL_CALL_CB(ecall->icall, media_stoppedh,
		&ecall->icall, ecall->icall.arg);
}


static void mf_gather_handler(struct iflow *iflow, void *arg)
{
	struct ecall *ecall = arg;
	int err;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}

	info("ecall(%p): mf_gather_handler complete "
	     "(async=%s)\n",
	     ecall,
	     async_sdp_name(ecall->sdp.async));

	switch (econn_current_state(ecall->econn)) {
		case ECONN_TERMINATING:
		case ECONN_HANGUP_SENT:
		case ECONN_HANGUP_RECV:
			return;

		default:
			break;
	}

	switch (ecall->sdp.async) {

	case ASYNC_NONE:
		break;

	case ASYNC_OFFER:
		err = offer_and_connect(ecall);
		if (err) {
			warning("ecall(%p): gather_handler: generate_offer"
				" failed (%m)\n", ecall, err);
			goto error;
		}

		ecall->sdp.async = ASYNC_COMPLETE;
		break;

	case ASYNC_ANSWER:
		err = generate_answer(ecall, ecall->econn_pending);
		if (err)
			goto error;

		ecall->sdp.async = ASYNC_COMPLETE;
		break;

	case ASYNC_COMPLETE:
		break;

	default:
		break;
	}

	return;

 error:
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}

#if 0
static bool audio_level_update(struct ecall *ecall,
			       struct list *levell)
{
	bool upd = false;
	struct le *le;
	struct le *ple;

	ple = ecall->audio.level.l.head;
	if (!ple) {
		if (!levell->head) {
			return false;
		}
		else {
			ecall->audio.level.l = *levell;
			return true;
		}
	}

	for(le = levell->head; le && ple && !upd; le = le->next) {
		struct audio_level *aulevel = le->data;
		struct audio_level *plevel = ple->data;

		upd = !audio_level_eq(aulevel, plevel);
		ple = ple->next;
	}
	if (!(le == NULL && ple == NULL))
		upd = true;

	if (upd) {
		list_flush(&ecall->audio.level.l);
		ecall->audio.level.l = *levell;
	}

	return upd;
}
#endif

static void audio_level_handler(void *arg)
{
	struct ecall *ecall = arg;
	struct list levell = LIST_INIT;
	int err = 0;
	
	tmr_start(&ecall->audio.level.tmr, TIMEOUT_AUDIO_LEVEL,
		  audio_level_handler, ecall);

	if (!ecall->icall.audio_levelh)
		return;

#if 0
	(void)err;
#else
	err = IFLOW_CALLE(ecall->flow, get_audio_level, &levell);
	if (err) {
		warning("ecall(%p): could not get audio levels: %m\n", ecall, err);
		return;
	}
#endif

	//info("levels from flow: %H\n", audio_level_list_debug, &levell);
	
	/* Compare previous levels to these levels, if order has changed,
	 * trigger callback
	 */
#if 1
	ICALL_CALL_CB(ecall->icall, audio_levelh,
		      &ecall->icall, &levell, ecall->icall.arg);
	list_flush(&levell);
#else
	ecall->audio.level.cycle++;

	if (audio_level_update(ecall, &levell)) {
		ICALL_CALL_CB(ecall->icall, audio_levelh,
			      &ecall->icall, &ecall->audio.level.l, ecall->icall.arg);
	}
	else if (ecall->audio.level.cycle > AUDIO_LEVEL_UPDATE_CYCLE) {
		if (ecall->audio.level.l.head)
			list_flush(&ecall->audio.level.l);
		ecall->audio.level.l = levell;

		ICALL_CALL_CB(ecall->icall, audio_levelh,
			      &ecall->icall, &ecall->audio.level.l, ecall->icall.arg);
		ecall->audio.level.cycle = 0;
	}
	else {
		list_flush(&levell);
	}
#endif
}


static void channel_estab_handler(struct iflow *iflow, void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}
	info("ecall(%p): data channel established\n", ecall);

	tmr_cancel(&ecall->dc_tmr);

	econn_set_datachan_established(ecall->econn);

	if (ecall->delayed_restart) {
		ecall->delayed_restart = false;
		ecall_restart(ecall, ecall->call_type);
		return;
	}

	/* Update the cbr status */
	if (IFLOW_CALLE(ecall->flow, get_audio_cbr, true)){
		err = econn_props_update(ecall->props_local,
					 "audiocbr", "true");
		if (err) {
			warning("ecall: econn_props_update(audiocbr)",
			        " failed (%m)\n", err);
			goto error;
		}
	}

	/* sync the properties to the remote peer */
	if (!ecall->devpair
	    && econn_can_send_propsync(ecall->econn)) {
		err = econn_send_propsync(ecall->econn, false,
					  ecall->props_local);
		if (err) {
			warning("ecall: channel_estab: econn_send_propsync"
				" failed (%m)\n", err);
			goto error;
		}
	}

	ecall->update = false;
	ecall->num_retries = 0;

	if (ecall->icall.audio_levelh) {
		tmr_start(&ecall->audio.level.tmr, TIMEOUT_AUDIO_LEVEL,
			  audio_level_handler, ecall);
	}
	
	ICALL_CALL_CB(ecall->icall, datachan_estabh,
		&ecall->icall, ecall->userid_peer, ecall->clientid_peer,
		ecall->update, ecall->icall.arg);

	return;

 error:
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


void propsync_get_states(struct econn_props *props,
			 bool *vstate_present, 
			 int  *vstate, 
			 bool *muted_present,
			 bool *muted)
{
	const char *vr;
	const char *mt;

	if (!props) {
		return;
	}

	*vstate = ICALL_VIDEO_STATE_STOPPED;
	*vstate_present = false;
	*muted = 0;
	*muted_present = false;

	vr = econn_props_get(props, "videosend");
	if (vr) {
		*vstate_present = true;
		if (strcmp(vr, "true") == 0) {
			*vstate = ICALL_VIDEO_STATE_STARTED;
		}
		else if (strcmp(vr, "paused") == 0) {
			*vstate = ICALL_VIDEO_STATE_PAUSED;
		}
	}

	vr = econn_props_get(props, "screensend");
	if (vr) {
		*vstate_present = true;
		if (strcmp(vr, "true") == 0) {
			// screenshare overrides video started
			*vstate = ICALL_VIDEO_STATE_SCREENSHARE;
		}
		else if (strcmp(vr, "paused") == 0 &&
			ICALL_VIDEO_STATE_STARTED != *vstate) {
			// video started overrides screenshare paused
			*vstate = ICALL_VIDEO_STATE_PAUSED;
		}
	}
    
	mt = econn_props_get(props, "muted");
	if (mt) {
		*muted_present = true;
		*muted = (strcmp(mt, "true") == 0);
	}

}


static void propsync_handler(struct ecall *ecall)
{
	int vstate = ICALL_VIDEO_STATE_STOPPED;
	bool vstate_present = false;
	bool muted = false;
	bool muted_present = false;

	if (!ecall) {
		warning("ecall(%p): propsyc_handler ecall is NULL, "
			"ignoring props\n", ecall);
		return;
	}

	info("econn(%p): c3_props_recv: %H\n", ecall->econn,
	     econn_props_print, ecall->props_remote);
	info("ecall(%p): propsync_handler current recv_state %s\n",
	     ecall, icall_vstate_name(ecall->video.recv_state));

	propsync_get_states(ecall->props_remote,
			    &vstate_present,
			    &vstate,
			    &muted_present,
			    &muted);
    
	if (vstate_present && vstate != ecall->video.recv_state) {
		info("ecall(%p): propsync_handler updating recv_state "
		     "%s -> %s\n",
		     ecall,
		     icall_vstate_name(ecall->video.recv_state),
		     icall_vstate_name(vstate));
		
		ecall->video.recv_state = vstate;

		if (ecall->icall.vstate_changedh) {
			ICALL_CALL_CB(ecall->icall, vstate_changedh,
				      &ecall->icall, ecall->userid_peer,
				      ecall->clientid_peer, vstate, ecall->icall.arg);
		}
	}

	if (muted_present && muted != ecall->audio.remote_muted_state) {
		info("ecall(%p): propsync_handler updating mute_state "
		     "%s -> %s\n",
		     ecall,
		     muted ? "true" : "false",
		     ecall->audio.remote_muted_state ? "true" : "false");
		ecall->audio.remote_muted_state = muted;

		ICALL_CALL_CB(ecall->icall, muted_changedh,
			      &ecall->icall, 
			      ecall->userid_peer,
			      ecall->clientid_peer,
			      muted ? 1 : 0,
			      ecall->icall.arg);
	}

}


static int handle_propsync(struct ecall *ecall, struct econn_message *msg)
{
	int err = 0;

	if (!ecall->devpair &&
	    econn_message_isrequest(msg) &&
	    econn_can_send_propsync(ecall->econn)) {

		err = econn_send_propsync(ecall->econn, true,
					  ecall->props_local);
		if (err) {
			warning("ecall: data_recv:"
				" econn_send_propsync"
				" failed (%m)\n",
				err);
		}
	}

	if (ecall->propsynch) {
		return ecall->propsynch(ecall, msg, ecall->icall.arg);
	}

	if (msg->u.propsync.props) {
		mem_deref(ecall->props_remote);
		ecall->props_remote = mem_ref(msg->u.propsync.props);
	}

	propsync_handler(ecall);

	return err;
}

static void data_channel_handler(struct iflow *iflow,
				 const uint8_t *data, 
				 size_t len,
				 void *arg)
{
	struct ecall *ecall = arg;
	struct econn_message *msg = 0;
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char dest_userid_anon[ANON_ID_LEN];
	char dest_clientid_anon[ANON_CLIENT_LEN];
	int err;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}

	err = econn_message_decode(&msg, 0, 0, (char *)data, len);
	if (err) {
		warning("ecall: channel: failed to decode %zu bytes (%m)\n",
			len, err);
		return;
	}

	/* Check that message was received via correct transport */
	if (ECONN_TRANSP_DIRECT != econn_transp_resolve(msg->msg_type)) {
		warning("ecall: dc_recv: wrong transport for type %s\n",
			econn_msg_name(msg->msg_type));
	}

	if (msg->msg_type != ECONN_PING) {
		info("ecall(%p): dce_message_recv: convid=%s from=%s.%s to=%s.%s "
		     "msg=%H\n",
		     ecall, anon_id(convid_anon, ecall->convid),
		     anon_id(dest_userid_anon, ecall->userid_peer),
		     anon_client(dest_clientid_anon, ecall->clientid_peer),
		     anon_id(userid_anon, ecall->userid_self),
		     anon_client(clientid_anon, ecall->clientid_self),
		     econn_message_print, msg);

		ecall_trace(ecall, msg, false, ECONN_TRANSP_DIRECT,
			    "DataChan %H\n", econn_message_brief, msg);
	}

	if (msg->msg_type == ECONN_PROPSYNC) {
		handle_propsync(ecall, msg);
	}
	else {
		/* forward message to ECONN */
		econn_recv_message(ecall->econn, econn_userid_remote(ecall->econn),
				   econn_clientid_remote(ecall->econn), msg);
	}

	mem_deref(msg);
}


static void channel_close_handler(struct iflow *iflow, void *arg)
{
	struct ecall *ecall = arg;

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}

	ecall_close(ecall, EDATACHANNEL, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void rtp_start_handler(struct iflow *iflow,
			      bool started,
			      bool video_started,
			      void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n", ecall, __FUNCTION__);
		return;
	}

	if (started) {
		ecall->num_retries = 0;
		ICALL_CALL_CB(ecall->icall, audio_estabh,
			&ecall->icall, ecall->userid_peer, ecall->clientid_peer,
			ecall->update, ecall->icall.arg);

		if (ecall->audio_setup_time < 0 && ecall->ts_answered){
			uint64_t now = tmr_jiffies();
			ecall->audio_setup_time = now - ecall->ts_answered;
			ecall->call_setup_time = now - ecall->ts_started;
		}

		if (ecall->oldflow) {
			IFLOW_CALL(ecall->oldflow, close);
			ecall->oldflow = NULL;
		}
	}
}


static void acbr_detect_handler(struct iflow *iflow,
				int enabled,
				void *arg)
{
	bool cbr_enabled = false;
	bool local_cbr = false;
	bool remote_cbr = false;
	const char *cr, *cl;

	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (iflow != ecall->flow) {
		info("ecall(%p): ignoring %s on wrong flow\n",
		     ecall, __FUNCTION__);
		return;
	}

	cl = ecall_props_get_local(ecall, "audiocbr");
	local_cbr = cl && (0 == strcmp(cl, "true"));

	cr = ecall_props_get_remote(ecall, "audiocbr");
	remote_cbr = cr && (0 == strcmp(cr, "true"));

	cbr_enabled = (enabled != 0) && (remote_cbr && local_cbr);
	if (enabled != ecall->audio.cbr_state) {
		info("ecall(%p): acbrh(%p) enabled=%d "
		     "lcbr=%s rcbr=%s cbr=%d\n",
		     ecall, ecall->icall.acbr_changedh,
		     enabled,
		     local_cbr ? "true" : "false",
		     remote_cbr ? "true" : "false",
		     cbr_enabled);

		if (ecall->icall.acbr_changedh) {
			ICALL_CALL_CB(ecall->icall, acbr_changedh,
				      &ecall->icall, ecall->userid_peer,
				      ecall->clientid_peer,
				      cbr_enabled, ecall->icall.arg);
			ecall->audio.cbr_state = cbr_enabled;
		}
	}
}


static void norelay_handler(struct iflow *iflow, bool local, void *arg)
{
	struct ecall *ecall = arg;

	ICALL_CALL_CB(ecall->icall, norelayh, &ecall->icall, local, ecall->icall.arg);
}

	

int ecall_set_media_laddr(struct ecall *ecall, struct sa *laddr)
{
	struct sa *maddr;
	
	if (!ecall || !laddr)
		return EINVAL;
	
	ecall->media_laddr = mem_deref(ecall->media_laddr);
	maddr = mem_zalloc(sizeof(*maddr), NULL);
	if (!maddr)
		return ENOMEM;

	sa_cpy(maddr, laddr);	
	ecall->media_laddr = maddr;

	return 0;
}

static int alloc_flow(struct ecall *ecall, enum async_sdp role,
		      enum icall_call_type call_type,
		      bool audio_cbr)
{
	//struct sa laddr;
	char tag[64] = "";
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	size_t i;
	int err;

	IFLOW_CALL(ecall->oldflow, close);
	ecall->oldflow = ecall->flow;
	ecall->flow = NULL;
	ecall->established = false;

	err = iflow_alloc(&ecall->flow,
			  ecall->convid,
			  ecall->userid_self,
			  ecall->clientid_self,
			  ecall->conv_type,
			  call_type,
			  ecall->vstate,
			  ecall->icall.arg);

	if (err) {
		warning("ecall(%p): failed to alloc mediaflow (%m)\n",
			ecall, err);
		goto out;
	}

	iflow_set_callbacks(ecall->flow,
			    mf_estab_handler,
			    mf_close_handler,
			    mf_stopped_handler,
			    rtp_start_handler,
			    mf_restart_handler,
			    mf_gather_handler,
			    channel_estab_handler,
			    data_channel_handler,
			    channel_close_handler,
			    acbr_detect_handler,
			    norelay_handler,
			    ecall);
	info("ecall(%p): alloc_flow: user=%s client=%s mediaflow=%p "
	     "call_type=%d audio_cbr=%d\n",
	     ecall,
	     anon_id(userid_anon, ecall->userid_peer),
	     anon_client(clientid_anon, ecall->clientid_peer),
	     ecall->flow, call_type, audio_cbr);

	ecall->audio_cbr = audio_cbr;
	IFLOW_CALL(ecall->flow, set_audio_cbr, audio_cbr);

	if (str_isset(ecall->userid_peer) || str_isset(ecall->clientid_peer)) {
		IFLOW_CALL(ecall->flow, set_remote_userclientid,
			   ecall->userid_peer, ecall->clientid_peer);
	}
	IFLOW_CALL(ecall->flow, set_video_state, ecall->vstate);
	
	for(i = 0; i < ecall->turnc; ++i) {
		struct zapi_ice_server *t = &ecall->turnv[i];

		IFLOW_CALL(ecall->flow, add_turnserver,
			t->url,
			t->username,
			t->credential);
	}

	if (ecall->keystore) {
		IFLOW_CALL(ecall->flow, set_keystore,
			   ecall->keystore);
	}
	re_snprintf(tag, sizeof(tag), "%s.%s",
		    ecall->userid_self, ecall->clientid_self);
	//mediaflow_set_tag(ecall->flow, tag);

	//mediaflow_set_rtpstate_handler(ecall->flow, rtp_start_handler);

	if (msystem_get_privacy(ecall->msys)) {
		info("ecall(%p): alloc_flow: enable mediaflow privacy\n",
		     ecall);
		IFLOW_CALL(ecall->flow, enable_privacy, true);
	}

	/* DataChannel is mandatory in Calling 3.0 */
/*
	err = mediaflow_add_data(ecall->flow);
	if (err) {
		warning("ecall(%p): mediaflow add data failed (%m)\n",
			ecall, err);
		goto out;
	}

	if (ecall->media_laddr) {
		info("ecall(%p): overriding interface with: %j\n",
		     ecall, ecall->media_laddr);
		mediaflow_set_media_laddr(ecall->flow, ecall->media_laddr);
	}
*/	/* populate all network interfaces */
	ecall->turn_added = true;
	if (role == ASYNC_OFFER)
		gather_all(ecall, true);
 out:
	if (err) {
		struct iflow *flow = ecall->flow;
		
		ecall->flow = NULL;
		IFLOW_CALL(flow, close);
	}

	return err;
}


int ecall_create_econn(struct ecall *ecall)
{
	int err;

	if (!ecall)
		return EINVAL;

	assert(ecall->econn == NULL);

	err = econn_alloc(&ecall->econn, &ecall->conf.econf,
			  ecall->userid_self,
			  ecall->clientid_self,
			  ecall->sessid,
			  &ecall->transp,
			  econn_conn_handler,
			  econn_answer_handler,
			  econn_update_req_handler,
			  econn_update_resp_handler,
			  econn_alert_handler,
			  econn_confpart_handler,
			  econn_ping_handler,
			  econn_close_handler,
			  ecall);
	if (err) {
		warning("ecall_setup: econn_alloc failed: %m\n", err);
		return err;
	}

	info("ecall(%p): created econn: %p\n", ecall, ecall->econn);
	
	return err;
}


int ecall_start(struct ecall *ecall, enum icall_call_type call_type,
		bool audio_cbr)
{
	int err;

	info("ecall(%p): start\n", ecall);

	if (!ecall)
		return EINVAL;

#ifdef ECALL_CBR_ALWAYS_ON
	audio_cbr = true;
#endif

	if (ecall->econn) {
		if (ECONN_PENDING_INCOMING == econn_current_state(ecall->econn)) {
			return ecall_answer(ecall, call_type, audio_cbr);
		}
		else {
			warning("ecall: start: already in progress (econn=%s)\n",
				econn_state_name(econn_current_state(ecall->econn)));
			return EALREADY;
		}
	}

#if 0
	if (ecall->turnc == 0) {
		warning("ecall: start: no TURN servers -- cannot start\n");
		return EINTR;
	}
#endif

	ecall->call_type = call_type;
	
	err = ecall_create_econn(ecall);
	if (err) {
		warning("ecall: start: create_econn failed: %m\n", err);
		return err;
	}

	econn_set_state(ecall_get_econn(ecall), ECONN_PENDING_OUTGOING);

	err = alloc_flow(ecall, ASYNC_OFFER, ecall->call_type, audio_cbr);
	if (err) {
		warning("ecall: start: alloc_flow failed: %m\n", err);
		goto out;
	}

	IFLOW_CALL(ecall->flow, set_audio_cbr, audio_cbr);
	
	if (ecall->props_local &&
	    (call_type == ICALL_CALL_TYPE_VIDEO
	     && ecall->vstate == ICALL_VIDEO_STATE_STARTED)) {
		const char *vstate_string = "true";

		int err2 = econn_props_update(ecall->props_local,
					      "videosend", vstate_string);
		if (err2) {
			warning("ecall(%p): econn_props_update(videosend)",
				" failed (%m)\n", ecall, err2);
			/* Non fatal, carry on */
		}
	}

	ecall->sdp.async = ASYNC_NONE;
	err = generate_offer(ecall);
	if (err) {
		warning("ecall(%p): start: generate_offer"
			" failed (%m)\n", ecall, err);
		goto out;
	}

	ecall->ts_started = tmr_jiffies();
	ecall->call_setup_time = -1;

 out:
	/* err handling */
	return err;
}


int ecall_answer(struct ecall *ecall, enum icall_call_type call_type,
		 bool audio_cbr)
{
	int err = 0;

	if (!ecall)
		return EINVAL;

#ifdef ECALL_CBR_ALWAYS_ON
	audio_cbr = true;
#endif
	

	info("ecall(%p): answer on pending econn %p call_type=%d\n", ecall, ecall->econn, call_type);

	if (!ecall->econn) {
		warning("ecall: answer: econn does not exist!\n");
		return ENOENT;
	}

	if (ECONN_PENDING_INCOMING != econn_current_state(ecall->econn)) {
		info("ecall(%p): answer: invalid state (%s)\n", ecall,
		     econn_state_name(econn_current_state(ecall->econn)));
		return EPROTO;
	}

	if (!ecall->flow) {
		warning("ecall: answer: no mediaflow\n");
		return EPROTO;
	}

	ecall->call_type = call_type;
	IFLOW_CALL(ecall->flow, set_call_type, call_type);

	ecall->audio_cbr = audio_cbr;
	IFLOW_CALL(ecall->flow, set_audio_cbr, audio_cbr);

#if 0
	if (ecall->props_local) {
		const char *vstate_string =
			call_type == ICALL_CALL_TYPE_VIDEO ? "true" : "false";
		int err2 = econn_props_update(ecall->props_local, "videosend", vstate_string);
		if (err2) {
			warning("ecall(%p): econn_props_update(videosend)",
				" failed (%m)\n", ecall, err2);
			/* Non fatal, carry on */
		}
	}
#endif

	err = generate_or_gather_answer(ecall, ecall->econn);
	if (err) {
		warning("ecall: answer: failed to gather_or_answer\n");
		goto out;
	}

	ecall->answered = true;
	ecall->audio_setup_time = -1;
	ecall->call_estab_time = -1;
	ecall->ts_answered = tmr_jiffies();

 out:
	return err;
}


int ecall_msg_recv(struct ecall *ecall,
		   uint32_t curr_time, /* in seconds */
		   uint32_t msg_time, /* in seconds */
		   const char *userid_sender,
		   const char *clientid_sender,
		   struct econn_message *msg)
{
	char userid_anon[ANON_ID_LEN];
	int err = 0;
	bool update_ids = false;

	info("ecall(%p): msg_recv: %H\n", ecall, econn_message_brief, msg);

	if (!ecall || !userid_sender || !clientid_sender || !msg)
		return EINVAL;

	ecall_trace(ecall, msg, false, ECONN_TRANSP_BACKEND,
		    "SE %H\n", econn_message_brief, msg);

	if (userid_sender && !ecall->userid_peer) {
		ecall_set_peer_userid(ecall, userid_sender);
		update_ids = true;
	}
	if (clientid_sender && !ecall->clientid_peer) {
		ecall_set_peer_clientid(ecall, clientid_sender);
		update_ids = true;
	}
	
	if (update_ids) {
		info("ecall(%p): updating ids on flow: %p\n", ecall, ecall->flow);
	}
	IFLOW_CALL(ecall->flow, set_remote_userclientid,
		   ecall->userid_peer, ecall->clientid_peer);
	
	if (ECONN_PROPSYNC == msg->msg_type) {
		err = handle_propsync(ecall, msg);
		if (err) {
			warning("ecall(%p): recv: handle_propsync failed\n", ecall);
		}
		return err;
	}

	/* Check that message was received via correct transport */
	if (ECONN_TRANSP_BACKEND != econn_transp_resolve(msg->msg_type)) {
		warning("ecall: recv: wrong transport for type %s\n",
			econn_msg_name(msg->msg_type));
	}

	/* Messages from the same userid.
	 */
	if (0 == str_casecmp(ecall->userid_self, userid_sender) &&
		ecall->conv_type == ICALL_CONV_TYPE_ONEONONE) {

		if (msg->msg_type == ECONN_REJECT || 
			(msg->msg_type == ECONN_SETUP && msg->resp)) {

			/* Received SETUP(r) or REJECT from other Client.
			 * We must stop the ringing.
			 */
			info("ecall: other client %s"
			     " -- stop ringtone\n", msg->msg_type == ECONN_REJECT ? 
			     "rejected" : "answered");

			if (ecall->econn &&
				econn_current_state(ecall->econn) == ECONN_PENDING_INCOMING) {

				int why = msg->msg_type == ECONN_REJECT ? EREMOTE : EALREADY;
				econn_close(ecall->econn, why,
					msg ? msg->time : ECONN_MESSAGE_TIME_UNKNOWN);
			}
			else {
				info("no pending incoming econns\n");
			}
		}
		else {
			info("ecall(%p): ignore message %s from"
			     " same user (%s)\n", ecall,
			     econn_msg_name(msg->msg_type), anon_id(userid_anon, userid_sender));
		}

		goto out;
	}

	/* create a new ECONN */
	if (!ecall->econn &&
	    econn_is_creator_full(ecall->userid_self, ecall->clientid_self,
				  userid_sender, clientid_sender, msg)) {

		err = ecall_create_econn(ecall);
		if (err) {
			warning("ecall: transp_recv: econn_alloc failed:"
				" %m\n", err);
			goto out;
		}
	}

	/* forward the received message to ECONN */
	econn_recv_message(ecall->econn, userid_sender, clientid_sender, msg);

 out:
	return err;
}

void ecall_transp_recv(struct ecall *ecall,
		       uint32_t curr_time, /* in seconds */
		       uint32_t msg_time, /* in seconds */
		       const char *userid_sender,
		       const char *clientid_sender,
		       const char *str)
{
	struct econn_message *msg;
	int err;

	if (!ecall || !userid_sender || !clientid_sender || !str)
		return;

	err = econn_message_decode(&msg, curr_time, msg_time,
				   str, str_len(str));
	if (err) {
		warning("ecall: could not decode message %zu bytes (%m)\n",
			str_len(str), err);
		return;
	}

	ecall_msg_recv(ecall, curr_time, msg_time,
		       userid_sender, clientid_sender, msg);

	mem_deref(msg);
}


struct ecall *ecall_find_convid(const struct list *ecalls, const char *convid)
{
	struct le *le;

	for (le = list_head(ecalls); le; le = le->next) {
		struct ecall *ecall = le->data;

		if (0 == str_casecmp(convid, ecall->convid))
			return ecall;
	}

	return NULL;
}


void ecall_end(struct ecall *ecall)
{
	char userid_anon[ANON_ID_LEN];
	if (!ecall)
		return;

	info("ecall(%p): [self=%s] end\n", ecall, anon_id(userid_anon, ecall->userid_self));

	/* disconnect all connections */
	econn_end(ecall->econn);

	IFLOW_CALL(ecall->flow, stop_media);
}


int icall_send_reject_msg(struct icall *icall,
			  const struct list *clientl,
			  const char *userid_local,
			  const char *clientid_local)
{
	struct list targets = LIST_INIT;
	struct le *le = NULL;
	struct econn_message *msg = NULL;
	int err = 0;

	if (!icall || !clientl || !userid_local || !clientid_local)
		return EINVAL;

	info("icall(%p): send_reject_msg\n",
	     icall);

	LIST_FOREACH(clientl, le) {
		struct icall_client *cli = le->data;
		if (cli && strcaseeq(cli->userid, userid_local) &&
		    !strcaseeq(cli->clientid, clientid_local)) {
			struct icall_client *tgt;

			tgt = icall_client_alloc(cli->userid,
						 cli->clientid);
			if (!tgt) {
				warning("icall(%p): send_reject_msg "
					"unable to alloc target\n", icall);
				goto out;
			}
			list_append(&targets, &tgt->le, tgt);
		}
	}

	info("icall(%p): send_reject_msg targets=%u\n",
	     icall,
	     list_count(&targets));

	if (list_count(&targets) > 0) {

	msg = econn_message_alloc();
	if (!msg) {
		err = ENOMEM;
		goto out;
	}

	str_ncpy(msg->src_userid, userid_local, ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, clientid_local, ECONN_ID_LEN);
	msg->msg_type = ECONN_REJECT;
	msg->resp = false;

	err = ICALL_CALL_CBE((*icall), sendh,
			     icall,
			     userid_local,
			     msg, &targets, icall->arg);
	}

out:
	mem_deref(msg);
	list_flush(&targets);
	return err;
}


void ecall_set_clients(struct ecall* ecall,
		       struct list *clientl)
{
	int err = 0;

	if (!ecall || !clientl)
		return;

	if (ecall->should_reject) {
		ecall_end(ecall);
		err = icall_send_reject_msg(&ecall->icall,
					    clientl,
					    ecall->userid_self,
					    ecall->clientid_self);
		ecall->should_reject = false;
		if (err)
			warning("ecall(%p): send_reject_message failed %m\n",
				ecall, err);
	}
}


void ecall_reject(struct ecall *ecall)
{
	char userid_anon[ANON_ID_LEN];
	if (!ecall)
		return;

	info("ecall(%p): [self=%s] reject\n", ecall, anon_id(userid_anon, ecall->userid_self));

	ecall->should_reject = true;
	ICALL_CALL_CB(ecall->icall, req_clientsh,
		      &ecall->icall, ecall->icall.arg);
	
}


enum econn_state ecall_state(const struct ecall *ecall)
{
	if (!ecall)
		return ECONN_IDLE;

	if (ecall->econn) {
		return econn_current_state(ecall->econn);
	}
	else {
		return ECONN_IDLE;
	}
}


struct econn *ecall_get_econn(const struct ecall *ecall)
{
	if (!ecall)
		return NULL;

	return ecall->econn;
}


int ecall_set_video_send_state(struct ecall *ecall, enum icall_vstate vstate)
{
	int err = 0;

	if (!ecall)
		return EINVAL;

	info("ecall(%p): set_video_send_state %s econn %p update %d\n",
	     ecall,
	     icall_vstate_name(vstate),
	     ecall->econn,
	     ecall->update);

	const char *vstate_string;
	const char *sstate_string;
	switch (vstate) {
	case ICALL_VIDEO_STATE_STARTED:
		vstate_string = "true";
		sstate_string = "false";
		break;
	case ICALL_VIDEO_STATE_SCREENSHARE:
		vstate_string = "false";
		sstate_string = "true";
		break;
	case ICALL_VIDEO_STATE_PAUSED:
		vstate_string = "paused";
		sstate_string = "false";
		break;
	case ICALL_VIDEO_STATE_STOPPED:
	default:
		vstate_string = "false";
		sstate_string = "false";
		break;
	}
	err = econn_props_update(ecall->props_local, "videosend", vstate_string);
	if (err) {
		warning("ecall(%p): econn_props_update(videosend)",
			" failed (%m)\n", ecall, err);
		return err;
	}
	err = econn_props_update(ecall->props_local, "screensend", sstate_string);
	if (err) {
		warning("ecall(%p): econn_props_update(screensend)",
			" failed (%m)\n", ecall, err);
		return err;
	}

	ecall->vstate = vstate;
	if (ecall->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		ICALL_CALL_CB(ecall->icall, vstate_changedh,
			      &ecall->icall, ecall->userid_self, ecall->clientid_self,
			      vstate, ecall->icall.arg);
	}

	/* if the call has video, update mediaflow */
	if (ecall->flow && ICALL_CALLE(ecall->flow, has_video)) {
		err = IFLOW_CALLE(ecall->flow, set_video_state, vstate);
		if (err) {
			warning("ecall(%p): set_video_send_active:"
				" failed to set mf->active (%m)\n", ecall, err);
			goto out;
		}
	}
	else if (ICALL_VIDEO_STATE_STARTED == vstate
		 || ICALL_VIDEO_STATE_SCREENSHARE == vstate) {
		/* If webapp sent us a SETUP for audio only call and we are escalating, */
		/* force an UPDATE so they can answer with video recvonly AUDIO-1549 */
		enum econn_state state = econn_current_state(ecall->econn);
		switch (state) {
		case ECONN_ANSWERED:
		case ECONN_DATACHAN_ESTABLISHED:
			ecall_restart(ecall, ICALL_CALL_TYPE_VIDEO);
			goto out;
			
		default:
			break;
		}
	}

	/* sync the properties to the remote peer */
	if (!ecall->devpair
	    && !ecall->update
	    && econn_can_send_propsync(ecall->econn)) {
		info("ecall(%p): set_video_send_state: setting props "
			"videosend:%s screensend:%s\n",
			ecall, vstate_string, sstate_string);

		err = econn_send_propsync(ecall->econn, false,
					  ecall->props_local);
		if (err) {
			warning("ecall: set_video: econn_send_propsync"
				" failed (%m)\n",
				err);
			goto out;
		}
	}

 out:
	return err;
}


bool ecall_is_answered(const struct ecall *ecall)
{
	return ecall ? ecall->answered : false;
}


bool ecall_has_video(const struct ecall *ecall)
{
	if (!ecall)
		return false;

	if (!ecall->econn)
		return false;

	if (!ecall->flow)
		return false;

	return IFLOW_CALLE(ecall->flow, has_video);
}


int ecall_media_start(struct ecall *ecall)
{
	int err = 0;
	const char *is_video;

	debug("ecall: media start ecall=%p\n", ecall);

	if (!ecall)
		return EINVAL;
/*
	if (!IFLOW_CALLE(ecall->flow, is_ready)){
		info("ecall(%p): mediaflow not ready cannot start media \n",
		     ecall);
		return 0;
	}
*/
	is_video = ecall_props_get_local(ecall, "videosend");
	if (is_video && strcmp(is_video, "true") == 0) {
		IFLOW_CALL(ecall->flow, set_video_state, ecall->vstate);
	}
/*
	err = mediaflow_start_media(ecall->flow);
	if (err) {
		warning("ecall: mediaflow start media failed (%m)\n", err);
		econn_set_error(ecall->econn, err);
		goto out;
	}
*/	info("ecall(%p): media started on flow:%p\n", ecall, ecall->flow);

	tmr_cancel(&ecall->media_start_tmr);

 //out:
	return err;
}


void ecall_media_stop(struct ecall *ecall)
{
	debug("ecall: media stop ecall=%p\n", ecall);

	if (!ecall)
		return;

	IFLOW_CALL(ecall->flow, stop_media);
	info("ecall(%p): media stopped on ecall:%p\n", ecall, ecall);
}


int ecall_propsync_request(struct ecall *ecall)
{
	int err;

	if (!ecall)
		return EINVAL;

	if (!ecall->econn)
		return EINTR;

	if (ecall->devpair)
		return 0;

	err = econn_send_propsync(ecall->econn, false, ecall->props_local);
	if (err) {
		warning("ecall: request: econn_send_propsync failed (%m)\n",
			err);
		return err;
	}

	return err;
}


const char *ecall_props_get_local(struct ecall *ecall, const char *key)
{
	if (!ecall)
		return NULL;

	if (!ecall->econn)
		return NULL;

	return econn_props_get(ecall->props_local, key);
}


const char *ecall_props_get_remote(struct ecall *ecall, const char *key)
{
	if (!ecall)
		return NULL;

	if (!ecall->econn)
		return NULL;

	return econn_props_get(ecall->props_remote, key);
}


int ecall_debug(struct re_printf *pf, const struct ecall *ecall)
{
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	if (!ecall)
		return 0;

	err |= re_hprintf(pf, "ECALL SUMMARY %p:\n", ecall);
	err |= re_hprintf(pf, "convid:      %s\n", anon_id(convid_anon, ecall->convid));
	err |= re_hprintf(pf, "userid_self: %s\n", anon_id(userid_anon, ecall->userid_self));
	err |= re_hprintf(pf, "clientid:    %s\n",
			  anon_client(clientid_anon, ecall->clientid_self));
	err |= re_hprintf(pf, "async_sdp:   %s\n",
			  async_sdp_name(ecall->sdp.async));
	err |= re_hprintf(pf, "answered:    %s\n",
			  ecall->answered ? "Yes" : "No");
	err |= re_hprintf(pf, "estab_time:  %d ms\n", ecall->call_estab_time);
	err |= re_hprintf(pf, "audio_setup_time:  %d ms\n",
			  ecall->audio_setup_time);

	if (ecall->flow && ecall->flow->debug) {
		err |= re_hprintf(pf, "mediaflow:   %H\n",
				  ecall->flow->debug, ecall->flow);
	}
	else {
		err |= re_hprintf(pf, "mediaflow:   None\n");
	}

	err |= re_hprintf(pf, "props_local:  %H\n",
			  econn_props_print, ecall->props_local);
	err |= re_hprintf(pf, "props_remote: %H\n",
			  econn_props_print, ecall->props_remote);

	if (ecall->econn) {
		err |= econn_debug(pf, ecall->econn);
	}
	else {
		err |= re_hprintf(pf, "econn:   None\n");
	}

	if (ecall->userid_peer) {
		err |= re_hprintf(pf, "userid_peer: %s\n",
				  anon_id(userid_anon, ecall->userid_peer));
	}
/* TODO fix dce status
	if (peerflow_get_dce(ecall->flow)) {
		err |= re_hprintf(pf, "DCE: %H\n",
				  dce_status, peerflow_get_dce(ecall->flow));
	}
*/
	err |= ecall_show_trace(pf, ecall);

	return err;
}

int ecall_stats(struct re_printf *pf, const struct ecall *ecall)
{
	struct iflow_stats stats;
	struct json_object *jfstats = NULL;
	int err = 0;

	memset(&stats, 0, sizeof(stats));

	IFLOW_CALL(ecall->flow, get_stats,
		&stats);

	jfstats = jzon_alloc_object();
	jzon_add_str(jfstats, "remoteUserId", "%s", ecall->userid_peer);
	jzon_add_str(jfstats, "remoteClientId", "%s", ecall->clientid_peer);
	jzon_add_int(jfstats, "audioPacketsReceived", stats.apkts_recv);
	jzon_add_int(jfstats, "audioPacketsSent", stats.apkts_sent);
	jzon_add_int(jfstats, "videoPacketsReceived", stats.vpkts_recv);
	jzon_add_int(jfstats, "videoPacketsSent", stats.vpkts_sent);
	
	jzon_print(pf, jfstats);

	mem_deref(jfstats);
	return err;
}

int ecall_set_sessid(struct ecall *ecall, const char *sessid)
{
	int err;

	if (!ecall)
		return EINVAL;

	mem_deref(ecall->sessid);
	err = str_dup(&ecall->sessid, sessid);

	return err;
}

void ecall_set_peer_userid(struct ecall *ecall, const char *userid)
{
	char userid_anon[ANON_ID_LEN];
	if (!ecall)
		return;

	ecall->userid_peer = mem_deref(ecall->userid_peer);
	if (userid) {
		str_dup(&ecall->userid_peer, userid);
		debug("ecall(%p): set_peer_userid %s\n", 
			ecall,
			anon_id(userid_anon, userid));
	}
}


void ecall_set_peer_clientid(struct ecall *ecall, const char *clientid)
{
	char clientid_anon[ANON_CLIENT_LEN];
	if (!ecall)
		return;

	ecall->clientid_peer = mem_deref(ecall->clientid_peer);
	if (clientid) {
		str_dup(&ecall->clientid_peer, clientid);
		debug("ecall(%p): set_peer_clientid %s\n", 
			ecall,
			anon_client(clientid_anon, clientid));
	}
}


const char *ecall_get_peer_userid(const struct ecall *ecall)
{
	if (!ecall)
		return NULL;

	return ecall->userid_peer;
}


const char *ecall_get_peer_clientid(const struct ecall *ecall)
{
	if (!ecall)
		return NULL;

	return ecall->clientid_peer;
}


int ecall_set_real_clientid(struct ecall *ecall, const char *clientid)
{
	char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	if (!ecall)
		return EINVAL;

	ecall->clientid_real = mem_deref(ecall->clientid_real);
	if (clientid) {
		err = str_dup(&ecall->clientid_real, clientid);
		if (err)
			return err;

		debug("ecall(%p): set_real_clientid %s\n", 
			ecall,
			anon_client(clientid_anon, clientid));
	}

	return err;
}


struct ecall *ecall_find_userclient(const struct list *ecalls,
				    const char *userid,
				    const char *clientid)
{
	struct le *le;

	for (le = list_head(ecalls); le; le = le->next) {
		struct ecall *ecall = le->data;

		if (0 == str_casecmp(userid, ecall->userid_peer) &&
			0 == str_casecmp(clientid, ecall->clientid_peer))
			return ecall;
	}

	return NULL;
}


int ecall_restart(struct ecall *ecall, enum icall_call_type call_type)
{
	enum econn_state state;
	int err = 0;
	bool muted;

	if (!ecall)
		return EINVAL;

	state = econn_current_state(ecall->econn);

	switch (state) {
	case ECONN_ANSWERED:
	case ECONN_DATACHAN_ESTABLISHED:
		break;

	default:
		warning("ecall(%p): restart: cannot restart in state: '%s'\n",
			ecall, econn_state_name(state));
		return EPROTO;
	}

	if (ecall->conv_type == ICALL_CONV_TYPE_CONFERENCE) {
		ICALL_CALL_CB(ecall->icall, closeh,
			      &ecall->icall,
			      EAGAIN,
			      NULL,
			      0,
			      NULL,
			      NULL, 
			      ecall->icall.arg);
			
		return 0;
	}

	ecall->call_type = call_type;
	ecall->update = true;
	tmr_cancel(&ecall->dc_tmr);
	ecall->conf_part = mem_deref(ecall->conf_part);
	muted = msystem_get_muted();

	ecall->dce = NULL;
	ecall->dce_ch = NULL;
	err = alloc_flow(ecall, ASYNC_OFFER, ecall->call_type, ecall->audio_cbr);
	msystem_set_muted(muted);
	if (err) {
		warning("ecall: re-start: alloc_flow failed: %m\n", err);
		goto out;
	}
	//if (ecall->conf_part)
	//	ecall->conf_part->data = ecall->flow;

	IFLOW_CALL(ecall->flow, set_remote_userclientid,
		econn_userid_remote(ecall->econn),
		econn_clientid_remote(ecall->econn));
	IFLOW_CALL(ecall->flow, set_video_state, ecall->vstate);

	ecall->sdp.async = ASYNC_NONE;
	err = generate_offer(ecall);
	if (err) {
		warning("ecall(%p): restart: generate_offer"
			" failed (%m)\n", ecall, err);
		goto out;
	}

 out:
	return err;
}

struct conf_part *ecall_get_conf_part(struct ecall *ecall)
{
	return ecall ? ecall->conf_part : NULL;
}


void ecall_set_conf_part(struct ecall *ecall, struct conf_part *cp)
{
	if (!ecall)
		return;

	if (ecall->conf_part)
		ecall->conf_part = mem_deref(ecall->conf_part);

	ecall->conf_part = cp;
	//if (ecall->conf_part)
	//	ecall->conf_part->data = ecall->flow;
}


int ecall_remove(struct ecall *ecall)
{
	if (!ecall)
		return EINVAL;

	list_unlink(&ecall->le);
	return 0;
}


static void quality_handler(void *arg)
{
	struct ecall *ecall = arg;
	struct iflow_stats stats;
	int err = 0;

	memset(&stats, 0, sizeof(stats));

	tmr_start(&ecall->quality.tmr, ecall->quality.interval,
		  quality_handler, arg);

	if (!ecall->icall.qualityh || !ecall->flow)
		return;
	err = IFLOW_CALLE(ecall->flow, get_stats,
			  &stats);
	if (!err) {
		ICALL_CALL_CB(ecall->icall, qualityh,
			      &ecall->icall, 
			      ecall->userid_peer,
			      ecall->clientid_peer,
			      stats.rtt,
			      stats.dloss,
			      0.0f,
			      ecall->icall.arg);
	}
}

int ecall_set_quality_interval(struct ecall *ecall,
			      uint64_t interval)
{
	if (!ecall)
		return EINVAL;
	
	ecall->quality.interval = interval;

	if (interval == 0)
		tmr_cancel(&ecall->quality.tmr);
	else
		tmr_start(&ecall->quality.tmr, interval,
			  quality_handler, ecall);

	return 0;
}

int ecall_update_mute_state(const struct ecall *ecall)
{
	const char *muted_string;
	bool muted;
	int err = 0;
	
	if (!ecall) {
		return EINVAL;
	}

	muted = msystem_get_muted();

	muted_string = muted ? "true" : "false";
	err = econn_props_update(ecall->props_local, "muted", muted_string);
	if (err) {
		warning("ecall(%p): econn_props_update(muted)",
			" failed (%m)\n", ecall, err);
		return err;
	}

	/* sync the properties to the remote peer */
	if (!ecall->devpair
	    && !ecall->update
	    && econn_can_send_propsync(ecall->econn)) {
		info("ecall(%p): update_mute_state: setting props "
			"muted:%s\n",
			ecall, muted_string);

		err = econn_send_propsync(ecall->econn, false,
					  ecall->props_local);
		if (err) {
			warning("ecall: update_mute_state: econn_send_propsync"
				" failed (%m)\n",
				err);
			goto out;
		}
	}

out:
	return err;
}

int ecall_add_decoders_for_user(struct ecall *ecall,
				const char *userid,
				const char *clientid,
				const char *userid_hash,
				uint32_t ssrca,
				uint32_t ssrcv)
{
	int err = 0;
	
	if (!ecall || !userid || !clientid) {
		return EINVAL;
	}

	if (ssrca ==0 && ssrcv == 0) {
		return EINVAL;
	}

	err = IFLOW_CALLE(ecall->flow, add_decoders_for_user,
		userid, clientid, userid_hash, ssrca, ssrcv);

	return err;
}


int ecall_remove_decoders_for_user(struct ecall *ecall,
				   const char *userid,
				   const char *clientid,
				   uint32_t ssrca,
				   uint32_t ssrcv)
{
	int err = 0;
	
	if (!ecall || !userid || !clientid) {
		return EINVAL;
	}

	if (ssrca == 0 && ssrcv == 0) {
		return EINVAL;
	}

	err = IFLOW_CALLE(ecall->flow, remove_decoders_for_user,
			  userid, clientid);

	return err;
}

int ecall_sync_decoders(struct ecall *ecall)
{
	int err;

	if (!ecall)
		return EINVAL;

	err = IFLOW_CALLE(ecall->flow, sync_decoders);

	return err;
}

int ecall_set_keystore(struct ecall *ecall,
		       struct keystore *keystore)
{
	if (!ecall || !keystore) {
		return EINVAL;
	}

	info("ecall(%p): set_keystore %p\n", ecall, keystore);
	ecall->keystore = mem_deref(ecall->keystore);
	ecall->keystore = mem_ref(keystore);

	IFLOW_CALL(ecall->flow, set_keystore,
		   keystore);
	return 0;
}

int ecall_ping(struct ecall *ecall, bool response)
{
	if (!ecall || !ecall->econn)
		return EINVAL;

	return econn_send_ping(ecall->econn, response);
}

static void econn_ping_handler(struct econn *econn,
			       bool response,
			       void *arg)
{
	struct ecall *ecall = arg;

	if (!ecall || !econn) {
		return;
	}

	assert(ECALL_MAGIC == ecall->magic);

	if (ecall->pingh) {
		ecall->pingh(ecall, response, ecall->icall.arg);
	}
}

int ecall_set_ping_handler(struct ecall *ecall,
			   ecall_ping_h *pingh)
{
	if (!ecall) {
		return EINVAL;
	}

	ecall->pingh = pingh;
	return 0;
}
