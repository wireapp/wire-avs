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
#include "avs_media.h"
#include "avs_dce.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_turn.h"
#include "avs_cert.h"
#include "avs_msystem.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_ecall.h"
#include "avs_conf_pos.h"
#include "avs_jzon.h"
#include "avs_mediastats.h"
#include "avs_version.h"
#include "ecall.h"


#define SDP_MAX_LEN 4096
#define ECALL_MAGIC 0xeca1100f

#define TIMEOUT_DC_CLOSE     10000
#define TIMEOUT_MEDIA_START  10000


static const struct ecall_conf default_conf = {
	.econf = {
		.timeout_setup = 60000,
		.timeout_term  =  5000,
	},
};


static int alloc_mediaflow(struct ecall *ecall);
static int generate_answer(struct ecall *ecall, struct econn *econn);


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
	ecall_close_h *closeh;
	struct mediaflow *mf;

	if (!ecall)
		return;

	closeh = ecall->closeh;

	if (err) {
		info("ecall(%p): closed (%m)\n", ecall, err);
	}
	else {
		info("ecall(%p): closed (normal)\n", ecall);
	}

	char *json_str = NULL;
	struct json_object *jobj;
	jobj = json_object_new_object();
	if (jobj){
		bool success = ecall_stats_prepare(ecall, jobj, err);
		if (success) {
			int ret = jzon_encode(&json_str, jobj);
			if (ret) {
				warning("ecall: jzon_encode failed (%m)\n",
					ret);
			}
		}
	}

	/* Keep mf reference, but indicate that it's gone */
	mf = ecall->mf;
	ecall->conf_part = mem_deref(ecall->conf_part);
	ecall->mf = NULL;
	mem_deref(mf);
	
	/* NOTE: calling the callback handlers MUST be done last,
	 *       to make sure that all states are correct.
	 */
	if (ecall->video.recv_state && ecall->vstateh) {
		ecall->vstateh(ecall, ecall->userid_peer, 0, ecall->arg);
		ecall->video.recv_state = 0;
	}

	if (closeh) {
		ecall->closeh = NULL;
		closeh(err, json_str, ecall, msg_time, ecall->arg);
	}

	mem_deref(jobj);
	mem_deref(json_str);
	/* NOTE here the app should have destroyed the econn */
}


static void econn_conn_handler(struct econn *econn,
			       uint32_t msg_time,
			       const char *userid_sender,
			       const char *clientid_sender,
			       uint32_t age,
			       const char *sdp,
			       struct econn_props *props,
			       void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;
	const char *vr;
	bool video_active = false;

	assert(ECALL_MAGIC == ecall->magic);

	/* check if the Peer UserID is set */
	if (str_isset(ecall->userid_peer)) {

		if (0 != str_casecmp(ecall->userid_peer,
				     userid_sender)) {

			warning("ecall: conn_handler:"
			     " peer UserID already set to `%s'"
			     " - dropping message with `%s'\n",
			     ecall->userid_peer, userid_sender);
		}
	}
	else {
		err = str_dup(&ecall->userid_peer, userid_sender);
		if (err)
			goto error;
	}

	if (!ecall->mf) {

		err = alloc_mediaflow(ecall);
		if (err)
			goto error;
	}

	mediaflow_set_remote_userclientid(ecall->mf, userid_sender, clientid_sender);

	err = mediaflow_handle_offer(ecall->mf, sdp);
	if (err) {
		warning("ecall[%s]: handle_offer error (%m)\n",
			ecall->userid_self, err);
		goto error;
	}

	if (!mediaflow_has_data(ecall->mf)) {
		warning("ecall: conn_handler: remote peer does not"
			" support datachannels (%s|%s)\n",
			userid_sender, clientid_sender);
		return;
	}

	/* check for remote properties */
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

	if (ecall->connh) {
		ecall->connh(ecall, msg_time, userid_sender,
			     video_active, ecall->arg);
	}

	ecall->ts_started = tmr_jiffies();
	ecall->call_setup_time = -1;

	return;

 error:
	ecall_close(ecall, err, msg_time);
}


static int generate_or_gather_answer(struct ecall *ecall, struct econn *econn)
{
	if (mediaflow_is_gathered(ecall->mf)) {
		return generate_answer(ecall, econn);
	}
	else {
		async_sdp_name(ecall->sdp.async);
		if (ecall->sdp.async != ASYNC_NONE
		    && ecall->sdp.async != ASYNC_OFFER) {
			warning("ecall: answer: async sdp (%s)\n",
				async_sdp_name(ecall->sdp.async));
			return EPROTO;
		}

		ecall->sdp.async = ASYNC_ANSWER;

		ecall->econn_pending = econn;
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
	const char *vr;
	bool video_active = false;
	bool strm_chg;
	bool muted;

	assert(ECALL_MAGIC == ecall->magic);

	ecall->update = true;

	strm_chg = strstr(sdp, "x-streamchange") != NULL;

	muted = msystem_get_muted();
	
	if (ecall->mf && strm_chg) {
		info("ecall(%p): update: x-streamchange\n", ecall);
		mediaflow_stop_media(ecall->mf);
		mediaflow_sdpstate_reset(ecall->mf);
		mediaflow_reset_media(ecall->mf);
	}
	else {
		if (ecall->conf_part)
			ecall->conf_part->data = NULL;
		ecall->mf = mem_deref(ecall->mf);
		err = alloc_mediaflow(ecall);
		if (err)
			goto error;

		if (ecall->conf_part)
			ecall->conf_part->data = ecall->mf;

		mediaflow_set_remote_userclientid(ecall->mf,
				      userid_sender,
				      econn_clientid_remote(econn));
	}

	msystem_set_muted(muted);

	err = mediaflow_handle_offer(ecall->mf, sdp);
	if (err) {
		warning("ecall(%p): [%s]: handle_offer error (%m)\n",
			ecall, ecall->userid_self, err);
		goto error;
	}

	if (!mediaflow_has_data(ecall->mf)) {
		warning("ecall: update_req_handler: remote peer does not"
			" support datachannels (%s|%s)\n",
			userid_sender, clientid_sender);
		return;
	}

	ecall->props_remote = mem_deref(ecall->props_remote);
	ecall->props_remote = mem_ref(props);

	vr = ecall_props_get_remote(ecall, "videosend");
	if (vr) {
		if (strcmp(vr, "true") == 0) {
			video_active = true;
		}
	}

	ecall->sdp.async = ASYNC_NONE;
	err = generate_or_gather_answer(ecall, econn);
	if (err) {
		warning("ecall(%p): generate_or_gather_answer failed (%m)\n",
			ecall, err);
		goto error;
	}

	if (strm_chg) {
		mediaflow_start_media(ecall->mf);
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

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): [ %s.%s ] ecall: answered (reset=%d, sdp=%p)\n",
	     ecall,
	     ecall->userid_self, ecall->clientid_self, reset, sdp);

	ecall->audio_setup_time = -1;
	ecall->call_estab_time = -1;
	ecall->ts_answered = tmr_jiffies();

	if (reset) {

		mediaflow_sdpstate_reset(ecall->mf);

		mediaflow_set_remote_userclientid(ecall->mf,
						  econn_userid_remote(conn),
						  econn_clientid_remote(conn));

		err = mediaflow_handle_offer(ecall->mf, sdp);
		if (err) {
			warning("ecall[%s]: handle_offer error (%m)\n",
				ecall->userid_self, err);
			goto error;
		}

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

	mediaflow_set_remote_userclientid(ecall->mf, econn_userid_remote(conn),
		econn_clientid_remote(conn));

	err = mediaflow_handle_answer(ecall->mf, sdp);
	if (err) {
		warning("ecall: answer_handler: handle_answer failed"
			" (%m)\n", err);
		goto error;
	}

	if (!mediaflow_has_data(ecall->mf)) {
		warning("ecall: answer_handler: remote peer does not"
			" support datachannel\n");
		err = EPROTO;
		goto error;
	}

	err = mediaflow_start_ice(ecall->mf);
	if (err) {
		warning("ecall: mediaflow_start_ice failed (%m)\n", err);
		goto error;
	}

	ecall->props_remote = mem_ref(props);

	ecall->answered = true;

	if (ecall->answerh) {
		ecall->answerh(ecall->arg);
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
	int err = 0;

	assert(ECALL_MAGIC == ecall->magic);

	if (!ecall->update) {
		warning("ecall(%p): received UPDATE-resp with no update\n",
			ecall);
		return;
	}

	info("ecall(%p): [%s.%s] UPDATE-resp (sdp=%p)\n",
	     ecall->userid_self, ecall->clientid_self, sdp);

	err = mediaflow_handle_answer(ecall->mf, sdp);
	if (err) {
		warning("ecall: answer_handler: handle_answer failed"
			" (%m)\n", err);
		goto error;
	}

	if (!mediaflow_has_data(ecall->mf)) {
		warning("ecall: answer_handler: remote peer does not"
			" support datachannel\n");
		err = EPROTO;
		goto error;
	}

	err = mediaflow_start_ice(ecall->mf);
	if (err) {
		warning("ecall: mediaflow_start_ice failed (%m)\n", err);
		goto error;
	}

	ecall->props_remote = mem_deref(ecall->props_remote);
	ecall->props_remote = mem_ref(props);

	if (dce_is_chan_open(ecall->dce_ch))
		econn_set_datachan_established(econn);

	return;

 error:
	/* if error, close the call */
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void econn_alert_handler(struct econn *econn, uint32_t level,
				const char *descr, void *arg)
{
	struct ecall *ecall = arg;

	if (ecall->alerth) {
		ecall->alerth(level, descr, ecall->arg);
	}
	else {
		warning("ecall: received level %u ALERT (%s)\n",
			level, descr);
	}
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

	ecall_close(ecall, err, msg_time);
}


static void ecall_destructor(void *data)
{
	struct ecall *ecall = data;
	size_t i;

#if 1
	info("--------------------------------------\n");
	info("%H\n", ecall_debug, ecall);
	info("--------------------------------------\n");
#endif

	list_unlink(&ecall->le);

	tmr_cancel(&ecall->dc_tmr);
	tmr_cancel(&ecall->media_start_tmr);
	tmr_cancel(&ecall->update_tmr);

	if (ecall->conf_part) {
		ecall->conf_part->data = NULL;
		mem_deref(ecall->conf_part);
	}
	mem_deref(ecall->mf);
	mem_deref(ecall->userid_self);
	mem_deref(ecall->userid_peer);
	mem_deref(ecall->clientid_self);
	mem_deref(ecall->clientid_peer);
	mem_deref(ecall->convid);
	mem_deref(ecall->msys);
	mem_deref(ecall->usrd);

	for (i=0; i<ARRAY_SIZE(ecall->turnv) ; i++) {
		mem_deref(ecall->turnv[i].user);
		mem_deref(ecall->turnv[i].pass);
	}

	mem_deref(ecall->props_remote);
	mem_deref(ecall->props_local);

	mem_deref(ecall->econn);

	list_flush(&ecall->tracel);

	/* last thing to do */
	ecall->magic = 0;
}


static int send_handler(struct econn *conn,
			struct econn_message *msg, void *arg)
{
	struct ecall *ecall = arg;
	char *str = NULL;
	int err;

	assert(ECALL_MAGIC == ecall->magic);

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
		ecall_trace(ecall, msg, true, ECONN_TRANSP_BACKEND,
			    "SE %H\n", econn_message_brief, msg);

		err = ecall->sendh(ecall->userid_self, msg, ecall->arg);
		break;

	case ECONN_HANGUP:
	case ECONN_PROPSYNC:
		err = econn_message_encode(&str, msg);
		if (err) {
			warning("ecall: send_handler: econn_message_encode"
				" failed (%m)\n", err);
			return err;
		}

		ecall_trace(ecall, msg, true, ECONN_TRANSP_DIRECT,
			    "DataChan %H\n",
			    econn_message_brief, msg);

		err = dce_send(ecall->dce, ecall->dce_ch, str, str_len(str));
		mem_deref(str);
		break;

	default:
		warning("ecall: send_handler: message not supported (%s)\n",
			econn_msg_name(msg->msg_type));
		err = EPROTO;
		break;
	}

	return err;
}


int ecall_alloc(struct ecall **ecallp, struct list *ecalls,
		const struct ecall_conf *conf,
		struct msystem *msys,
		const char *convid,
		const char *userid_self,
		const char *clientid,
		ecall_conn_h *connh,
		ecall_answer_h *answerh,
		ecall_media_estab_h *media_estabh,
		ecall_audio_estab_h *audio_estabh,
		ecall_datachan_estab_h *datachan_estabh,
		ecall_propsync_h *propsynch,
		ecall_video_state_change_h *vstateh,
		ecall_audio_cbr_change_h *audiocbrh,
		ecall_close_h *closeh,
		ecall_transp_send_h *sendh, void *arg)
{
	struct ecall *ecall;
	int err = 0;

	if (!msys || !str_isset(convid))
		return EINVAL;

	ecall = mem_zalloc(sizeof(*ecall), ecall_destructor);
	if (!ecall)
		return ENOMEM;

	ecall->magic = ECALL_MAGIC;
	ecall->conf = conf ? *conf : default_conf;

	/* Add some properties */
	err = econn_props_alloc(&ecall->props_local, NULL);
	if (err)
		goto out;

	err = econn_props_add(ecall->props_local, "videosend", "false");
	if (err)
		goto out;

	err = econn_props_add(ecall->props_local, "audiocbr", "false");
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

	ecall->connh = connh;
	ecall->answerh = answerh;
	ecall->media_estabh = media_estabh;
	ecall->audio_estabh = audio_estabh;
	ecall->datachan_estabh = datachan_estabh;
	ecall->propsynch = propsynch;
	ecall->vstateh = vstateh;
	ecall->audiocbrh = audiocbrh;
	ecall->closeh = closeh;
	ecall->sendh = sendh;
	ecall->arg = arg;

	list_append(ecalls, &ecall->le, ecall);

	ecall->ts_start = tmr_jiffies();

 out:
	if (err)
		mem_deref(ecall);
	else if (ecallp)
		*ecallp = ecall;

	return err;
}


int ecall_add_turnserver(struct ecall *ecall, const struct sa *srv,
			 const char *user, const char *pass)
{
	struct turn_server *turn;
	int err = 0;

	if (!ecall || !srv || !user || !pass)
		return EINVAL;

	info("ecall(%p): add turnserver (addr=%J user=%s)\n", ecall, srv, user);

	if (ecall->turnc >= ARRAY_SIZE(ecall->turnv)) {
		warning("ecall: maximum %zu turn servers\n",
			ARRAY_SIZE(ecall->turnv));
		return EOVERFLOW;
	}

	turn = &ecall->turnv[ecall->turnc];

	turn->srv = *srv;
	err |= str_dup(&turn->user, user);
	err |= str_dup(&turn->pass, pass);
	if (err)
		return err;

	++ecall->turnc;

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

	err = mediaflow_generate_offer(ecall->mf, sdp, sdp_len);
	if (err) {
		warning("ecall(%p): offer_and_connect:"
			" mf=%p generate_offer failed (%m)\n",
			ecall, ecall->mf, err);
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

	if (mediaflow_is_gathered(ecall->mf)) {

		err = offer_and_connect(ecall);
		if (err)
			return err;
	}
	else {
		info("ecall(%p): generate_offer: mf=%p: not gathered "
		     ".. wait ..\n", ecall, ecall->mf);

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

	err = mediaflow_generate_answer(ecall->mf, sdp, sdp_len);
	if (err) {
		warning("ecall: generate answer failed (%m)\n", err);
		goto out;
	}

	err = mediaflow_start_ice(ecall->mf);
	if (err) {
		warning("ecall: mediaflow_start_ice failed (%m)\n", err);
		goto out;
	}

	if (ecall->update) {
		err = econn_update_resp(econn, sdp, ecall->props_local);

		if (dce_is_chan_open(ecall->dce_ch))
			econn_set_datachan_established(econn);
	}
	else {
		err = econn_answer(econn, sdp, ecall->props_local);
	}
	if (err)
		goto out;

 out:
	mem_deref(sdp);
	return err;
}


static void media_start_timeout_handler(void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	warning("ecall(%p): media_start timeout \n", ecall);

	if (ecall->econn) {
		econn_set_error(ecall->econn, EIO);

		ecall_end(ecall);
	}
	else {
		/* notify upwards */
		ecall_close(ecall, EIO, ECONN_MESSAGE_TIME_UNKNOWN);
	}
}


static void mf_estab_handler(const char *crypto, const char *codec,
			     void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): mediaflow established (crypto=%s)\n",
	     ecall, crypto);

	/* Save the negotiated crypto */
	ecall->crypto = mediaflow_crypto(ecall->mf);

	if (ecall->call_estab_time < 0 && ecall->ts_answered) {
		ecall->call_estab_time = tmr_jiffies() - ecall->ts_answered;
	}

	if (ecall->media_estabh) {

		/* Start a timer to check that we do start Audio Later */
		tmr_start(&ecall->media_start_tmr, TIMEOUT_MEDIA_START,
			  media_start_timeout_handler, ecall);

		ecall->media_estabh(ecall, ecall->update, ecall->arg);
	}
	else {
		int err = ecall_media_start(ecall);
		if (err) {
			ecall_end(ecall);
		}
	}
}


static void mf_close_handler(int err, void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): mediaflow closed (%m)\n", ecall, err);

	info("ecall(%p): mf_close_handler: mediaflow failed. "
		"user=%s err='%m'\n", ecall, ecall->userid_self, err);

	if (ecall->econn) {
		econn_set_error(ecall->econn, err);

		ecall_end(ecall);
	}
	else {
		/* notify upwards */
		ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
	}
}


static void mf_gather_handler(void *arg)
{
	struct ecall *ecall = arg;
	int err;

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): mf_gather_handler complete "
	     "(async=%s)\n",
	     ecall,
	     async_sdp_name(ecall->sdp.async));

	if (ECONN_TERMINATING == econn_current_state(ecall->econn))
		return;

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


static void channel_estab_handler(void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): data channel established\n", ecall);

	econn_set_datachan_established(ecall->econn);

	/* Update the cbr status */
	if (mediaflow_get_audio_cbr(ecall->mf, true)){
		err = econn_props_update(ecall->props_local,
					 "audiocbr", "true");
		if (err) {
			warning("ecall: econn_props_update(audiocbr)",
			        " failed (%m)\n", err);
			goto error;
		}
	}

	/* sync the properties to the remote peer */
	if (!ecall->devpair && econn_can_send_propsync(ecall->econn)) {
		err = econn_send_propsync(ecall->econn, false,
					  ecall->props_local);
		if (err) {
			warning("ecall: channel_estab: econn_send_propsync"
				" failed (%m)\n", err);
			goto error;
		}
	}

	if (ecall->datachan_estabh)
		ecall->datachan_estabh(ecall, ecall->update, ecall->arg);

	return;

 error:
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void data_estab_handler(void *arg)
{
	struct ecall *ecall = arg;
	int err = 0;

	assert(ECALL_MAGIC == ecall->magic);

	info("ecall(%p): [ %s ] datachan estab.\n", ecall, ecall->userid_self);

	if (mediaflow_is_sdp_offerer(ecall->mf)) {
		err = dce_open_chan(ecall->dce, ecall->dce_ch);
		if (err) {
			warning("ecall: dce_open_chan failed (%m)\n", err);
			goto error;
		}
	}
	if (ecall->usrd) {
		ecall_add_user_data_channel(ecall,
				    mediaflow_is_sdp_offerer(ecall->mf));
	}

	return;

 error:
	ecall_close(ecall, err, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void propsync_handler(struct ecall *ecall)
{
	bool state = false;
	bool vstate_changed = false;
	const char *vr, *cr, *cl;
	bool local_cbr, remote_cbr, cbr_enabled;

	if (!ecall) {
		warning("ecall(%p): propsyc_handler ecall is NULL, "
			"ignoring props\n", ecall);
		return;
	}

	info("ecall(%p): propsync_handler, current recv_state %s \n",
	     ecall, ecall->video.recv_state ? "true" : "false");

	vr = ecall_props_get_remote(ecall, "videosend");
	if (vr) {
		vstate_changed = true;
		if (strcmp(vr, "true") == 0) {
			state = true;
		}
	}

	vr = ecall_props_get_remote(ecall, "screensend");
	if (vr) {
		vstate_changed = true;
		if (strcmp(vr, "true") == 0) {
			state = true;
		}
	}
    
	if (vstate_changed && state != ecall->video.recv_state) {
		info("ecall(%p): propsync_handler updating recv_state "
		     "%s -> %s\n",
		     ecall,
		     ecall->video.recv_state ? "true" : "false",
		     state ? "true" : "false");
		if (ecall->vstateh) {
			ecall->vstateh(ecall, ecall->userid_peer, state, ecall->arg);
			ecall->video.recv_state = state;
		}
	}
    
	cl = ecall_props_get_local(ecall, "audiocbr");
	local_cbr = cl && (0 == strcmp(cl, "true"));

	cr = ecall_props_get_remote(ecall, "audiocbr");
	remote_cbr = cr && (0 == strcmp(cr, "true"));
		
	cbr_enabled = local_cbr && remote_cbr ? 1 : 0;

	if (cbr_enabled != ecall->audio.cbr_state) {
		info("ecall(%p): acbrh(%p) lcbr=%s rcbr=%s cbr=%d\n", ecall, ecall->audiocbrh,
			local_cbr ? "true" : "false", remote_cbr ? "true" : "false",
			cbr_enabled);
		if (ecall->audiocbrh) {
			ecall->audiocbrh(ecall, ecall->userid_peer, cbr_enabled, ecall->arg);
			ecall->audio.cbr_state = cbr_enabled;
		}
	}
}


static void data_channel_handler(int chid, uint8_t *data,
				 size_t len, void *arg)
{
	struct ecall *ecall = arg;
	struct econn_message *msg = 0;
	int err;

	assert(ECALL_MAGIC == ecall->magic);

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

	ecall_trace(ecall, msg, false, ECONN_TRANSP_DIRECT,
		    "DataChan %H\n", econn_message_brief, msg);

	info("ecall(%p): channel: [%s] receive message type '%s'\n", ecall,
	     econn_state_name(econn_current_state(ecall->econn)),
	     econn_msg_name(msg->msg_type));

	if (msg->msg_type == ECONN_PROPSYNC) {

		if (!ecall->devpair &&
		    econn_message_isrequest(msg) &&
		    econn_can_send_propsync(ecall->econn)) {

			err = econn_send_propsync(ecall->econn, true,
						  ecall->props_local);
			if (err) {
				warning("ecall: data_recv:"
					" [open=%d] econn_send_propsync"
					" failed (%m)\n",
					dce_is_chan_open(ecall->dce_ch),
					err);
				goto out;
			}
		}

		if (msg->u.propsync.props) {
			mem_deref(ecall->props_remote);
			ecall->props_remote = mem_ref(msg->u.propsync.props);
		}

		propsync_handler(ecall);

		if (ecall->propsynch)
			ecall->propsynch(ecall->arg);

		goto out;
	}

	/* forward message to ECONN */
	econn_recv_message(ecall->econn, econn_userid_remote(ecall->econn),
			   econn_clientid_remote(ecall->econn), msg);

 out:
	mem_deref(msg);
}


static void data_channel_open_handler(int chid, const char *label,
                                       const char *protocol, void *arg)
{
	struct ecall *ecall = arg;

	info("ecall(%p): data channel opened with label %s \n", ecall, label);

	channel_estab_handler(ecall);
}


static void channel_closed_handler(void *arg)
{
	struct ecall *ecall = arg;

	ecall_close(ecall, EDATACHANNEL, ECONN_MESSAGE_TIME_UNKNOWN);
}


static void data_channel_closed_handler(int chid, const char *label,
                                        const char *protocol, void *arg)
{
	struct ecall *ecall = arg;

	warning("ecall(%p): data channel closed with label %s\n",
		ecall, label);

	tmr_start(&ecall->dc_tmr, TIMEOUT_DC_CLOSE,
		  channel_closed_handler, ecall);
}


static void rtp_start_handler(bool started, bool video_started, void *arg)
{
	struct ecall *ecall = arg;

	assert(ECALL_MAGIC == ecall->magic);

	if (started) {
		if (ecall->audio_estabh){
			ecall->audio_estabh(ecall, ecall->update, ecall->arg);
		}

		if (ecall->audio_setup_time < 0 && ecall->ts_answered){
			uint64_t now = tmr_jiffies();
			ecall->audio_setup_time = now - ecall->ts_answered;
			ecall->call_setup_time = now - ecall->ts_started;
		}
	}
}


static bool interface_handler(const char *ifname, const struct sa *sa,
			      void *arg)
{
	struct ecall *ecall = arg;
	int err;

	assert(ECALL_MAGIC == ecall->magic);

	/* Skip loopback and link-local addresses */
	if (sa_is_loopback(sa) || sa_is_linklocal(sa))
		return false;

	info("ecall(%p): adding local host interface: %s:%j\n",
	     ecall, ifname, sa);

	err = mediaflow_add_local_host_candidate(ecall->mf, ifname, sa);
	if (err) {
		warning("ecall: failed to add local host candidate"
			" %s:%j (%m)\n", ifname, sa, err);
		return false;
	}

	return false;
}


static int alloc_mediaflow(struct ecall *ecall)
{
	struct sa laddr;
	char tag[64] = "";
	size_t i;
	bool enable_kase = msystem_have_kase(ecall->msys);
	enum media_crypto cryptos = 0;
	int err;

	debug("ecall: alloc_mediaflow: ecall=%p\n", ecall);

	/*
	 * NOTE: v4 has presedence over v6 for now
	 */
	if (0 == net_default_source_addr_get(AF_INET, &laddr)) {
		info("ecall(%p): alloc_mediaflow:: local IPv4 addr %j\n",
		     ecall, &laddr);
	}
	else if (0 == net_default_source_addr_get(AF_INET6, &laddr)) {
		info("ecall(%p): alloc_mediaflow: local IPv6 addr %j\n",
		     ecall, &laddr);
	}
	else if (msystem_get_loopback(ecall->msys)) {

		sa_set_str(&laddr, "127.0.0.1", 0);
	}
	else {
		warning("ecall: alloc_mediaflow: no local addresses\n");
		err = EAFNOSUPPORT;
		goto out;
	}

	cryptos |= CRYPTO_DTLS_SRTP;

	if (enable_kase)
		cryptos |= CRYPTO_KASE;

	assert(ecall->mf == NULL);
	err = mediaflow_alloc(&ecall->mf, ecall->clientid_self,
			      msystem_dtls(ecall->msys),
			      msystem_aucodecl(ecall->msys),
			      &laddr,
			      cryptos,
			      mf_estab_handler,
			      mf_close_handler,
			      ecall);
	if (err) {
		warning("ecall(%p): failed to alloc mediaflow (%m)\n",
			ecall, err);
		goto out;
	}

	if (enable_kase) {
		mediaflow_set_fallback_crypto(ecall->mf, CRYPTO_KASE);
	}

	re_snprintf(tag, sizeof(tag), "%s.%s",
		    ecall->userid_self, ecall->clientid_self);
	mediaflow_set_tag(ecall->mf, tag);

	mediaflow_set_rtpstate_handler(ecall->mf, rtp_start_handler);

	if (msystem_get_privacy(ecall->msys)) {
		info("ecall(%p): alloc_mediaflow: enable mediaflow privacy\n",
		     ecall);
		mediaflow_enable_privacy(ecall->mf, true);
	}

	mediaflow_set_gather_handler(ecall->mf, mf_gather_handler);

	mediaflow_enable_group_mode(ecall->mf, ecall->group_mode);
	/* In devpair mode, we want to disable audio, and not add video */
	if (ecall->devpair)
		mediaflow_disable_audio(ecall->mf);
	else {
		err = mediaflow_add_video(ecall->mf,
					  msystem_vidcodecl(ecall->msys));
		if (err) {
			warning("ecall(%p): mediaflow add video failed (%m)\n",
				ecall, err);
			goto out;
		}
	}

	ecall->dce = mediaflow_get_dce(ecall->mf);
	if (!ecall->dce){
		warning("ecall(%p) no dce object available \n", ecall);
		goto out;
	}

	err = dce_channel_alloc(&ecall->dce_ch,
				ecall->dce,
				"calling-3.0",
				"",
				data_estab_handler,
				data_channel_open_handler,
				data_channel_closed_handler,
				data_channel_handler,
				ecall);

	if (err) {
		warning("ecall(%p): mediaflow dce_channel_alloc failed (%m)\n",
				ecall, err);
		goto out;
	}

	/* DataChannel is mandatory in Calling 3.0 */
	err = mediaflow_add_data(ecall->mf);
	if (err) {
		warning("ecall(%p): mediaflow add data failed (%m)\n",
			ecall, err);
		goto out;
	}

	/* populate all network interfaces */
	net_if_apply(interface_handler, ecall);

	for (i=0; i<ecall->turnc; i++) {

		struct turn_server *turn = &ecall->turnv[i];

		err = mediaflow_gather_turn(ecall->mf, &turn->srv,
					    turn->user,
					    turn->pass);
		if (err) {
			warning("ecall: mediaflow_gather_turn failed (%m)\n",
				err);
			goto out;
		}
	}

 out:
	if (err) {
		if (ecall->conf_part)
			ecall->conf_part->data = NULL;
		ecall->mf = mem_deref(ecall->mf);
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
			  &ecall->transp,
			  econn_conn_handler,
			  econn_answer_handler,
			  econn_update_req_handler,
			  econn_update_resp_handler,
			  econn_alert_handler,
			  econn_close_handler,
			  ecall);
	if (err) {
		warning("ecall_setup: econn_alloc failed: %m\n", err);
		return err;
	}

	return err;
}


int ecall_start(struct ecall *ecall, bool audio_cbr, void *extcodec_arg)
{
	int err;

	info("ecall(%p): start\n", ecall);

	if (!ecall)
		return EINVAL;

	if (ecall->econn) {
		warning("ecall: start: already in progress (econn=%s)\n",
			econn_state_name(econn_current_state(ecall->econn)));
		return EALREADY;
	}

#if 0
	if (ecall->turnc == 0) {
		warning("ecall: start: no TURN servers -- cannot start\n");
		return EINTR;
	}
#endif

	err = ecall_create_econn(ecall);
	if (err) {
		warning("ecall: start: create_econn failed: %m\n", err);
		return err;
	}

	econn_set_state(ecall_get_econn(ecall), ECONN_PENDING_OUTGOING);

	err = alloc_mediaflow(ecall);
	if (err) {
		warning("ecall: start: alloc_mediaflow failed: %m\n", err);
		goto out;
	}

	mediaflow_set_audio_cbr(ecall->mf, audio_cbr);

	if (extcodec_arg)
		mediaflow_set_extcodec(ecall->mf, extcodec_arg);
	
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


int ecall_answer(struct ecall *ecall, bool audio_cbr, void *extcodec_arg)
{
	int err = 0;

	if (!ecall)
		return EINVAL;

	info("ecall(%p): answer on pending econn %p\n", ecall, ecall->econn);

	if (!ecall->econn) {
		warning("ecall: answer: econn does not exist!\n");
		return ENOENT;
	}

	if (ECONN_PENDING_INCOMING != econn_current_state(ecall->econn)) {
		info("ecall(%p): answer: invalid state (%s)\n", ecall,
		     econn_state_name(econn_current_state(ecall->econn)));
		return EPROTO;
	}

	if (!ecall->mf) {
		warning("ecall: answer: no mediaflow\n");
		return EPROTO;
	}

	mediaflow_set_audio_cbr(ecall->mf, audio_cbr);

	if (extcodec_arg)
		mediaflow_set_extcodec(ecall->mf, extcodec_arg);

	err = generate_or_gather_answer(ecall, ecall->econn);
	if (err) {
		warning("ecall: answer: gailed to gather_or_answer\n");
		goto out;
	}

	ecall->answered = true;
	ecall->audio_setup_time = -1;
	ecall->call_estab_time = -1;
	ecall->ts_answered = tmr_jiffies();

 out:
	return err;
}


void ecall_msg_recv(struct ecall *ecall,
		    uint32_t curr_time, /* in seconds */
		    uint32_t msg_time, /* in seconds */
		    const char *userid_sender,
		    const char *clientid_sender,
		    struct econn_message *msg)
{
	int err;

	info("ecall(%p): msg_recv: %H\n", ecall, econn_message_brief, msg);

	if (!ecall || !userid_sender || !clientid_sender || !msg)
		return;

	ecall_trace(ecall, msg, false, ECONN_TRANSP_BACKEND,
		    "SE %H\n", econn_message_brief, msg);

	/* Check that message was received via correct transport */
	if (ECONN_TRANSP_BACKEND != econn_transp_resolve(msg->msg_type)) {
		warning("ecall: recv: wrong transport for type %s\n",
			econn_msg_name(msg->msg_type));
	}

	/* Messages from the same userid.
	 */
	if (0 == str_casecmp(ecall->userid_self, userid_sender)) {

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
			     econn_msg_name(msg->msg_type), userid_sender);
		}

		goto out;
	}

	/* create a new ECONN */
	if (!ecall->econn &&
	    econn_is_creator(ecall->userid_self, userid_sender, msg)) {

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
	return;
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
	if (!ecall)
		return;

	info("ecall(%p): [self=%s] end\n", ecall, ecall->userid_self);

	/* disconnect all connections */
	econn_end(ecall->econn);

	mediaflow_stop_media(ecall->mf);
}


struct mediaflow *ecall_mediaflow(const struct ecall *ecall)
{
	return ecall ? ecall->mf : NULL;
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


int ecall_set_video_send_active(struct ecall *ecall, bool active)
{
	int err = 0;

	if (!ecall)
		return EINVAL;

	info("ecall(%p): set_video_send_active %s econn %p\n",
	     ecall,
	     active ? "true" : "false",
	     ecall->econn);

	err = econn_props_update(ecall->props_local, "videosend",
				 active ? "true" : "false");
	if (err) {
		warning("ecall: econn_props_update(videosend)",
			" failed (%m)\n",
			err);
		return err;
	}

	/* if the call has video, update mediaflow */
	if (mediaflow_has_video(ecall->mf)) {
		err = mediaflow_set_video_send_active(ecall->mf, active);
		if (err) {
			warning("ecall: set_video_send_active:"
				" failed to set mf->active (%m)\n", err);
			goto out;
		}
	}

	/* sync the properties to the remote peer */
	if (!ecall->devpair && econn_can_send_propsync(ecall->econn)) {
		info("ecall(%p): setting videosend prop to %s\n", ecall,
		     active ? "true" : "false");

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


int ecall_set_group_mode(struct ecall *ecall, bool active)
{
	int err = 0;

	if (!ecall)
		return EINVAL;

	info("ecall(%p): set_group_mode %s econn %p\n",
	     ecall,
	     active ? "true" : "false",
	     ecall->econn);

	ecall->group_mode = active;

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

	if (!ecall->mf)
		return false;

	return mediaflow_has_video(ecall->mf);
}


int ecall_media_start(struct ecall *ecall)
{
	int err;
	const char *is_video;

	debug("ecall: media start ecall=%p\n", ecall);

	if (!ecall)
		return EINVAL;

	if (!mediaflow_is_ready(ecall->mf)){
		info("ecall(%p): mediaflow not ready cannot start media \n",
		     ecall);
		return 0;
	}

	is_video = ecall_props_get_local(ecall, "videosend");
	if (is_video && strcmp(is_video, "true") == 0) {
		mediaflow_set_video_send_active(ecall->mf, true);
	}
	err = mediaflow_start_media(ecall->mf);
	if (err) {
		warning("ecall: mediaflow start media failed (%m)\n", err);
		econn_set_error(ecall->econn, err);
		goto out;
	}
	info("ecall(%p): media started on ecall:%p\n", ecall, ecall);

	tmr_cancel(&ecall->media_start_tmr);

 out:
	return err;
}


void ecall_media_stop(struct ecall *ecall)
{
	debug("ecall: media stop ecall=%p\n", ecall);

	if (!ecall)
		return;

	mediaflow_stop_media(ecall->mf);
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
	int err = 0;

	if (!ecall)
		return 0;

	err |= re_hprintf(pf, "ECALL SUMMARY %p:\n", ecall);
	err |= re_hprintf(pf, "crypto:      %H\n",
			  mediaflow_cryptos_print, ecall->crypto);
	err |= re_hprintf(pf, "convid:      %s\n", ecall->convid);
	err |= re_hprintf(pf, "userid_self: %s\n", ecall->userid_self);
	err |= re_hprintf(pf, "clientid:    %s\n", ecall->clientid_self);
	err |= re_hprintf(pf, "async_sdp:   %s\n",
			  async_sdp_name(ecall->sdp.async));
	err |= re_hprintf(pf, "answered:    %s\n",
			  ecall->answered ? "Yes" : "No");
	err |= re_hprintf(pf, "estab_time:  %d ms\n", ecall->call_estab_time);
	err |= re_hprintf(pf, "audio_setup_time:  %d ms\n",
			  ecall->audio_setup_time);

	if (ecall->mf) {
		err |= re_hprintf(pf, "mediaflow:   %H\n",
				  mediaflow_debug, ecall->mf);
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
		err |= re_hprintf(pf, "userid_peer: %s\n", ecall->userid_peer);
	}

	if (mediaflow_get_dce(ecall->mf)) {
		err |= re_hprintf(pf, "DCE: %H\n",
				  dce_status, mediaflow_get_dce(ecall->mf));
	}

	err = ecall_show_trace(pf, ecall);
	if (err)
		return err;

	return err;
}


void ecall_set_peer_userid(struct ecall *ecall, const char *userid)
{
	if (!ecall)
		return;

	ecall->userid_peer = mem_deref(ecall->userid_peer);
	if (userid)
		str_dup(&ecall->userid_peer, userid);
}


void ecall_set_peer_clientid(struct ecall *ecall, const char *clientid)
{
	if (!ecall)
		return;

	ecall->clientid_peer = mem_deref(ecall->clientid_peer);
	if (clientid)
		str_dup(&ecall->clientid_peer, clientid);
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


struct ecall *ecall_find_userid(const struct list *ecalls,
				const char *userid)
{
	struct le *le;

	for (le = list_head(ecalls); le; le = le->next) {
		struct ecall *ecall = le->data;

		if (0 == str_casecmp(userid, ecall->userid_peer))
			return ecall;
	}

	return NULL;
}


int ecall_restart(struct ecall *ecall)
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
			econn_state_name(state));
		return EPROTO;
	}

	ecall->update = true;
	tmr_cancel(&ecall->dc_tmr);
	ecall->conf_part = mem_deref(ecall->conf_part);
	muted = msystem_get_muted();
	ecall->mf = mem_deref(ecall->mf);
	err = alloc_mediaflow(ecall);
	msystem_set_muted(muted);
	if (err) {
		warning("ecall: re-start: alloc_mediaflow failed: %m\n", err);
		goto out;
	}
	if (ecall->conf_part)
		ecall->conf_part->data = ecall->mf;

	mediaflow_set_remote_userclientid(ecall->mf, econn_userid_remote(ecall->econn),
				      econn_clientid_remote(ecall->econn));

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
	if (ecall->conf_part)
		ecall->conf_part->data = ecall->mf;
}


void ecall_set_alert_handler(struct ecall *ecall, ecall_alert_h *alerth)
{
	if (!ecall)
		return;

	ecall->alerth = alerth;
}


int ecall_send_alert(struct ecall *ecall, uint32_t level, const char *descr)
{
	if (!ecall)
		return EINVAL;

	return econn_send_alert(ecall->econn, level, descr);
}
