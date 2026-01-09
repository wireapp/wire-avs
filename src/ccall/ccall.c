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

//#include <openssl/ssl.h>
#include <assert.h>
#include <sodium.h>

#include <re.h>
#include <avs.h>
#include "ccall.h"
#include "avs_wcall.h"
#include "avs_audio_level.h"

#if (defined ANDROID || defined __EMSCRIPTEN__ || defined LINUX)

#include <endian.h>
#define htonll(x) htobe64(x)

#endif

#define CCALL_CBR_ALWAYS_ON 1
#define RESOLUTION_DEGRADE 0

#define SFT_STATUS_NETWORK_ERROR 1000

static void ccall_connect_timeout(void *arg);
static void ccall_stop_ringing_timeout(void *arg);
static void ccall_ongoing_call_timeout(void *arg);
static void ccall_sync_vstate_timeout(void *arg);
static void ccall_send_check_timeout(void *arg);
static void ccall_decrypt_check_timeout(void *arg);
static void ccall_keepalive_timeout(void *arg);
static void ccall_end_with_err(struct ccall *ccall, int err);
static void ccall_alone_timeout(void *arg);
static int  ccall_send_msg(struct ccall *ccall,
			   enum econn_msg type,
			   bool resp,
			   struct list *targets,
			   bool transient);
static int ccall_send_msg_sft(struct ccall *ccall,
			      const char *sft_url,
			      struct econn_message *msg);
static int  ccall_send_conf_conn(struct ccall *ccall,
				 const char *sft_url,
				 const char *sft_username,
				 const char *sft_credential,
				 bool update);
static int ccall_send_keys(struct ccall *ccall,
			   bool send_to_all);
static int ccall_request_keys(struct ccall *ccall);
static int ccall_sync_media_keys(struct ccall *ccall);
static void ccall_stop_others_ringing(struct ccall *ccall);
static struct zapi_ice_server* ccall_get_sft_info(struct ccall *ccall,
						  const char *sft_url);
static struct zapi_ice_server* ccall_get_sft_info_at_index(struct ccall *ccall,
							   int ix);
static void ccall_update_active_counts(struct ccall *ccall);

static int alloc_message(struct econn_message **msgp,
			 struct ccall *ccall,
			 enum econn_msg type,
			 bool resp,
			 const char *src_userid,
			 const char *src_clientid,
			 const char *dest_userid,
			 const char *dest_clientid,
			 bool transient);
static int  ccall_req_cfg_join(struct ccall *ccall,
			       enum icall_call_type call_type,
			       bool audio_cbr,
			       bool retry_attempt,
			       bool is_outgoing);

static int copy_sft(char **pdst, const char* src)
{
	char *dst;
	size_t slen;

	slen = strlen(src);

	dst = mem_zalloc(slen + 2, NULL);
	if (!dst) {
		return ENOMEM;
	}
	strncpy(dst, src, slen);
	if (dst[slen - 1] != '/') {
		strncat(dst, "/", 1);
	}

	*pdst = dst;

	return 0;
}

static void destructor(void *arg)
{
	struct ccall *ccall = arg;

	if (ccall->je) {
		ccall->je = mem_deref(ccall->je);
	}

	tmr_cancel(&ccall->tmr_connect);
	tmr_cancel(&ccall->tmr_call);
	tmr_cancel(&ccall->tmr_send_check);
	tmr_cancel(&ccall->tmr_ongoing);
	tmr_cancel(&ccall->tmr_rotate_key);
	tmr_cancel(&ccall->tmr_rotate_mls);
	tmr_cancel(&ccall->tmr_ring);
	tmr_cancel(&ccall->tmr_sft_reject);
	tmr_cancel(&ccall->tmr_vstate);
	tmr_cancel(&ccall->tmr_decrypt_check);
	tmr_cancel(&ccall->tmr_keepalive);
	tmr_cancel(&ccall->tmr_alone);

	mem_deref(ccall->sft_url);
	mem_deref(ccall->primary_sft_url);
 	mem_deref(ccall->sft_tuple);
	mem_deref(ccall->convid_real);
	mem_deref(ccall->convid_hash);
	mem_deref(ccall->turnv);
	mem_deref(ccall->userl);

	mem_deref(ccall->ecall);
	mem_deref(ccall->secret);
	mem_deref(ccall->keystore);

	list_flush(&ccall->sftl);
	list_flush(&ccall->saved_partl);
	list_flush(&ccall->videol);

	mbuf_reset(&ccall->confpart_data);
}

static int ccall_set_secret(struct ccall *ccall,
			    const uint8_t *secret,
			    size_t secretlen)
{
	int err = 0;

	info("ccall(%p): set_secret: %02x%02x%02x%02x len: %u\n",
	     ccall,
	     secret[0], secret[1], secret[2], secret[3],
	     secretlen);
	ccall->secret = mem_deref(ccall->secret);
	ccall->secret_len = 0;

	ccall->secret = mem_zalloc(secretlen, NULL);
	if (!ccall->secret)
		return ENOMEM;

	memcpy(ccall->secret, secret, secretlen);
	ccall->secret_len = secretlen;

	ccall->convid_hash = mem_deref(ccall->convid_hash);
	hash_conv(ccall->secret,
		  secretlen,
		  ccall->convid_real,
		  &ccall->convid_hash);

	userlist_set_secret(ccall->userl, secret, secretlen);

	err = keystore_set_salt(ccall->keystore,
				(const uint8_t*)ccall->convid_hash,
				ccall->convid_hash ? strlen(ccall->convid_hash) : 0);

	if (ccall->ecall)
		ecall_set_sessid(ccall->ecall, ccall->convid_hash);

	return err;
}

int ccall_set_config(struct ccall *ccall, struct config *cfg)
{
	if (!ccall || !cfg)
		return EINVAL;

	ccall->cfg = cfg;

	return 0;
}

const char *ccall_state_name(enum ccall_state state)
{
	switch (state) {
		case CCALL_STATE_NONE:
			return "CCALL_STATE_NONE";

		case CCALL_STATE_IDLE:
			return "CCALL_STATE_IDLE";

		case CCALL_STATE_INCOMING:
			return "CCALL_STATE_INCOMING";

		case CCALL_STATE_CONNSENT:
			return "CCALL_STATE_CONNSENT";

		case CCALL_STATE_SETUPRECV:
			return "CCALL_STATE_SETUPRECV";

		case CCALL_STATE_CONNECTING:
			return "CCALL_STATE_CONNECTING";

		case CCALL_STATE_CONNECTED:
			return "CCALL_STATE_CONNECTED";

		case CCALL_STATE_ACTIVE:
			return "CCALL_STATE_ACTIVE";

		case CCALL_STATE_TERMINATING:
			return "CCALL_STATE_TERMINATING";

		case CCALL_STATE_WAITCONFIG:
			return "CCALL_STATE_WAITCONFIG";

		case CCALL_STATE_WAITCONFIG_OUTGOING:
			return "CCALL_STATE_WAITCONFIG_OUTGOING";

		default:
			return "???";
	}
}

static void set_state(struct ccall* ccall, enum ccall_state state)
{
	enum ccall_state old_state;

	if (!ccall) {
		return;
	}

	old_state = ccall->state;
	info("ccall(%p): State changed: `%s' --> `%s'\n",
		ccall,
		ccall_state_name(ccall->state),
		ccall_state_name(state));

	ccall->state = state;

	if (old_state == CCALL_STATE_IDLE && ccall->ts_start == 0)
		ccall->ts_start = tmr_jiffies();

	switch (ccall->state) {
	case CCALL_STATE_IDLE:
		ccall->sft_url = mem_deref(ccall->sft_url);
		ccall->sft_timestamp = 0;
		userlist_reset_keygenerator(ccall->userl);
		ccall->received_confpart = false;
		keystore_reset(ccall->keystore);
		tmr_cancel(&ccall->tmr_rotate_key);
		tmr_cancel(&ccall->tmr_rotate_mls);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_cancel(&ccall->tmr_connect);
		tmr_cancel(&ccall->tmr_decrypt_check);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		break;

	case CCALL_STATE_INCOMING:
		ccall->sft_url = mem_deref(ccall->sft_url);
		userlist_reset_keygenerator(ccall->userl);
		ccall->received_confpart = false;
		keystore_reset_keys(ccall->keystore);
		tmr_cancel(&ccall->tmr_rotate_key);
		tmr_cancel(&ccall->tmr_rotate_mls);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_start(&ccall->tmr_ongoing, CCALL_ONGOING_CALL_TIMEOUT,
			  ccall_ongoing_call_timeout, ccall);
		tmr_cancel(&ccall->tmr_decrypt_check);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		break;

	case CCALL_STATE_WAITCONFIG:
	case CCALL_STATE_WAITCONFIG_OUTGOING:
		ccall->sft_url = mem_deref(ccall->sft_url);
		userlist_reset_keygenerator(ccall->userl);
		ccall->received_confpart = false;
		keystore_reset_keys(ccall->keystore);
		tmr_cancel(&ccall->tmr_rotate_key);
		tmr_cancel(&ccall->tmr_rotate_mls);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_start(&ccall->tmr_connect, CCALL_CONNECT_TIMEOUT,
			  ccall_connect_timeout, ccall);
		tmr_cancel(&ccall->tmr_decrypt_check);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		tmr_cancel(&ccall->tmr_ring);
		break;

	case CCALL_STATE_CONNSENT:
		ccall->received_confpart = false;
		if (!ccall->is_mls_call) {
			if (ccall->reconnect_attempts == 0) {
				keystore_reset_keys(ccall->keystore);
				tmr_cancel(&ccall->tmr_rotate_mls);
			}
		}
		tmr_cancel(&ccall->tmr_rotate_key);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_start(&ccall->tmr_connect, CCALL_CONNECT_TIMEOUT,
			  ccall_connect_timeout, ccall);
		tmr_cancel(&ccall->tmr_decrypt_check);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		break;

	case CCALL_STATE_SETUPRECV:
		break;

	case CCALL_STATE_CONNECTING:
		break;

	case CCALL_STATE_CONNECTED:
		tmr_cancel(&ccall->tmr_connect);
		break;

	case CCALL_STATE_ACTIVE:
		ccall->expected_ping = 0;
		ccall->last_ping = 0;
		tmr_cancel(&ccall->tmr_connect);
		tmr_start(&ccall->tmr_decrypt_check, CCALL_DECRYPT_CHECK_TIMEOUT,
			  ccall_decrypt_check_timeout, ccall);
		tmr_start(&ccall->tmr_keepalive, CCALL_KEEPALIVE_TIMEOUT,
			  ccall_keepalive_timeout, ccall);
		if (ccall->reconnect_attempts > 0 && userlist_is_keygenerator_me(ccall->userl)) {
		        tmr_cancel(&ccall->tmr_send_check);
		        tmr_start(&ccall->tmr_send_check, 0,
				  ccall_send_check_timeout, ccall);
		}
		break;

	case CCALL_STATE_TERMINATING:
		tmr_cancel(&ccall->tmr_send_check);
		tmr_cancel(&ccall->tmr_connect);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		break;

	case CCALL_STATE_NONE:
		break;
	}

	if (state != old_state &&
	    (CCALL_STATE_ACTIVE == state || CCALL_STATE_ACTIVE == old_state)) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			&ccall->icall, ccall->icall.arg);
	}
}

static void ccall_send_check_timeout(void *arg)
{
	struct ccall *ccall = arg;
	uint64_t now = 0;
	int err = 0;

	if (CCALL_STATE_ACTIVE == ccall->state &&
	    userlist_is_keygenerator_me(ccall->userl)) {
		info("ccall(%p): send_check state=%s\n", ccall, ccall_state_name(ccall->state));

		ccall_send_msg(ccall, ECONN_CONF_CHECK,
			       true, NULL, false);
		if (err != 0) {
			warning("ccall(%p): send_check failed to send msg err=%d\n",
				ccall, err);
		}

		if (ccall->is_mls_call) {
			now = tmr_jiffies();
			info("ccall(%p): checking epoch age "
			     "tdiff: %llu timeout: %llu\n",
			     ccall,
			     now - ccall->epoch_start_ts,
			     CCALL_REQ_NEW_EPOCH_TIMEOUT);
		    	if (ccall->epoch_start_ts > 0 &&
			    now - ccall->epoch_start_ts >= CCALL_REQ_NEW_EPOCH_TIMEOUT) {
				info("ccall(%p): calling req_new_epochh\n", ccall);
				ICALL_CALL_CB(ccall->icall, req_new_epochh,
					&ccall->icall, ccall->icall.arg);
			}
		}
	}

	tmr_start(&ccall->tmr_send_check,
		  CCALL_SEND_CHECK_TIMEOUT,
		  ccall_send_check_timeout, ccall);
}

static void ccall_ongoing_call_timeout(void *arg)
{
	struct ccall *ccall = arg;

	if (CCALL_STATE_INCOMING == ccall->state) {
		info("ccall(%p): ongoing_call_timeout\n", ccall);

		set_state(ccall, CCALL_STATE_IDLE);
		ICALL_CALL_CB(ccall->icall, closeh, 
			      &ccall->icall, 0, &ccall->metrics, ECONN_MESSAGE_TIME_UNKNOWN,
			      NULL, NULL, ccall->icall.arg);
	}
}

static void ccall_stop_ringing_timeout(void *arg)
{
	struct ccall *ccall = arg;

	if (CCALL_STATE_INCOMING == ccall->state) {
		info("ccall(%p): stop_ringing_timeout\n", ccall);
		ICALL_CALL_CB(ccall->icall, leaveh,
			      &ccall->icall, ICALL_REASON_STILL_ONGOING,
			      ECONN_MESSAGE_TIME_UNKNOWN, ccall->icall.arg);
		ccall->is_ringing = false;
	}
}

static void ccall_sft_reject_timeout(void *arg)
{
	struct ccall *ccall = arg;
	int reason = 0;

	if (CCALL_STATE_CONNSENT == ccall->state) {
		info("ccall(%p): sft_reject_timeout status %u\n",
		     ccall,
		     ccall->confconn_status);
		set_state(ccall, CCALL_STATE_IDLE);
		switch (ccall->confconn_status) {
		case ECONN_CONFCONN_OK:
			return;
		case ECONN_CONFCONN_REJECTED_BLACKLIST:
			reason = ICALL_REASON_OUTDATED_CLIENT;
			break;
		case ECONN_CONFCONN_REJECTED_AUTH_INVALID:
		case ECONN_CONFCONN_REJECTED_AUTH_LIMIT:
		case ECONN_CONFCONN_REJECTED_AUTH_EXPIRED:
			reason = ICALL_REASON_AUTH_FAILED;
			break;
		case ECONN_CONFCONN_REJECTED_AUTH_CANTSTART:
			reason = ICALL_REASON_AUTH_FAILED_START;
			break;
		}
		ICALL_CALL_CB(ccall->icall, leaveh,
			      &ccall->icall, reason,
			      ECONN_MESSAGE_TIME_UNKNOWN, ccall->icall.arg);
		ecall_end(ccall->ecall);
	}
}

static void ccall_sync_vstate_timeout(void *arg)
{
	struct ccall *ccall = arg;

	ccall_set_vstate(&ccall->icall, ccall->vstate);
}

static void ccall_reconnect(struct ccall *ccall,
			    uint32_t msg_time,
			    bool notify,
			    bool again)
{
	bool decrypt_attempted = false;
	bool decrypt_successful = false;
	const struct zapi_ice_server *sfti = NULL;

	if (!ccall || !ccall->keystore) {
		return;
	}

	if (ccall->state == CCALL_STATE_CONNSENT) {
	        return;
	}

	keystore_get_decrypt_states(ccall->keystore,
				    &decrypt_attempted,
				    &decrypt_successful);
	info("ccall(%p): reconnect: cp: %s da: %s ds: %s att: %u p: %u\n",
	     ccall,
	     ccall->received_confpart ? "YES" : "NO",
	     decrypt_attempted ? " YES" : "NO",
	     decrypt_successful ? "YES" : "NO",
	     ccall->reconnect_attempts,
	     ccall->expected_ping);

	if (ccall->reconnect_attempts >= CCALL_MAX_RECONNECT_ATTEMPTS) {
		ccall_end_with_err(ccall, ETIMEDOUT);
		ICALL_CALL_CB(ccall->icall, leaveh, 
			&ccall->icall, ICALL_REASON_STILL_ONGOING,
			msg_time, ccall->icall.arg);
		return;
	}
	ccall->reconnect_attempts++;
	ccall->expected_ping = 0;
	ccall->last_ping = 0;

	if (notify) {
		ccall->inc_reconnects = true;
		ccall->metrics.reconnects_attempted++;
	}

	userlist_incall_clear(ccall->userl, true, again);
	list_flush(&ccall->videol);

	set_state(ccall, CCALL_STATE_CONNSENT);
	if (ccall->sft_url) {
		sfti = ccall_get_sft_info(ccall, ccall->sft_url);
		ccall_send_conf_conn(ccall,
				     ccall->sft_url,
				     sfti ? sfti->username : NULL,
				     sfti ? sfti->credential : NULL,
				     true);
	}

	if (notify) {
		ICALL_CALL_CB(ccall->icall, qualityh,
			      &ccall->icall, 
			      "SFT",
			      "SFT",
			      0,
			      ICALL_RECONNECTING,
			      ICALL_RECONNECTING,
			      ccall->icall.arg);
	}
}

static void ccall_decrypt_check_timeout(void *arg)
{
	struct ccall *ccall = arg;
	bool has_keys = false;
	bool decrypt_attempted = false;
	bool decrypt_successful = false;

	if (!ccall)
		return;

	if (ccall->state != CCALL_STATE_ACTIVE) {
		info("ccall(%p): decrypt_check_timeout in state %s, ignoring\n",
		     ccall,
		     ccall_state_name(ccall->state));
		return;
	}

	if (!ccall->received_confpart) {
		info("ccall(%p): decrypt_check_timeout no confpart received, "
		     "reconnecting\n",
		     ccall);
		ccall_reconnect(ccall, ECONN_MESSAGE_TIME_UNKNOWN, true,
				false);
		return;
	}

	if (!userlist_get_keygenerator(ccall->userl)) {
		info("ccall(%p): decrypt_check_timeout no keygenerator, waiting\n",
		     ccall);
		
		tmr_start(&ccall->tmr_decrypt_check, CCALL_DECRYPT_CHECK_TIMEOUT,
			  ccall_decrypt_check_timeout, ccall);
		return;
	}

	if (userlist_is_keygenerator_me(ccall->userl)) {
		info("ccall(%p): decrypt_check_timeout keygenerator is me\n",
		     ccall);
		return;
	}

	has_keys = keystore_has_keys(ccall->keystore);
	keystore_get_decrypt_states(ccall->keystore,
				    &decrypt_attempted,
				    &decrypt_successful);

	info("ccall(%p): decrypt_check_timeout state: %s key: %s att: %s succ: %s\n",
	     ccall,
	     ccall_state_name(ccall->state),
	     has_keys ? "YES" : "NO",
	     decrypt_attempted ? "YES" : "NO",
	     decrypt_successful ? "YES" : "NO");

	if (!has_keys || (decrypt_attempted && !decrypt_successful)) {
		ccall_request_keys(ccall);
	}
}

static int ecall_ping_handler(struct ecall *ecall,
			      bool response,
			      void *arg)
{
	struct ccall *ccall = arg;

	ccall->last_ping = tmr_jiffies();
	ccall->expected_ping = 0;
	ccall->reconnect_attempts = 0;

	if (ccall->inc_reconnects) {
		ccall->metrics.reconnects_successful++;
		ccall->inc_reconnects = false;
	}
	info("ccall(%p): ping arrived\n",
	     ccall);

	return 0;
}

static void ccall_keepalive_timeout(void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall)
		return;

	info("ccall(%p): keepalive: ping %u\n",
	     ccall,
	     ccall->expected_ping);
	if (ccall->state != CCALL_STATE_ACTIVE) {
		info("ccall(%p): keepalive_timeout in state %s, ignoring\n",
		     ccall,
		     ccall_state_name(ccall->state));
		return;
	}

	if (!ccall->ecall) {
		info("ccall(%p): keepalive_timeout no ecall, ignoring\n",
		     ccall);
		return;
	}

	ecall_ping(ccall->ecall, false);
	ccall->expected_ping++;
	if (ccall->expected_ping > CCALL_MAX_MISSING_PINGS) {
		ccall_reconnect(ccall, ECONN_MESSAGE_TIME_UNKNOWN, true, false);
	}
	else {
		tmr_start(&ccall->tmr_keepalive, CCALL_KEEPALIVE_TIMEOUT,
			  ccall_keepalive_timeout, ccall);
	}
}

static void ccall_alone_timeout(void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall)
		return;

	if (ccall->state != CCALL_STATE_ACTIVE) {
		info("ccall(%p): alone_timeout in state %s, ignoring\n",
		     ccall,
		     ccall_state_name(ccall->state));
		return;
	}

	if (!ccall->ecall) {
		info("ccall(%p): alone_timeout no ecall, ignoring\n",
		     ccall);
		return;
	}

	info("ccall(%p): alone_timeout sj: %s\n",
	     ccall, ccall->someone_joined ? "YES" : "NO");

	ccall_end_with_err(ccall,
			   ccall->someone_joined ? EEVERYONELEFT :
						   ENOONEJOINED);
}

static int ccall_generate_session_key(struct ccall *ccall,
			      bool is_first)
{
	uint8_t session_key[E2EE_SESSIONKEY_SIZE];
	uint32_t idx = 0;
	int err = 0;

	if (!ccall || !ccall->keystore) {
		return EINVAL;
	}

	if (!is_first) {
		idx = keystore_get_max_key(ccall->keystore) | 0xFFFF;
		idx++;
		if (ccall->became_kg) {
			idx += 0x10000;
		}
	}
	ccall->became_kg = false;
	info("ccall(%p): generate_session_key %08x\n", ccall, idx);
	randombytes_buf(session_key, E2EE_SESSIONKEY_SIZE);

	err = keystore_set_fresh_session_key(ccall->keystore,
					     idx,
					     session_key,
					     E2EE_SESSIONKEY_SIZE,
					     mbuf_buf(&ccall->confpart_data),
					     mbuf_get_left(&ccall->confpart_data));
	if (err) {
		goto out;
	}

	err = ccall_send_keys(ccall, true);
	if (err) {
		goto out;
	}

out:
	sodium_memzero(session_key, E2EE_SESSIONKEY_SIZE);
	return err;
}

static int ccall_send_keys(struct ccall *ccall,
			   bool send_to_all)
{
	struct list targets = LIST_INIT;
	int err = 0;

	if (ccall->is_mls_call) {
		info("ccall(%p): send_keys: ignore for mls call\n", ccall);
		return 0;
	}

	if (userlist_is_keygenerator_me(ccall->userl) &&
	    CCALL_STATE_ACTIVE == ccall->state) {

		err = userlist_get_key_targets(ccall->userl, &targets, send_to_all);
		if (err)
			goto out;

	}

	if (list_count(&targets) > 0) {
		info("ccall(%p): send_keys state=%s targets=%u\n",
		     ccall,
		     ccall_state_name(ccall->state),
		     list_count(&targets));

		ccall_send_msg(ccall, ECONN_CONF_KEY,
			       true, &targets,
			       false);
	}

out:
	list_flush(&targets);
	return err;
}

static void ccall_rotate_key_timeout(void *arg)
{
	struct ccall *ccall = arg;
	int err = 0;

	if (!ccall) {
		return;
	}

	if (CCALL_STATE_ACTIVE == ccall->state &&
	    (userlist_is_keygenerator_me(ccall->userl) &&
	     !ccall->is_mls_call)) {
		info("ccall(%p): rotate_key state=%s\n", ccall, ccall_state_name(ccall->state));

		err = keystore_rotate(ccall->keystore);
		if (err) {
			warning("ccall(%p): rotate_key err=%m\n", ccall, err);
		}

		if (ccall->someone_left &&
		    !ccall->is_mls_call) {
			ccall_generate_session_key(ccall, false);
			ccall->someone_left = false;

		}
		tmr_start(&ccall->tmr_rotate_key,
			  CCALL_ROTATE_KEY_TIMEOUT,
			  ccall_rotate_key_timeout, ccall);
	}
}

static void ccall_rotate_mls_timeout(void *arg)
{
	struct ccall *ccall = arg;
	uint64_t keytime = 0;
	uint64_t now = 0;

	if (!ccall) {
		return;
	}

	if (CCALL_STATE_ACTIVE == ccall->state && ccall->is_mls_call) {
		now = tmr_jiffies();
		assert(now > CCALL_MLS_KEY_AGE);
		keytime = now - CCALL_MLS_KEY_AGE;

		info("ccall(%p): rotate_key_mls state=%s now=%llu keytime=%llu\n",
		     ccall,
		     ccall_state_name(ccall->state),
		     now,
		     keytime);

		if (keystore_rotate_by_time(ccall->keystore, keytime)) {
			tmr_start(&ccall->tmr_rotate_mls,
				  CCALL_ROTATE_MLS_TIMEOUT,
				  ccall_rotate_mls_timeout, ccall);
		}
	}
}

static int ccall_request_keys(struct ccall *ccall)
{
	struct list targets = LIST_INIT;
	struct icall_client *c;
	const struct userinfo *kg = NULL;
	int err = 0;

	if (!ccall)
		return EINVAL;

	kg = userlist_get_keygenerator(ccall->userl);
	if (!kg) {
		warning("ccall(%p): request_keys keygenerator is NULL!\n", ccall);
		return 0;
	}

	if (ccall->is_mls_call) {
		warning("ccall(%p): request_keys ignore for mls calls!\n", ccall);
		return 0;
	}

	if (userlist_is_keygenerator_me(ccall->userl)) {
		warning("ccall(%p): request_keys keygenerator is self!\n", ccall);
		return 0;
	}

	if (!kg->userid_real || !kg->clientid_real) {
		warning("ccall(%p): request_keys keygenerator is invalid!\n", ccall);
		return 0;
	}

	c = icall_client_alloc(kg->userid_real,
			       kg->clientid_real);
	if (!c) {
		warning("ccall(%p): ccall_request_keys "
			"unable to alloc target\n", ccall);
		err = ENOMEM;
		goto out;
	}
	list_append(&targets, &c->le, c);

	info("ccall(%p): request_keys state=%s targets=%u\n",
	     ccall,
	     ccall_state_name(ccall->state),
	     list_count(&targets));

	ccall_send_msg(ccall, ECONN_CONF_KEY,
		       false, &targets,
		       false);

out:
	list_flush(&targets);
	return err;
}

static void ecall_setup_handler(struct icall *icall,
				uint32_t msg_time,
			        const char *userid_sender,
			        const char *clientid_sender,
			        bool video_call,
				bool should_ring,
				enum icall_conv_type conv_type,
			        void *arg)
{
	struct ccall *ccall = arg;
	bool audio_cbr;

#ifdef CCALL_CBR_ALWAYS_ON
	audio_cbr = true;
#else
	audio_cbr = false;
#endif

	(void)msg_time;
	(void)icall; /* not really used, revise code below and use directly */
	(void)should_ring;
	(void)conv_type;

	ecall_answer(ccall->ecall,
		     ccall->call_type,
		     audio_cbr);
}

static void ecall_setup_resp_handler(struct icall *icall, void *arg)
{
	//struct ccall *ccall = arg;
}

static void ecall_datachan_estab_handler(struct icall *icall,
					 const char *userid,
					 const char *clientid,
					 bool update,
					 void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall) {
		return;
	}

	switch (ccall->state) {
	case CCALL_STATE_CONNECTING:
		set_state(ccall, CCALL_STATE_CONNECTED);
		break;

	default:
		info("ccall(%p): ecall_datachan_estab_handler "
		     "established in state %s\n",
		     ccall, ccall_state_name(ccall->state));
		break;

	}
}

static void ecall_media_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update, void *arg)
{
	struct ecall *ecall = (struct ecall *)icall;
	struct ccall *ccall = arg;

	if (!ccall)
		return;

	if (ccall->ecall != ecall) {
		warning("ccall(%p): media_estab_handler: on wrong ecall: %p(%p)\n",
			ccall, ecall, ccall->ecall);
		return;
	}

	ICALL_CALL_CB(ccall->icall, media_estabh,
		icall, userid, clientid, update, ccall->icall.arg);

	if (ccall->is_mls_call) {
	        ICALL_CALL_CB(ccall->icall, req_new_epochh,
			      &ccall->icall, ccall->icall.arg);
	}

	if (CCALL_STATE_CONNSENT != ccall->state) {
		enum ccall_state old_state = ccall->state;
		set_state(ccall, CCALL_STATE_ACTIVE);
		userlist_incall_clear(ccall->userl, true,
				      old_state == CCALL_STATE_ACTIVE);
	}
	else {
		info("ccall(%p): refusing to go to CCALL_STATE_ACTIVE "
		     "from CCALL_STATE_CONNSENT\n");
	}
}

static void ecall_audio_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update, void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall)
		return;

	ICALL_CALL_CB(ccall->icall, audio_estabh,
		icall, userid, clientid, update, ccall->icall.arg);
}


static void ecall_aulevel_handler(struct icall *icall, struct list *levell, void *arg)
{
	struct ccall *ccall = arg;
	bool changed = false;

	if (!ccall)
		return;

	userlist_update_audio_level(ccall->userl,
				    levell,
				    &changed);
	if (changed) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			&ccall->icall, ccall->icall.arg);
	}

	ICALL_CALL_CB(ccall->icall, audio_levelh,
		      icall, levell, ccall->icall.arg);
}


static void ecall_close_handler(struct icall *icall,
				int err,
				struct icall_metrics *metrics,
				uint32_t msg_time,
				const char *userid,
				const char *clientid,
				void *arg)
{
	struct ecall *ecall = (struct ecall*)icall;
	struct ccall *ccall = arg;
	bool should_end = false;

	should_end = userlist_incall_count(ccall->userl) == 0;
	info("ccall(%p): ecall_close_handler err=%d ecall=%p should_end=%s parts=%u\n",
	     ccall, err, ecall, should_end ? "YES" : "NO", userlist_get_count(ccall->userl));

	if (ecall != ccall->ecall) {
		mem_deref(ecall);
		return;
	}

	userlist_reset_keygenerator(ccall->userl);

	if (err != EAGAIN) {
// TODO: call vstateh
#if 0
		LIST_FOREACH(&ccall->partl, le) {
			struct userinfo *u = le->data;

			if (u && u->incall_now &&
			    u->video_state != ICALL_VIDEO_STATE_STOPPED) {
				ICALL_CALL_CB(ccall->icall, vstate_changedh,
					      &ccall->icall, u->userid_real,
					      u->clientid_real, ICALL_VIDEO_STATE_STOPPED,
					      ccall->icall.arg);
				u->video_state = ICALL_VIDEO_STATE_STOPPED;
			}
		}
#endif
	}

	if (err == EAGAIN || err == ENOTCONN) {
		ccall->reconnect_attempts = 0;
		ccall->expected_ping = 0;
		ccall->last_ping = 0;

		ccall_reconnect(ccall, msg_time,
				err == ENOTCONN,
				err == EAGAIN);
		return;
	}

	userlist_incall_clear(ccall->userl, false, false);
	mem_deref(ecall);
	ccall->ecall = NULL;

	switch (ccall->error) {
	case 0:
	case ENOONEJOINED:
	case EEVERYONELEFT:
		if (should_end && ccall->received_confpart) {
			ccall_send_msg(ccall, ECONN_CONF_END,
				       false, NULL, false);
		}
		break;
	}

	if (metrics) {
		uint64_t now = tmr_jiffies();
		ccall->metrics.duration_call = (now - ccall->ts_start) / 1000;
		ccall->metrics.duration_active += metrics->duration_call;
		ccall->metrics.packetloss_last = metrics->packetloss_last;
		ccall->metrics.packetloss_max  = MAX(metrics->packetloss_max, ccall->metrics.packetloss_max);
		ccall->metrics.rtt_last = metrics->rtt_last;
		ccall->metrics.rtt_max  = MAX(metrics->rtt_max, ccall->metrics.rtt_max);
	}
	if (should_end) {
		set_state(ccall, CCALL_STATE_IDLE);

		ICALL_CALL_CB(ccall->icall, closeh, 
			&ccall->icall, ccall->error, &ccall->metrics, msg_time,
			NULL, NULL, ccall->icall.arg);
	}
	else {
		set_state(ccall, CCALL_STATE_INCOMING);

		ICALL_CALL_CB(ccall->icall, leaveh, 
			&ccall->icall, ICALL_REASON_STILL_ONGOING,
			msg_time, ccall->icall.arg);
	}

	ccall->error = 0;
}


static void ecall_quality_handler(struct icall *icall,
				  const char *userid,
				  const char *clientid,
				  int rtt, int uploss, int downloss,
				  void *arg)
{
	struct ccall *ccall = arg;
	uint64_t tdiff;
	bool dec_res = false;

	if (!icall || !ccall)
		return;

	if (CCALL_STATE_ACTIVE != ccall->state)
		return;

	tdiff = tmr_jiffies() - ccall->last_ping;

	info("ccall(%p): ecall_quality_handler rtt=%d up=%d dn=%d "
	     "ping=%u pdiff=%llu\n",
	     ccall, rtt, uploss, downloss, ccall->expected_ping, tdiff);

	if (downloss > 20) {
		dec_res = true;
	}
	if (ccall->expected_ping >= CCALL_QUALITY_POOR_MISSING) {
		dec_res = true;
		downloss = 30;
	}
	else if (ccall->expected_ping > CCALL_QUALITY_MEDIUM_MISSING) {
		dec_res = true;
		downloss = 10;
	}

#if RESOLUTION_DEGRADE
	if (dec_res) {
		struct le *le;
		struct list clil = LIST_INIT;
		bool send_request = false;

		LIST_FOREACH(&ccall->videol, le) {
			struct icall_client *cli = le->data;
			struct icall_client *vinfo = NULL;

			vinfo = icall_client_alloc(cli->userid,
						   cli->clientid);
			vinfo->quality = cli->quality;
			vinfo->vstate = cli->vstate;
			if (cli->vstate != ICALL_VIDEO_STATE_SCREENSHARE
			 && cli->quality >= CCALL_RESOLUTION_HIGH) {
				vinfo->quality = CCALL_RESOLUTION_LOW;
				send_request = true;
			}
			list_append(&clil, &vinfo->le, vinfo);
		}

		if (send_request && clil.head) {
			ccall_request_video_streams((struct icall *)ccall,
						    &clil,
						    0);
		}
		list_flush(&clil);
	}
#else
	(void)dec_res;
#endif

	ICALL_CALL_CB(ccall->icall, qualityh,
		      &ccall->icall, 
		      userid,
		      clientid,
		      rtt,
		      uploss,
		      downloss,
		      ccall->icall.arg);
}


static int ccall_send_msg_sft(struct ccall *ccall,
			      const char *sft_url,
			      struct econn_message *msg)
{
	int err = 0;
	char *url = NULL;
	const char *fmt;
	int len;

	if (!sft_url) {
		return EINVAL;
	}

	len = strlen(sft_url);
	if (len < 1) {
		return EINVAL;
	}

	if (ECONN_CONF_CONN == msg->msg_type ||
	    ECONN_SETUP == msg->msg_type ||
	    ECONN_UPDATE == msg->msg_type) {
		// Send these messages to the SFT

		if (sft_url[len - 1] == '/') {
			fmt = "%ssft/%s";
		}
		else {
			fmt = "%s/sft/%s";
		}

		len = strlen(fmt) +
		      strlen(sft_url) + 
		      strlen(ccall->convid_hash) + 3;

		url = mem_zalloc(len + 1, NULL);
		if (!url) {
			err = ENOMEM;
			goto out;
		}
		snprintf(url, len, fmt,
			 sft_url,
			 ccall->convid_hash);

		info("ccall(%p): ecall_transp_send_handler send "
		     "msg: %s transp: sft url: %s hndlr: %p conv: %s\n",
		     ccall, econn_msg_name(msg->msg_type), url, ccall->icall.sfth, ccall->convid_hash);

		str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);
		err = ICALL_CALL_CBE(ccall->icall, sfth,
			&ccall->icall, url, msg, ccall->icall.arg);
		if (err)
			goto out;

		if ((ECONN_SETUP == msg->msg_type ||
		    ECONN_UPDATE == msg->msg_type) &&
		    CCALL_STATE_ACTIVE != ccall->state) {
			set_state(ccall, CCALL_STATE_CONNECTING);
		}
	}

out:
	mem_deref(url);
	return err;
}

static int ecall_transp_send_handler(struct icall *icall,
				     const char *userid,
				     struct econn_message *msg,
				     struct list *targets,
				     bool my_clients_only,
				     void *arg)
{
	struct ccall *ccall = arg;
	(void) userid;
	(void) targets;

	return ccall_send_msg_sft(ccall, ccall->sft_url, msg);
}

static int send_confpart_response(struct ccall *ccall)
{
	struct econn_message *msg = NULL;
	char *str = NULL;
	const struct userinfo *self = NULL;
	//struct le *le = NULL;
	struct mbuf mb;
	//char userid_anon[ANON_ID_LEN];
	//char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	if (!ccall)
		return EINVAL;

	self = userlist_get_self(ccall->userl);
	if (!self)
		return ENOENT;

	err = alloc_message(&msg, ccall, ECONN_CONF_PART, true,
		self->userid_hash, self->clientid_hash,
		"SFT", "SFT", false);
	if (err) {
		goto out;
	}

	err = userlist_get_partlist(ccall->userl,
				    &msg->u.confpart.partl,
				    ccall->is_mls_call);
	if (err) {
		goto out;
	}

	err = econn_message_encode(&str, msg);
	if (err) {
		warning("ccall(%p): send_confpart_resp: econn_message_encode"
			" failed (%m)\n", ccall, err);
		goto out;
	}

	ecall_trace(ccall->ecall, msg, true, ECONN_TRANSP_DIRECT,
		    "DataChan %H\n",
		    econn_message_brief, msg);

	mb.pos = 0;
	mb.size = str_len(str);
	mb.end = mb.size;
	mb.buf = (uint8_t *)str;

	err =  ecall_dce_send(ccall->ecall, &mb);
	if (err) {
		warning("ccall(%p): send_confpart_resp: ecall_dce_send"
			" failed (%m)\n", ccall, err);
		goto out;
	}

 out:
	mem_deref(str);
	mem_deref(msg);

	return err;
}

#if USE_VIDEO_REQUEST_LIMITER
// Right now it's causing more problems, so we're disabling this feature. But we'll reconsider
static bool has_video_for_client(struct ccall *ccall, struct icall_client *cli)
{
	bool found = false;
	struct le *le;

	for(le = ccall->videol.head; !found && le; le = le->next) {
		struct icall_client *vi = le->data;

		found = streq(vi->userid, cli->userid)
		     && streq(vi->clientid, cli->clientid)
		     && vi->quality == cli->quality;
	}

	return found;
}
#endif

int  ccall_request_video_streams(struct icall *icall,
				 struct list *clientl,
				 enum icall_stream_mode mode)
{
	struct ccall *ccall = (struct ccall*)icall;
	const struct userinfo *self = NULL;
	struct econn_stream_info *sinfo;
	struct econn_message *msg;
	char *str = NULL;
	struct mbuf mb;
	struct le *le = NULL;
	struct mbuf *qb;
	char *clients_str;
	int err = 0;

	if (!ccall)
		return EINVAL;

	if (!clientl || NULL == clientl->head)
		return EINVAL;

#if USE_VIDEO_REQUEST_LIMITER
	bool found = true;
	info("ccall(%p): video request limiter is on!\n", ccall);
	for(le = clientl->head; found && le; le = le->next) {
		struct icall_client *cli = le->data;

		found = has_video_for_client(ccall, cli);
	}
	if (found) {
		info("ccall(%p): request_video_streams skipping for identical clients\n", ccall);
		return 0;
	}
#endif

	self = userlist_get_self(ccall->userl);
	if (!self)
		return ENOENT;

	err = alloc_message(&msg, ccall, ECONN_CONF_STREAMS, false,
		self->userid_hash, self->clientid_hash,
		"SFT", "SFT", false);
	if (err) {
		goto out;
	}

	list_flush(&ccall->videol);
	str_dup(&msg->u.confstreams.mode, "list");
	qb = mbuf_alloc(1024);
	LIST_FOREACH(clientl, le) {
		struct icall_client *cli = le->data;
		struct userinfo *user;
		uint32_t quality = (uint32_t)cli->quality;
		struct icall_client *vinfo;
		char userid_anon[ANON_ID_LEN];
		char clientid_anon[ANON_CLIENT_LEN];

		user = userlist_find_by_real(ccall->userl,
					     cli->userid, cli->clientid);
		if (user) {

			sinfo = econn_stream_info_alloc(user->userid_hash, quality);
			if (!sinfo) {
				err = ENOMEM;
				goto out;
			}
		
			list_append(&msg->u.confstreams.streaml, &sinfo->le, sinfo);

			vinfo = icall_client_alloc(cli->userid,
						   cli->clientid);
			vinfo->quality = cli->quality;
			vinfo->vstate = user->video_state;
			mbuf_printf(qb, "%s.%s(q=%d) ",
				    anon_id(userid_anon, cli->userid),
				    anon_client(clientid_anon, cli->clientid),
				    cli->quality);
			list_append(&ccall->videol, &vinfo->le, vinfo);
		}
	}

	qb->pos = 0;
	mbuf_strdup(qb, &clients_str, qb->end);
	mem_deref(qb);

	info("ccall(%p): request_video_streams mode: %u clients: %u matched: %u [%s]\n",
	     ccall,
	     mode,
	     list_count(clientl),
	     list_count(&msg->u.confstreams.streaml),
         strlen(clients_str) != 0 ? clients_str : "");
	mem_deref(clients_str);

	ccall->metrics.participants_video_req = MAX(ccall->metrics.participants_video_req,
						    list_count(&msg->u.confstreams.streaml));
	err = econn_message_encode(&str, msg);
	if (err) {
		warning("ccall(%p): request_video_streams: econn_message_encode"
			" failed (%m)\n", ccall, err);
		goto out;
	}

	ecall_trace(ccall->ecall, msg, true, ECONN_TRANSP_DIRECT,
		    "DataChan %H\n",
		    econn_message_brief, msg);

	mb.pos = 0;
	mb.size = str_len(str);
	mb.end = mb.size;
	mb.buf = (uint8_t *)str;

	err =  ecall_dce_send(ccall->ecall, &mb);
	if (err) {
		warning("ccall(%p): request_video_streams: ecall_dce_send"
			" failed (%m)\n", ccall, err);
		goto out;
	}

 out:
	mem_deref(str);
	mem_deref(msg);

	return err;
}

static int ccall_sync_props(struct ccall *ccall)
{
	char estr[32];
	uint32_t epochid = 0;
	int err = 0;

	if (!ccall) {
		return EINVAL;
	}

	if (!ccall->ecall) {
		return 0;
	}

	err = userlist_get_latest_epoch(ccall->userl, &epochid);
	if (err) {
		epochid = 0;
	}

	memset(estr, 0, sizeof(estr));
	snprintf(estr, sizeof(estr) - 1, "%u", epochid);

	err = ecall_props_set_local(ccall->ecall, "keysync", estr);
	if (err) {
		warning("ccall(%p): set_media_key set_props failed\n", ccall);
		goto out;
	}
	err = ecall_sync_props(ccall->ecall, true);
	if (err) {
		warning("ccall(%p): set_media_key sync_props failed\n", ccall);
		goto out;
	}

out:
	return err;
}

static int ccall_set_media_key(struct icall *icall,
			       uint32_t epochid,
			       const uint8_t *key_data,
			       uint32_t key_size)
{
	struct ccall *ccall = (struct ccall*)icall;
	int err = 0;
	info("ccall(%p): set_media_key with key %u bytes\n", ccall, key_size);

	if (!ccall)
		return EINVAL;

	if (!ccall->is_mls_call) {
		warning("ccall(%p): set_media_key called for Proteus call\n", ccall);
		return EINVAL;
	}

	err = keystore_set_session_key(ccall->keystore,
				       epochid,
				       key_data,
				       key_size);

	if (err)
		goto out;

	ccall->epoch_start_ts = tmr_jiffies();
	userlist_set_latest_epoch(ccall->userl, epochid);
	err = ccall_sync_props(ccall);
	if (err) {
		warning("ccall(%p): set_media_key sync_props failed\n", ccall);
		err = 0;
	}

	ccall_sync_media_keys(ccall);
	ccall_rotate_mls_timeout(ccall);

out:
	return err;
}

static int ccall_sync_media_keys(struct ccall *ccall)
{
	uint32_t min_key;
	uint32_t curr;
	uint64_t ts;
	int err = 0;

	if (!ccall || !ccall->userl)
		return EINVAL;

	min_key = userlist_get_key_index(ccall->userl);
	if (min_key == 0)
		return 0;

	err = keystore_get_current(ccall->keystore, &curr, &ts);
	if (err)
		goto out;

	info("ccall(%p): sync_media_keys min:%u curr:%u\n",
	     ccall, min_key, curr);
	if (min_key > curr)
		keystore_set_current(ccall->keystore, min_key);

	ccall_rotate_mls_timeout(ccall);
out:
	return err;
}

static  int ecall_propsync_handler(struct ecall *ecall,
				   struct econn_message *msg,
				   void *arg)
{
	struct ccall *ccall = arg;
	struct userinfo *user;
	enum icall_vstate vstate = ICALL_VIDEO_STATE_STOPPED;
	bool vstate_present = false;
	bool muted = false;
	bool muted_present = false;
	bool group_changed = false;
	const char *vr;
	const char *mt;
	const char *ks;
	uint32_t latest_epoch = 0;
	int err = 0;

	if (!ccall || !msg) {
		return EINVAL;
	}

	vr = econn_props_get(msg->u.propsync.props, "videosend");
	mt = econn_props_get(msg->u.propsync.props, "muted");
	ks = econn_props_get(msg->u.propsync.props, "keysync");
	if (ks) {
		sscanf(ks, "%u", &latest_epoch);
	}
	info("ccall(%p): ecall_propsync_handler ecall: %p"
		" remote %s.%s video '%s' muted '%s' latest_epoch %d %s\n",
	     ccall, ecall, msg->src_userid, msg->src_clientid,
	     vr ? vr : "", mt ? mt : "", latest_epoch, msg->resp ? "resp" : "req");

	user = userlist_find_by_hash(ccall->userl, msg->src_userid, msg->src_clientid);
	if (!user) {
		return 0;
	}

	propsync_get_states(msg->u.propsync.props,
			    &vstate_present,
			    &vstate,
			    &muted_present,
			    &muted);

	if (vstate_present && vstate != user->video_state) {
		info("ccall(%p): propsync_handler updating video_state "
		     "%s -> %s\n",
		     ccall,
		     icall_vstate_name(user->video_state),
		     icall_vstate_name(vstate));
		
		user->video_state = vstate;

		ICALL_CALL_CB(ccall->icall, vstate_changedh,
			      &ccall->icall, user->userid_real,
			      user->clientid_real, vstate, ccall->icall.arg);
		group_changed = true;
	}

	if (muted_present && muted != user->muted) {
		info("ccall(%p): propsync_handler updating mute_state "
		     "%s -> %s\n",
		     ccall,
		     muted ? "true" : "false",
		     user->muted ? "true" : "false");
		user->muted = muted;
		group_changed = true;
	}

	if (group_changed) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			&ccall->icall, ccall->icall.arg);	
	}

	if (latest_epoch > 0 && latest_epoch != user->latest_epoch) {
		user->latest_epoch = latest_epoch;
		err = ccall_sync_media_keys(ccall);
		if (err) {
			warning("ccall(%p): propsync_handler: error "
				"syncing media keys!\n", ccall);
			goto out;
		}
	}

	ccall_update_active_counts(ccall);
out:
	return err;
}

static void ccall_keep_confpart_data(struct ccall *ccall,
				     const struct econn_message *msg)
{
	struct le *le;
	int err = 0;

	if (!ccall || !msg) {
		warning("ccall(%p): confpart_data invalid params\n", ccall);
		return;
	}

	mbuf_reset(&ccall->confpart_data);

	err = mbuf_write_u64(&ccall->confpart_data,
			     htonll(msg->u.confpart.timestamp));
	if (err)
		goto out;
	err = mbuf_write_u32(&ccall->confpart_data,
			     htonl(msg->u.confpart.seqno));
	if (err)
		goto out;
	err = mbuf_write_mem(&ccall->confpart_data,
			     msg->u.confpart.entropy,
			     msg->u.confpart.entropylen);
	if (err)
		goto out;

	list_flush(&ccall->saved_partl);

	LIST_FOREACH(&msg->u.confpart.partl, le) {
		struct econn_group_part *p = le->data;
		struct econn_group_part *pcopy = NULL;

		if (!p)
			continue;
		err = mbuf_write_str(&ccall->confpart_data,
				     p->userid);
		if (err)
			goto out;
		err = mbuf_write_u32(&ccall->confpart_data,
				     htonl(p->ssrca));
		if (err)
			goto out;
		err = mbuf_write_u32(&ccall->confpart_data,
				     htonl(p->ssrcv));
		if (err)
			goto out;

		pcopy = econn_part_alloc(p->userid, p->clientid);
		if (!pcopy) {
			err = ENOMEM;
			goto out;
		}
		pcopy->ssrca = p->ssrca;
		pcopy->ssrcv = p->ssrcv;

		list_append(&ccall->saved_partl, &pcopy->le, pcopy);
	}

	mbuf_set_pos(&ccall->confpart_data, 0);
out:
	if (err) {
		warning("ccall(%p): keep_confpart_data failed (%m)\n",
			ccall, err);
	}
}


static bool sfts_equal(const char *sfta, const char *sftb)
{
	size_t lena, lenb;

	if (!sfta || !sftb) {
		return false;
	}
	lena = strlen(sfta);
	if (lena > 0 && sfta[lena - 1] == '/') {
		lena--;
	}

	lenb = strlen(sftb);
	if (lenb > 0 && sftb[lenb - 1] == '/') {
		lenb--;
	}

	if (lena != lenb) {
		return false;
	}

	return (strncmp(sfta, sftb, lena) == 0);
}


static bool ccall_sftlist_changed(const struct list *lista, const struct list *listb)
{
	struct le *lea, *leb;
	struct stringlist_info *stra, *strb;

	if (list_count(lista) != list_count(listb))
		return true;

	lea = lista->head;
	leb = listb->head;
	while(lea && leb) {
		stra = lea->data;
		strb = leb->data;
		if (!sfts_equal(stra->str, strb->str)) {
			return true;
		}

		lea = lea->next;
		leb = leb->next;
	}

	return false;
}


static void ccall_update_active_counts(struct ccall *ccall)
{
	uint32_t active, active_a, active_v;
	int err;

	err = userlist_get_active_counts(ccall->userl, &active, &active_a, &active_v);
	if (!err) {
		if (!msystem_get_muted())
			active_a++;

		if (ccall->vstate == ICALL_VIDEO_STATE_STARTED)
			active_v++;

		ccall->metrics.participants_max = MAX(ccall->metrics.participants_max, active);
		ccall->metrics.participants_audio_max = MAX(ccall->metrics.participants_audio_max, active_a);
		ccall->metrics.participants_video_max = MAX(ccall->metrics.participants_video_max, active_v);
	}
}


static void ecall_confpart_handler(struct ecall *ecall,
				   const struct econn_message *msg,
				   void *arg)
{
	struct ccall *ccall = arg;
	bool list_changed = false;
	bool self_changed = false;
	bool first_confpart = false;
	bool missing_parts = false;
	int err = 0;

	uint64_t timestamp = msg->u.confpart.timestamp;
	uint32_t seqno = msg->u.confpart.seqno;
	bool should_start = msg->u.confpart.should_start;
	const struct list *partlist = &msg->u.confpart.partl;
	first_confpart = !ccall->received_confpart;

	info("ccall(%p): ecall_confpart_handler ecall: %p "\
	     "should_start %s partl: %u members ts: %llu sn: %u "\
	     "first: %s\n",
	     ccall, ecall, should_start ? "YES" : "NO",
	     list_count(partlist), timestamp, seqno,
	     first_confpart ? "YES" : "NO");

	if (!ccall || ecall != ccall->ecall) {
		return;
	}

	if (!should_start &&
	    ccall->is_caller &&
	    ccall->sft_timestamp == 0 &&
	    ccall->sft_seqno == 0 &&
	    list_count(partlist) == 1) {
		/* Handle the corner case that we started a video call,
		 * got a very quick data_chan_estab,
		 * and missed the initial CONFPART due to UPDATE
		 * This should fix SQCALL-587
		 */
		info("ccall(%p): forcing should_start true\n", ccall);
		should_start = true;
	}

	ccall->received_confpart = true;
	ccall_keep_confpart_data(ccall, msg);
	if (should_start)
		ccall->metrics.initiator = true;

	if (should_start && ccall->is_caller) {
		ccall->sft_timestamp = timestamp;
		ccall->sft_seqno = seqno;

		info("ccall(%p): sending CONFSTART from should_start\n", ccall);
		ccall_send_msg(ccall, ECONN_CONF_START,
			       false, NULL, false);
	}
	else if (ccall->sft_timestamp == 0 && ccall->sft_seqno == 0) {
		ccall->sft_timestamp = timestamp;
		ccall->sft_seqno = seqno;
		warning("ccall(%p): setting ts and seqno because they are currently unset\n", ccall);
	}
		

	if (list_count(partlist) > 1) {
		tmr_cancel(&ccall->tmr_alone);
		ccall->someone_joined = true;
	}
	else {
		tmr_start(&ccall->tmr_alone,
			  ccall->someone_joined ? CCALL_EVERYONE_LEFT_TIMEOUT :
						  CCALL_NOONE_JOINED_TIMEOUT,
			  ccall_alone_timeout, ccall);
	}

	if (first_confpart && ccall->ecall) {
		err = ccall_sync_props(ccall);
		if (err) {
			warning("ccall(%p): sync_props failed\n", ccall);
		}
	}

	err = userlist_update_from_sftlist(ccall->userl,
					   partlist,
					   &list_changed,
					   &self_changed,
					   &missing_parts);
	if (err) {
		warning("ccall(%p): update_from_sftlist failed\n", ccall);
		return;
	}

	send_confpart_response(ccall);

	if (self_changed) {
		const struct userinfo *self = userlist_get_self(ccall->userl);

		ecall_update_ssrc(ccall->ecall, self->ssrca, self->ssrcv);
	}

	if (list_changed) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			&ccall->icall, ccall->icall.arg);	

		if (userlist_is_keygenerator_me(ccall->userl) &&
		    !ccall->is_mls_call) {
			err = ccall_send_keys(ccall, false);
			if (err) {
				warning("ccall(%p): send_keys failed\n", ccall);
			}
		}

	}

	if (missing_parts) {
		ICALL_CALL_CB(ccall->icall, req_clientsh,
		      &ccall->icall, ccall->icall.arg);
	}

	bool sft_changed = ccall_sftlist_changed(&ccall->sftl, &msg->u.confpart.sftl);
	stringlist_clone(&msg->u.confpart.sftl, &ccall->sftl);
	
	if (userlist_is_keygenerator_me(ccall->userl) &&
	    !should_start &&
	    sft_changed) {
		ccall_send_check_timeout(ccall);
	}
}


static void ecall_confmsg_handler(struct ecall *ecall,
				  const struct econn_message *msg,
				  void *arg)
{
	if (!ecall || !msg) {
		return;
	}

	if (msg->msg_type == ECONN_CONF_PART) {
		ecall_confpart_handler(ecall, msg, arg);
	}
}


static int alloc_message(struct econn_message **msgp,
			 struct ccall *ccall,
			 enum econn_msg type,
			 bool resp,
			 const char *src_userid,
			 const char *src_clientid,
			 const char *dest_userid,
			 const char *dest_clientid,
			 bool transient)
{
	struct econn_message *msg = NULL;
	int err = 0;

	if (ccall == NULL) {
		return EINVAL;
	}

	msg = econn_message_alloc();
	if (msg == NULL) {
		return ENOMEM;
	}

	str_ncpy(msg->src_userid, src_userid, ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, src_clientid, ECONN_ID_LEN);
	msg->msg_type = type;
	msg->resp = resp;
	msg->transient = transient;

	if (dest_userid) {
		str_ncpy(msg->dest_userid, dest_userid, ECONN_ID_LEN);
	}

	if (dest_clientid) {
		str_ncpy(msg->dest_clientid, dest_clientid, ECONN_ID_LEN);
	}

	if (type == ECONN_CONF_START) {
		msg->u.confstart.timestamp = ccall->sft_timestamp;
		msg->u.confstart.seqno = ccall->sft_seqno;
		msg->u.confstart.secret = mem_zalloc(ccall->secret_len, NULL);
		if (!msg->u.confstart.secret) {
			err = ENOMEM;
			goto out;
		}
		memcpy(msg->u.confstart.secret, ccall->secret, ccall->secret_len);
		msg->u.confstart.secretlen = ccall->secret_len;
		err = str_dup(&msg->u.confstart.sft_url, ccall->primary_sft_url);
		if (err) {
			goto out;
		}
		if (ccall->sft_tuple) {
			err = str_dup(&msg->u.confstart.sft_tuple, ccall->sft_tuple);
			if (err) {
				goto out;
			}
		}
		str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);

		if (list_count(&ccall->sftl) > 0) {
			stringlist_clone(&ccall->sftl, &msg->u.confstart.sftl);
		}
		else {
			stringlist_append(&msg->u.confstart.sftl, ccall->primary_sft_url);
		}

		/* Add videosend prop */
		err = econn_props_alloc(&msg->u.confstart.props, NULL);
		if (err)
			goto skipprops;

		err = econn_props_add(msg->u.confstart.props, "videosend",
			ccall->call_type == ICALL_CALL_TYPE_VIDEO ? "true"
				                                   : "false");
		if (err)
			goto skipprops;
	}
	else if (type == ECONN_CONF_CHECK) {
		msg->u.confcheck.timestamp = ccall->sft_timestamp;
		msg->u.confcheck.seqno = ccall->sft_seqno;
		msg->u.confcheck.secret = mem_alloc(ccall->secret_len, NULL);
		if (!msg->u.confcheck.secret) {
			err = ENOMEM;
			goto out;
		}
		memcpy(msg->u.confcheck.secret, ccall->secret, ccall->secret_len);
		msg->u.confcheck.secretlen = ccall->secret_len;
		err = str_dup(&msg->u.confcheck.sft_url, ccall->primary_sft_url);
		if (err) {
			goto out;
		}
		if (ccall->sft_tuple) {
			err = str_dup(&msg->u.confcheck.sft_tuple, ccall->sft_tuple);
			if (err) {
				goto out;
			}
		}
		if (list_count(&ccall->sftl) > 0) {
			stringlist_clone(&ccall->sftl, &msg->u.confcheck.sftl);
		}
		else {
			stringlist_append(&msg->u.confcheck.sftl, ccall->primary_sft_url);
		}

		str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);
	}
	else if (type == ECONN_CONF_KEY) {
		if (ccall->is_mls_call) {
			warning("ccall(%p): alloc_message: request to send CONFKEY for mls call\n", ccall);
			err = EINVAL;
			goto out;
		}

		if (resp) {
			struct econn_key_info *key0 = NULL;
			struct econn_key_info *key1 = NULL;

			key0 = econn_key_info_alloc(E2EE_SESSIONKEY_SIZE);
			if (!key0) {
				err = ENOMEM;
				goto out;
			}
			err = keystore_get_current_session_key(ccall->keystore,
							       &key0->idx,
							       key0->data,
							       key0->dlen);
			if (err) {
				warning("ccall(%p): failed to get current key to send "
					"(%m) is_keygenerator %s\n",
					ccall,
					err,
					userlist_is_keygenerator_me(ccall->userl) ? "YES" : "NO");
				mem_deref(key0);
				goto out;
			}
			list_append(&msg->u.confkey.keyl, &key0->le, key0);

			key1 = econn_key_info_alloc(E2EE_SESSIONKEY_SIZE);
			if (!key1) {
				err = ENOMEM;
				goto out;
			}
			err = keystore_get_next_session_key(ccall->keystore,
							    &key1->idx,
							    key1->data,
							    key1->dlen);
			if (err) {
				/* No future key is OK */
				err = 0;
				mem_deref(key1);
			}
			else {
				list_append(&msg->u.confkey.keyl, &key1->le, key1);
			}

			info("ccall(%p): alloc_message: send CONFKEY resp with %u keys\n",
			     ccall, list_count(&msg->u.confkey.keyl));
		}
		else {
			info("ccall(%p): alloc_message: send CONFKEY req\n", ccall);
		}
	}
	else if (type == ECONN_CONF_END) {
		str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);
	}
	else if (type == ECONN_CONF_STREAMS) {
		str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);
	}
out:
skipprops:
	if (err) {
		mem_deref(msg);
	}
	else {
		*msgp = msg;
	}

	return err;
}

static int  ccall_send_msg_i(struct ccall *ccall,
			     enum econn_msg type,
			     bool resp,
			     struct list *targets,
			     bool transient,
			     bool my_clients_only)
{
	struct econn_message *msg = NULL;
	const struct userinfo *self = NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	int err = 0;

	if (!ccall)
		return EINVAL;

	self = userlist_get_self(ccall->userl);
	if (!self)
		return ENOENT;

	if (targets || my_clients_only) {
		info("ccall(%p): send_msg type: %s targets: %zu my_clients_only: %s\n",
		     ccall,
		     econn_msg_name(type),
		     list_count(targets),
		     my_clients_only ? "YES" : "NO");
	}
	else {
		info("ccall(%p): send_msg type: %s userid=%s clientid=%s\n",
		     ccall,
		     econn_msg_name(type),
		     anon_id(userid_anon, self->userid_real),
		     anon_client(clientid_anon, self->clientid_real));
	}

	err = alloc_message(&msg, ccall, type, resp,
			    self->userid_real, self->clientid_real,
			    NULL, NULL,
			    transient);

	if (err) {
		warning("ccall(%p): failed to alloc message: %m\n", ccall, err);
		goto out;
	}

	err = ICALL_CALL_CBE(ccall->icall, sendh,
			     &ccall->icall,
			     self->userid_real,
			     msg, targets,
			     my_clients_only,
			     ccall->icall.arg);

	if (err != 0) {
		goto out;
	}

out:
	mem_deref(msg);

	return err;
}

static int  ccall_send_msg(struct ccall *ccall,
			   enum econn_msg type,
			   bool resp,
			   struct list *targets,
			   bool transient)
{
	return ccall_send_msg_i(ccall, type, resp, targets, transient, false);
}

static int  ccall_send_conf_conn(struct ccall *ccall,
				 const char *sft_url,
				 const char *sft_username,
				 const char *sft_credential,
				 bool update)
{
	enum econn_msg type = ECONN_CONF_CONN;
	bool resp = false;
	struct econn_message *msg = NULL;
	struct zapi_ice_server *turnv;
	size_t turnc;
	char *sft_url_term = NULL;
	const struct userinfo *self = NULL;
	int err = 0;

	if (!ccall || !sft_url) {
		return EINVAL;
	}

	self = userlist_get_self(ccall->userl);
	if (!self)
		return ENOENT;

	err = copy_sft(&sft_url_term, sft_url);
	if (err)
		return err;

	info("ccall(%p): send_msg_sft url: %s type: %s resp: %s\n",
	     ccall,
	     sft_url,
	     econn_msg_name(type),
	     resp ? "YES" : "NO");

	err = alloc_message(&msg, ccall, type, resp,
			    self->userid_hash, self->clientid_hash,
			    "SFT", "_",
			    false);
	if (err) {
		goto out;
	}

	str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);
	turnv = config_get_sfticeservers(ccall->cfg, &turnc);
	if (turnv) {
		struct zapi_ice_server *turns;
		size_t i;
		
		turns = mem_zalloc(sizeof(*turns)*turnc, NULL);
		for (i = 0; i < turnc; ++i)
			turns[i] = turnv[i];
		turnv = turns;
	}
	else {
		turnv = mem_ref(ccall->turnv);
		turnc = ccall->turnc;
	}
	msg->u.confconn.turnv = turnv;
	msg->u.confconn.turnc = turnc;

	err = str_dup(&msg->u.confconn.tool, avs_version_str());
	if (err) {
		goto out;
	}
	err = str_dup(&msg->u.confconn.toolver, avs_version_short());
	if (err) {
		goto out;
	}
	msg->u.confconn.env = msystem_get_env();
	
	msg->u.confconn.update = update;
	msg->u.confconn.selective_audio = true;
	msg->u.confconn.selective_video = true;
	msg->u.confconn.vstreams = CCALL_MAX_VSTREAMS;

	if (ccall->primary_sft_url && 
	    strcmp(ccall->primary_sft_url, sft_url_term) != 0) {
		info("ccall(%p): send_msg_sft setting primary_sft_url %s\n",
		     ccall, ccall->primary_sft_url);
		err = str_dup(&msg->u.confconn.sft_url, ccall->primary_sft_url);
		if (err) {
			goto out;
		}
		if (ccall->sft_tuple) {
			info("ccall(%p): send_msg_sft setting sft_tuple %s\n",
			     ccall, ccall->sft_tuple);
			err = str_dup(&msg->u.confconn.sft_tuple, ccall->sft_tuple);
			if (err) {
				goto out;
			}
		}
	}

	if (sft_username && strlen(sft_username) > 0 &&
	    sft_credential && strlen(sft_credential) > 0) {
		err = str_dup(&msg->u.confconn.sft_username, sft_username);
		if (err) {
			goto out;
		}
		err = str_dup(&msg->u.confconn.sft_credential, sft_credential);
		if (err) {
			goto out;
		}
	}

	err = ccall_send_msg_sft(ccall, sft_url_term, msg);
	if (err != 0) {
		goto out;
	}

out:
	mem_deref(msg);
	mem_deref(sft_url_term);

	return err;
}

static int create_ecall(struct ccall *ccall)
{
	struct ecall *ecall = NULL;
	struct msystem *msys = msystem_instance();
	const struct userinfo *self = NULL;
	size_t i;
	int err = 0;

	if (!ccall)
		return EINVAL;

	assert(ccall->ecall == NULL);

	self = userlist_get_self(ccall->userl);
	if (!self)
		return ENOENT;

	if (ccall->call_type == ICALL_CALL_TYPE_NORMAL) {
		switch(ccall->vstate) {
		case ICALL_VIDEO_STATE_STARTED:
		case ICALL_VIDEO_STATE_SCREENSHARE:
			ccall->call_type = ICALL_CALL_TYPE_VIDEO;
			break;

		default:
			break;
		}
	}

	err = ecall_alloc(&ecall, NULL,
			  ICALL_CONV_TYPE_CONFERENCE,
			  ccall->call_type,
			  ccall->conf,
			  msys,
			  ccall->convid_real,
			  self->userid_hash,
			  self->clientid_hash);

	if (err) {
		goto out;
	}

	icall_set_callbacks(ecall_get_icall(ecall),
			    ecall_transp_send_handler,
			    NULL, // sft_handler,
			    ecall_setup_handler, 
			    ecall_setup_resp_handler,
			    ecall_media_estab_handler,
			    ecall_audio_estab_handler,
			    ecall_datachan_estab_handler,
			    NULL, // ecall_media_stopped_handler,
			    NULL, // group_changed_handler
			    NULL, // leave_handler
			    ecall_close_handler,
			    NULL, // metrics_handler
			    NULL, // ecall_vstate_handler,
			    NULL, // ecall_audiocbr_handler,
			    NULL, // muted_changed_handler,
			    ecall_quality_handler,
			    NULL, // ecall_req_clients_handler,
			    NULL, // ecall_norelay_handler,
			    ecall_aulevel_handler,
			    NULL, // ecall_req_new_epoch_handler,
			    ccall);

	ecall_set_confmsg_handler(ecall, ecall_confmsg_handler);
	ecall_set_propsync_handler(ecall, ecall_propsync_handler);
	ecall_set_ping_handler(ecall, ecall_ping_handler);
	ecall_set_keystore(ecall, ccall->keystore);
	err = ecall_set_quality_interval(ecall, ccall->quality_interval);
	if (err)
		goto out;

	err = ecall_set_real_clientid(ecall, self->clientid_real);
	if (err)
		goto out;

	for (i = 0; i < ccall->turnc; ++i) {
		err = ecall_add_turnserver(ecall, &ccall->turnv[i]);
		if (err)
			goto out;
	}

	if (ccall->convid_hash)
		ecall_set_sessid(ecall, ccall->convid_hash);

	ccall->ecall = ecall;
	tmr_start(&ccall->tmr_vstate,
		  0,
		  ccall_sync_vstate_timeout, ccall);

	info("ccall(%p): created ecall: %p for %s.%s\n", ccall, ecall, self->userid_hash, self->clientid_hash);

out:
	if (err) {
		mem_deref(ecall);
	}

	return err;
}

static void userlist_add_user_handler(const struct userinfo *user,
				      void *arg)
{
	struct ccall *ccall = arg;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!ccall || !user)
		return;

	info("ccall(%p): add_decoders_for_user: ecall: %p "
		"u: %s.%s a: %u v: %u\n",
		ccall, ccall->ecall,
		anon_id(userid_anon, user->userid_real),
		anon_client(clientid_anon, user->clientid_real),
		user->ssrca, user->ssrcv);

	if (ccall->ecall)
		ecall_add_decoders_for_user(ccall->ecall,
					    user->userid_real,
					    user->clientid_real,
					    user->userid_hash,
					    user->ssrca,
					    user->ssrcv);

	if (user->video_state != ICALL_VIDEO_STATE_STOPPED &&
	    user->ssrcv > 0) {
		ICALL_CALL_CB(ccall->icall, vstate_changedh,
			      &ccall->icall, user->userid_real,
			      user->clientid_real,
			      user->video_state,
			      ccall->icall.arg);
	}
}

static void userlist_remove_user_handler(const struct userinfo *user,
					 bool call_vstateh,
					 void *arg)
{
	struct ccall *ccall = arg;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!ccall || !user)
		return;

	info("ccall(%p): remove_decoders_for_user: ecall: %p "
		"u: %s.%s\n",
		ccall, ccall->ecall,
		anon_id(userid_anon, user->userid_real),
		anon_client(clientid_anon, user->clientid_real));
	ccall->someone_left = true;
	if (ccall->ecall) {
		ecall_remove_decoders_for_user(ccall->ecall,
					       user->userid_real,
					       user->clientid_real,
					       user->ssrca,
					       user->ssrcv);
	}

	if (call_vstateh && user->video_state != ICALL_VIDEO_STATE_STOPPED) {
		ICALL_CALL_CB(ccall->icall, vstate_changedh,
			      &ccall->icall, user->userid_real,
			      user->clientid_real,
			      ICALL_VIDEO_STATE_STOPPED,
			      ccall->icall.arg);
	}
}

static void userlist_sync_users_handler(void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall)
		return;

	if (ccall->ecall)
		ecall_sync_decoders(ccall->ecall);

	ccall_update_active_counts(ccall);
}

static void userlist_kg_change_handler(struct userinfo *keygenerator,
				       bool is_me,
				       bool is_first,
				       void *arg)
{
	struct ccall *ccall = arg;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	uint32_t kid = 0;
	uint64_t updated_ts = 0;
	int err = 0;

	if (!ccall || !ccall->keystore) {
		return;
	}

	if (is_me) {
		info("ccall(%p): track_keygenerator: new keygenerator is me\n",
		      ccall);

		if (!ccall->is_mls_call) {
			err = keystore_get_current(ccall->keystore, &kid, &updated_ts);
			if (err == ENOENT) {
				/* no current key, generate and send */
				info("ccall(%p): track_keygenerator: generate initial keys\n",
				      ccall);
				ccall_generate_session_key(ccall, true);
			}
			else {
				ccall->became_kg = true;
			}
			tmr_start(&ccall->tmr_rotate_key,
				  CCALL_ROTATE_KEY_FIRST_TIMEOUT,
				  ccall_rotate_key_timeout, ccall);
		}

		tmr_start(&ccall->tmr_send_check,
			  is_first ? CCALL_SEND_CHECK_TIMEOUT : 0,
			  ccall_send_check_timeout, ccall);
	}
	else {
		info("ccall(%p): track_keygenerator: new keygenerator is %s.%s\n",
		      ccall,
		      anon_id(userid_anon, keygenerator->userid_real),
		      anon_client(clientid_anon, keygenerator->clientid_real));

		if (ccall->request_key) {
			info("ccall(%p): requesting key resend from %s.%s\n",
			     ccall,
			     anon_id(userid_anon, keygenerator->userid_real),
			     anon_client(clientid_anon, keygenerator->clientid_real));
			ccall_request_keys(ccall);
			ccall->request_key = false;
		}

		tmr_cancel(&ccall->tmr_send_check);
		tmr_cancel(&ccall->tmr_rotate_key);
	}

}

static void userlist_vstate_handler(const struct userinfo *user,
				    enum icall_vstate new_vstate,
				    void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall || !user) {
		return;
	}

	ICALL_CALL_CB(ccall->icall, vstate_changedh,
		      &ccall->icall, user->userid_real,
		      user->clientid_real, ICALL_VIDEO_STATE_STOPPED,
		      ccall->icall.arg);
}

int ccall_alloc(struct ccall **ccallp,
		const struct ecall_conf *conf,		 
		const char *convid,
		const char *userid_self,
		const char *clientid,
		bool is_mls_call)
{
	char convid_anon[ANON_ID_LEN];
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct ccall *ccall = NULL;
	uint8_t secret[CCALL_SECRET_LEN];

	int err = 0;

	if (convid == NULL || ccallp == NULL) {
		return EINVAL;
	}


	ccall = mem_zalloc(sizeof(*ccall), destructor);
	if (ccall == NULL) {
		return ENOMEM;
	}

	info("ccall(%p): alloc convid: %s userid: %s"
	     " clientid: %s is_mls: %s\n",
	     ccall,
	     anon_id(convid_anon, convid),
	     anon_id(userid_anon, userid_self),
	     anon_client(clientid_anon, clientid),
	     is_mls_call ? "true" : "false");

	err = userlist_alloc(&ccall->userl,
			     userid_self,
			     clientid,
			     userlist_add_user_handler,
			     userlist_remove_user_handler,
			     userlist_sync_users_handler,
			     userlist_kg_change_handler,
			     userlist_vstate_handler,
			     ccall);
	if (err)
		goto out;

	err = str_dup(&ccall->convid_real, convid);
	if (err)
		goto out;

	ccall->turnv = mem_zalloc(MAX_TURN_SERVERS * sizeof(*ccall->turnv), NULL);
	if (!ccall->turnv) {
		err = ENOMEM;
		goto out;
	}

	err = keystore_alloc(&ccall->keystore, !is_mls_call);
	if (err)
		goto out;

	randombytes_buf(&secret, CCALL_SECRET_LEN);
	info("ccall(%p) set_secret from alloc\n", ccall);
	ccall_set_secret(ccall, secret, CCALL_SECRET_LEN);

	mbuf_init(&ccall->confpart_data);
	ccall->conf = conf;
	ccall->state = CCALL_STATE_IDLE;
	ccall->stop_ringing_reason = CCALL_STOP_RINGING_NONE;
	ccall->is_mls_call = is_mls_call;
	ccall->metrics.conv_type = is_mls_call ? ICALL_CONV_TYPE_CONFERENCE_MLS : ICALL_CONV_TYPE_CONFERENCE;

	tmr_init(&ccall->tmr_connect);
	tmr_init(&ccall->tmr_call);
	tmr_init(&ccall->tmr_send_check);
	tmr_init(&ccall->tmr_ongoing);
	tmr_init(&ccall->tmr_ring);
	tmr_init(&ccall->tmr_sft_reject);
	tmr_init(&ccall->tmr_vstate);

	icall_set_functions(&ccall->icall,
			    ccall_add_turnserver,
			    ccall_add_sft,
			    ccall_start,
			    ccall_answer,
			    ccall_end,
			    ccall_reject,
			    ccall_media_start,
			    ccall_media_stop,
			    NULL, // ccall_set_media_laddr
			    ccall_set_vstate,
			    ccall_msg_recv,
			    ccall_sft_msg_recv,
			    ccall_get_members,
			    ccall_set_quality_interval,
			    NULL, // ccall_dce_send
			    ccall_set_clients,
			    ccall_update_mute_state,
			    ccall_request_video_streams,
			    ccall_set_media_key,
			    ccall_debug,
			    ccall_stats,
			    ccall_set_background,
			    ccall_activate,
			    ccall_restart);
out:
	if (err == 0) {
		*ccallp = ccall;
	}
	else {
		mem_deref(ccall);
	}

	return err;
}

struct icall *ccall_get_icall(struct ccall *ccall)
{
	return &ccall->icall;
}

int  ccall_add_turnserver(struct icall *icall,
			  struct zapi_ice_server *srv)
{
	struct ccall *ccall = (struct ccall*)icall;
	int err = 0;

	if (!ccall || !srv)
		return EINVAL;

	if (ccall->turnc >= MAX_TURN_SERVERS)
		return EOVERFLOW;
	
	ccall->turnv[ccall->turnc] = *srv;
	++ccall->turnc;

	return err;
}

int  ccall_add_sft(struct icall *icall, const char *sft_url)
{
	return 0;
}

static struct zapi_ice_server* ccall_get_sft_info(struct ccall *ccall,
						  const char *sft_url)
{
	struct zapi_ice_server *sftv = NULL;
	size_t sft = 0, sftc = 0;

	if (!ccall || !sft_url) {
		return NULL;
	}

	sftv = config_get_sftservers_all(ccall->cfg, &sftc);
	if (sftv) {
		for (sft = 0; sft < sftc; sft++) {
			if (sfts_equal(sft_url, sftv[sft].url)) {
				return &sftv[sft];
			}
		}
	}
	return NULL;
}

static struct zapi_ice_server* ccall_get_sft_info_at_index(struct ccall *ccall,
							   int ix)
{
	struct zapi_ice_server *sftv = NULL;
	size_t sftc = 0;

	if (!ccall || ix < 0) {
		return NULL;
	}

	sftv = config_get_sftservers_all(ccall->cfg, &sftc);
	if (sftv) {
		if ((size_t)ix >= sftc)
			return NULL;

		return &sftv[ix];
	}
	return NULL;
}


static bool ccall_can_connect_sft(struct ccall *ccall, const char *sft_url)
{
	size_t sftc = 0;

	if (!ccall || !sft_url) {
		return false;
	}

	config_get_sftservers_all(ccall->cfg, &sftc);

	info("ccall(%p): can_connect %zu sfts in sft_servers_all\n",
		ccall, sftc);
	if (sftc == 0) {
		/* If no sfts in config, assume legacy behaviour:
		   we can connect to all SFTs */
		return true;
	}

	return ccall_get_sft_info(ccall, sft_url) != NULL;
}

static void config_update_handler(struct call_config *cfg, void *arg)
{
	struct join_elem *je = arg;
	struct ccall *ccall = je->ccall;
	struct zapi_ice_server *urlv;
	size_t urlc, sft;
	int state = ccall->state;
	struct le *le;
	struct zapi_ice_server *sfti = NULL;
	struct zapi_ice_server tmp_sft;
	bool deref_je = true;
	int err = 0;

	if (!ccall) {
		return;
	}
	if (je != ccall->je) {
		/* This is a callback from a previous attempt, ignore it */
		info("ccall(%p): cfg_update ignoring old update %p %p\n",
		     ccall, je, ccall->je);
		return;
	}
	urlv = config_get_sftservers(ccall->cfg, &urlc);

	info("ccall(%p): cfg_update received %zu sfts state: %s federating: %s\n",
	     ccall,
	     urlc,
	     ccall_state_name(ccall->state),
	     config_is_federating(ccall->cfg) ? "YES" : "NO");
	
	if (!urlv || !urlc) {
		warning("ccall(%p): no SFT server configured\n", ccall);
		ccall_end_with_err(ccall, ENOTSUP);
		goto out;
	}

	if (CCALL_STATE_WAITCONFIG != ccall->state &&
	    CCALL_STATE_WAITCONFIG_OUTGOING != ccall->state) {
		warning("ccall(%p): cfg_update in state %s, ignoring\n",
			ccall, ccall_state_name(ccall->state));
		goto out;
	}

	/* Prefer connecting to an already active sft */
	if (CCALL_STATE_WAITCONFIG == state) {
		if (list_count(&ccall->sftl) > 0) {
			uint32_t connected = 0;

			info("ccall(%p): cfg_update checking %u sfts from sft list\n",
			     ccall, list_count(&ccall->sftl));
			LIST_FOREACH(&ccall->sftl, le) {
				struct stringlist_info *nfo = le->data;

				if (ccall_can_connect_sft(ccall, nfo->str)) {
					sfti = ccall_get_sft_info(ccall, nfo->str);
					err = ccall_send_conf_conn(ccall,
								   nfo->str,
								   sfti ? sfti->username : NULL,
								   sfti ? sfti->credential : NULL,
								   false);
					if (err) {
						warning("ccall(%p): cfg_update failed to send "
							"confconn to sft %s err=%d\n",
							ccall, nfo->str, err);
					}
					else {
						info("ccall(%p): cfg_update connecting to %s "
							"from SFT list\n",
							ccall, nfo->str);
						connected++;
					}
				}

				if (connected >= 3)
					break;
			}

			if (connected > 0)
				goto out;
		}
		else if (ccall->primary_sft_url) {
			// legacy behaviour, connect to primary
			if (ccall_can_connect_sft(ccall, ccall->primary_sft_url)) {
				sfti = ccall_get_sft_info(ccall, ccall->primary_sft_url);
				err = ccall_send_conf_conn(ccall,
							   ccall->primary_sft_url,
							   sfti ? sfti->username : NULL,
							   sfti ? sfti->credential : NULL,
							   false);
				if (err) {
					warning("ccall(%p): cfg_update failed to send "
						"confconn to primary sft %s err=%d\n",
						ccall, ccall->primary_sft_url, err);
				}
				else {
					info("ccall(%p): cfg_update connecting to primary sft "
						"%s for legacy behaviour\n",
						ccall, ccall->primary_sft_url);
				}
				goto out;
			}
		}

		if (!config_is_federating(ccall->cfg)) {
			warning("ccall(%p): cfg_update not federating and no "
				"allowed SFTs to join (primary %s) is retry %s\n",
				ccall,
				ccall->primary_sft_url ? ccall->primary_sft_url : "none",
				je->retry_attempt ? "YES" : "NO");
			if (je->retry_attempt) {
				ccall_end_with_err(ccall, EACCES);
				err = EACCES;
			}
			else {
				err = ccall_req_cfg_join(ccall,
							 je->call_type,
							 je->audio_cbr,
							 true,
							 CCALL_STATE_WAITCONFIG_OUTGOING == ccall->state);
				if (err) {
					warning("ccall(%p): cfg_update error retrying "
						"config err=%d\n",
						ccall, err);
					goto out;
				}

				info("ccall(%p): cfg_update regetting config",
				     ccall);
				// Dont mem_deref je as it has been reassigned
				deref_je = false;
				err = EAGAIN;
			}
			goto out;
		}
	}

	urlc = MIN(urlc, 3);
	for (sft = 0; sft < urlc; sft++) {
		struct zapi_ice_server *si;

		si = ccall_get_sft_info(ccall, urlv[sft].url);
		if (si)
			sfti = si;
		else {
			si = ccall_get_sft_info_at_index(ccall, 0);
			if (si) {
				tmp_sft = *si;
				str_ncpy(tmp_sft.url,
					 urlv[sft].url,
					 sizeof(tmp_sft.url));
				sfti = &tmp_sft;				
			}
			else {
				sfti = &urlv[sft];
			}
		}

		/* If one SFT fails, keep trying the rest */
		info("ccall(%p): cfg_update connecting to sft "
			"%s user %s cred %s \n",
			ccall, 
			sfti->url,
			sfti->username,
			sfti->credential);

		err = ccall_send_conf_conn(ccall,
					   sfti->url,
					   sfti->username,
					   sfti->credential,
					   false);
		if (err) {
			warning("ccall(%p): cfg_update failed to send "
				"confconn to sft %s err=%d\n",
				ccall, sfti->url, err);
		}
		else {
			info("ccall(%p): cfg_update connecting to %s "
				"from calls/config\n",
				ccall, sfti->url);
		}
	}
 out:
	if (!err) {
		set_state(ccall, CCALL_STATE_CONNSENT);
		ccall->call_type = je->call_type;

		ccall_stop_others_ringing(ccall);
	}

	if (deref_je)
		ccall->je = mem_deref(ccall->je);
}

static void je_destructor(void *arg)
{
	struct join_elem *je = arg;

	config_unregister_update_handler(&je->upe);
}

static int  ccall_req_cfg_join(struct ccall *ccall,
			       enum icall_call_type call_type,
			       bool audio_cbr,
			       bool retry_attempt,
			       bool is_outgoing)
{
	struct join_elem *je;
	int err;

	info("ccall(%p): req_cfg type %d retry %s outgoing %s\n",
	     ccall,
	     call_type,
	     retry_attempt ? "YES" : "NO",
	     is_outgoing ? "YES" : "NO");

	je = mem_zalloc(sizeof(*je), je_destructor);
	if (!je)
		return ENOMEM;

	if (ccall->je) {
		ccall->je = mem_deref(ccall->je);
	}

	set_state(ccall, is_outgoing ? CCALL_STATE_WAITCONFIG_OUTGOING :
				       CCALL_STATE_WAITCONFIG);
	ccall->je = je;

	je->ccall = ccall;
	je->call_type = call_type;
	je->audio_cbr = audio_cbr;
	je->retry_attempt = retry_attempt;
	je->upe.updh = config_update_handler;
	je->upe.arg = je;

	err = config_register_update_handler(&je->upe, ccall->cfg);
	if (err)
		goto out;

	err = config_request(ccall->cfg);

 out:
	return err;
}

int  ccall_start(struct icall *icall,
		 enum icall_call_type call_type,
		 bool audio_cbr)
{
	struct ccall *ccall = (struct ccall*)icall;
	int err;

	ccall->error = 0;
	switch (ccall->state) {
	case CCALL_STATE_INCOMING:
		warning("ccall(%p): call_start attempt to start call in "
			" state %s, answering instead\n",
			ccall, ccall_state_name(ccall->state));
		return ccall_answer(icall, call_type, audio_cbr);

	case CCALL_STATE_IDLE:
		ccall->is_caller = true;
		err = ccall_req_cfg_join(ccall, call_type, audio_cbr, false, true);
		break;

	default:
		warning("ccall(%p): call_start attempt to start call in state %s\n",
			ccall, ccall_state_name(ccall->state));
		return 0;
	}

	return err;
}

int  ccall_answer(struct icall *icall,
		  enum icall_call_type call_type,
		  bool audio_cbr)
{
	struct ccall *ccall = (struct ccall*)icall;
	int err = 0;

	ccall->error = 0;
	switch (ccall->state) {
	case CCALL_STATE_IDLE:
		warning("ccall(%p): call_answer attempt to answer call in "
			"state %s, trying anyway\n",
			ccall, ccall_state_name(ccall->state));
		/* drop through */
	case CCALL_STATE_INCOMING:
		ccall->is_caller = false;
		ccall->stop_ringing_reason = CCALL_STOP_RINGING_ANSWERED;
		err = ccall_req_cfg_join(ccall, call_type, audio_cbr, false, false);
		break;

	default:
		warning("ccall(%p): call_answer attempt to answer call in state %s\n",
			ccall, ccall_state_name(ccall->state));
		return EINVAL;
	}

	return err;
}

void ccall_end(struct icall *icall)
{
	struct ccall *ccall = (struct ccall*)icall;
	if (!ccall)
		return;

	if (ccall->ecall)
		ecall_end(ccall->ecall);
}

void ccall_reject(struct icall *icall)
{
	struct ccall *ccall = (struct ccall*)icall;
	if (!ccall)
		return;

	info("ccall(%p): reject state=%s\n", ccall,
	     ccall_state_name(ccall->state));

	if (ccall->state == CCALL_STATE_INCOMING) {
		ccall->stop_ringing_reason = CCALL_STOP_RINGING_REJECTED;
		ccall_stop_others_ringing(ccall);
	}
}

int  ccall_media_start(struct icall *icall)
{
	struct ccall *ccall = (struct ccall*)icall;

	ecall_media_start(ccall->ecall);
	
	return 0;
}

void ccall_media_stop(struct icall *icall)
{
	struct ccall *ccall = (struct ccall*)icall;

	ecall_media_stop(ccall->ecall);
}

int  ccall_set_vstate(struct icall *icall, enum icall_vstate state)
{
	struct ccall *ccall = (struct ccall*)icall;
	const struct userinfo *self = NULL;
	int err;

	if (!ccall)
		return EINVAL;

	info("ccall(%p) set_vstate ecall: %p state: %u\n",
	      ccall,
	      ccall->ecall,
	      state);

	ccall->vstate = state;

	self = userlist_get_self(ccall->userl);
	if (self) {
		ICALL_CALL_CB(ccall->icall, vstate_changedh, &ccall->icall,
			      self->userid_real,
			      self->clientid_real,
			      state, ccall->icall.arg);
	}

	ICALL_CALL_CB(ccall->icall, group_changedh,
		&ccall->icall, ccall->icall.arg);

	if (ccall->ecall) {
		err = ecall_set_video_send_state(ccall->ecall, state);
		if (err) {
			return err;
		}
	}

	return 0;
}

int  ccall_get_members(struct icall *icall, struct wcall_members **mmp)
{
	struct ccall *ccall = (struct ccall*)icall;
	enum icall_audio_state astate;

	if (!ccall)
		return EINVAL;

	astate = (CCALL_STATE_ACTIVE == ccall->state) ?
		 ICALL_AUDIO_STATE_ESTABLISHED :
		 ICALL_AUDIO_STATE_CONNECTING;
	return userlist_get_members(ccall->userl, mmp,
				    astate,
				    ccall->vstate);
}

int  ccall_set_quality_interval(struct icall *icall, uint64_t interval)
{
	struct ccall *ccall = (struct ccall*)icall;

	if (!ccall)
		return EINVAL;

	ccall->quality_interval = interval;

	if (ccall->ecall)
		ecall_set_quality_interval(ccall->ecall, interval);
	
	return 0;
}

static void ccall_stop_others_ringing_i(struct ccall *ccall)
{
	struct list targets = LIST_INIT;
	enum econn_msg msgtype;
	int err = 0;

	if (!ccall)
		return;

	info("ccall(%p): stop_others_ringing reason=%d\n",
	     ccall,
	     ccall->stop_ringing_reason);
	switch (ccall->stop_ringing_reason) {
	case CCALL_STOP_RINGING_ANSWERED:
		msgtype = ECONN_CONF_START;
		break;

	case CCALL_STOP_RINGING_REJECTED:
		msgtype = ECONN_REJECT;
		break;

	default:
		goto out;
	}

	ccall->stop_ringing_reason = CCALL_STOP_RINGING_NONE;

	err = userlist_get_my_clients(ccall->userl, &targets);
	if (err) {
		warning("ccall(%p): stop_others_ringing get_my_clients failed (%m)\n",
			ccall, err);
		goto out;
	}
	info("ccall(%p): stop_others_ringing state=%s targets=%u\n",
	     ccall,
	     ccall_state_name(ccall->state),
	     list_count(&targets));

	if (ccall->is_mls_call || list_count(&targets) > 0) {
		ccall_send_msg_i(ccall, msgtype,
				 true, &targets,
				 false, true);
	}

out:
	list_flush(&targets);
}

static void ccall_stop_others_ringing(struct ccall *ccall)
{
	if (ccall->is_mls_call) {
		ccall_stop_others_ringing_i(ccall);
	}
	else {
		ICALL_CALL_CB(ccall->icall, req_clientsh,
			      &ccall->icall, ccall->icall.arg);
	}
}

void ccall_set_clients(struct icall* icall,
		       struct list *clientl,
		       uint32_t epoch)
{
	struct ccall *ccall = (struct ccall*)icall;
	bool list_changed = false;
	bool list_removed = false;
	int err = 0;

	if (!ccall || !ccall->userl)
		return;

	userlist_update_from_selist(ccall->userl,
				    clientl,
				    epoch,
				    ccall->secret,
				    ccall->secret_len,
				    &list_changed,
				    &list_removed);
	if (err) {
		warning("ccall(%p): set_clients err %M\n", ccall, err);
		return;
	}

	if (list_changed) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			      &ccall->icall, ccall->icall.arg);	
	}

	if (list_removed) {
		ccall->someone_left = true;
		tmr_start(&ccall->tmr_rotate_key,
			  CCALL_ROTATE_KEY_FAST_TIMEOUT,
			  ccall_rotate_key_timeout, ccall);
	}
	
	if (CCALL_STATE_ACTIVE == ccall->state) {
		if (list_changed) {
			info("ccall(%p): set_clients sending confpart "
			     " for new clients\n",
			     ccall);
			send_confpart_response(ccall);
		}
		ccall_send_keys(ccall, false);
	}

	if (ccall->stop_ringing_reason != CCALL_STOP_RINGING_NONE) {
		ccall_stop_others_ringing_i(ccall);
	}
}

int ccall_update_mute_state(const struct icall* icall)
{
	struct ccall *ccall = (struct ccall*)icall;

	if (!ccall)
		return EINVAL;

	ICALL_CALL_CB(ccall->icall, group_changedh,
		&ccall->icall, ccall->icall.arg);	

	if (ccall->ecall) {
		return ecall_update_mute_state(ccall->ecall);
	}

	return 0;
}

static int ccall_handle_confstart_check(struct ccall* ccall,
					uint32_t curr_time,
					uint32_t msg_time,
					const char *userid_sender,
					const char *clientid_sender,
					struct econn_message *msg)
{
	bool video = false;
	int ts_cmp = 0;
	uint64_t msg_ts;
	uint32_t msg_seqno, msg_secretlen;
	const char *msg_sft_url;
	const char *msg_sft_tuple;
	const uint8_t *msg_secret;
	bool valid_call, should_ring;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct list *sftl;
	const struct userinfo *self = NULL;
	int err = 0;

	if (!ccall || !msg ||
	    !userid_sender || !clientid_sender)
		return EINVAL;

	self = userlist_get_self(ccall->userl);
	if (!self)
		return ENOENT;

	if (ECONN_CONF_START == msg->msg_type) {
		msg_ts = msg->u.confstart.timestamp;
		msg_seqno = msg->u.confstart.seqno;
		msg_sft_url = msg->u.confstart.sft_url;
		msg_sft_tuple = msg->u.confstart.sft_tuple;
		msg_secret = msg->u.confstart.secret;
		msg_secretlen = msg->u.confstart.secretlen;
		sftl = &msg->u.confstart.sftl;

		valid_call = msg->age < CCALL_CONFSTART_TIMEOUT_S;
	}
	else if (ECONN_CONF_CHECK == msg->msg_type) {
		msg_ts = msg->u.confcheck.timestamp;
		msg_seqno = msg->u.confcheck.seqno;
		msg_sft_url = msg->u.confcheck.sft_url;
		msg_sft_tuple = msg->u.confcheck.sft_tuple;
		msg_secret = msg->u.confcheck.secret;
		msg_secretlen = msg->u.confcheck.secretlen;
		sftl = &msg->u.confcheck.sftl;

		valid_call = !econn_message_isrequest(msg) &&
			(msg->age < CCALL_CONFSTART_TIMEOUT_S);
	}
	else {
		return EINVAL;
	}

	if (!valid_call) {
		info("ccall(%p): handle_confstart_check ignore %s of age %u "
			"state: %s\n", ccall, econn_msg_name(msg->msg_type),
			msg->age, ccall_state_name(ccall->state));
		return 0;
	}

	if (ccall->sft_timestamp == 0 || msg_ts < ccall->sft_timestamp) {
		ts_cmp = 1;
	}
	else if (msg_ts > ccall->sft_timestamp) {
		ts_cmp = -1;
	}
	else {
		ts_cmp = ccall->sft_seqno - msg_seqno;
	}

	info("ccall(%p): handle_confstart_check %s "
	     "from: %s.%s state: %s ts_cmp: %d "
	     "(msg %llu.%u < local %llu.%u) is_keygenerator: %s\n",
	     ccall,
	     econn_msg_name(msg->msg_type),
	     anon_id(userid_anon, userid_sender),
	     anon_client(clientid_anon, clientid_sender),
	     ccall_state_name(ccall->state),
	     ts_cmp,
	     msg_ts, msg_seqno,
	     ccall->sft_timestamp, ccall->sft_seqno,
	     userlist_is_keygenerator_me(ccall->userl) ? "YES" : "NO");

	if (ts_cmp > 0) {
		ccall->primary_sft_url = mem_deref(ccall->primary_sft_url);
		info("ccall(%p): handle_confstart setting primary sft url: %s\n",
		     ccall, msg_sft_url);
		copy_sft(&ccall->primary_sft_url, msg_sft_url);
 		ccall->sft_tuple = mem_deref(ccall->sft_tuple);
 		if (msg_sft_tuple) {
 			err = str_dup(&ccall->sft_tuple, msg_sft_tuple);
 			if (err) {
				return err;
 			}
 		}
		ccall->sft_timestamp = msg_ts;
		ccall->sft_seqno = msg_seqno;
		info("ccall(%p) set_secret from confstart\n", ccall);
		keystore_reset(ccall->keystore);
		ccall_set_secret(ccall, msg_secret, msg_secretlen);
		ccall->is_caller = false;
		userlist_reset_keygenerator(ccall->userl);
	}

	switch (ccall->state) {
	case CCALL_STATE_IDLE:
		{
			should_ring = econn_message_isrequest(msg) &&
				ECONN_CONF_START == msg->msg_type &&
				((msg->age * 1000) < CCALL_SHOULD_RING_TIMEOUT);
			if (strcaseeq(userid_sender, self->userid_real)) {
				should_ring = false;
			}

			ccall->is_ringing = should_ring;
			set_state(ccall, CCALL_STATE_INCOMING);

			ICALL_CALL_CB(ccall->icall, starth,
				      &ccall->icall, msg->time,
				      userid_sender, clientid_sender,
				      video, should_ring,
				      ccall->is_mls_call ? ICALL_CONV_TYPE_CONFERENCE_MLS :
							   ICALL_CONV_TYPE_CONFERENCE,
				      ccall->icall.arg);

			if (should_ring) {
				tmr_start(&ccall->tmr_ring,
					  CCALL_RINGER_TIMEOUT,
					  ccall_stop_ringing_timeout, ccall);
			}

			if (ts_cmp >= 0)
				stringlist_clone(sftl, &ccall->sftl);
		}
		break;

	case CCALL_STATE_INCOMING:
		if (strcaseeq(userid_sender, self->userid_real) &&
		    ccall->is_ringing) {
			tmr_cancel(&ccall->tmr_ring);
			ICALL_CALL_CB(ccall->icall, leaveh,
				      &ccall->icall, ICALL_REASON_STILL_ONGOING,
				      ECONN_MESSAGE_TIME_UNKNOWN, ccall->icall.arg);
			ccall->is_ringing = false;
		}
		tmr_cancel(&ccall->tmr_ongoing);
		tmr_start(&ccall->tmr_ongoing, CCALL_ONGOING_CALL_TIMEOUT,
			  ccall_ongoing_call_timeout, ccall);

		if (ts_cmp >= 0)
			stringlist_clone(sftl, &ccall->sftl);
		break;

	case CCALL_STATE_CONNSENT:
	case CCALL_STATE_SETUPRECV:
	case CCALL_STATE_CONNECTING:
	case CCALL_STATE_CONNECTED:
	case CCALL_STATE_WAITCONFIG:
	case CCALL_STATE_WAITCONFIG_OUTGOING:
		if (ts_cmp > 0) {
			/* If remote call is earlier, drop connection and
			   reconnect to the earlier call */
			ecall_end(ccall->ecall);
			ccall->ecall = NULL;
			ccall->metrics.initiator = false;

			stringlist_clone(sftl, &ccall->sftl);
			return ccall_req_cfg_join(ccall, ccall->call_type, true, false, false);
		}
		break;

	case CCALL_STATE_ACTIVE:
		if (ts_cmp > 0) {
			/* If remote call is earlier, drop connection and
			   reconnect to the earlier call */
			ecall_end(ccall->ecall);
			ccall->ecall = NULL;

			stringlist_clone(sftl, &ccall->sftl);
			return ccall_req_cfg_join(ccall, ccall->call_type, true, false, false);
		}
		else if (ts_cmp < 0 && userlist_is_keygenerator_me(ccall->userl)) {
			/* If local call is earlier. send new CONFSTART to
			   assert dominance */
			info("ccall(%p): sending CONFSTART from incoming CONFSTART\n", ccall);
			ccall_send_msg(ccall, ECONN_CONF_START,
				       true, NULL, false);
		}
		break;

	case CCALL_STATE_NONE:
	case CCALL_STATE_TERMINATING:
		break;
	}

	return 0;
}

int  ccall_msg_recv(struct icall* icall,
		    uint32_t curr_time,
		    uint32_t msg_time,
		    const char *userid_sender,
		    const char *clientid_sender,
		    struct econn_message *msg)
{
	struct ccall *ccall = (struct ccall*)icall;
	char userid_anon[ANON_ID_LEN];
	char userid_anon2[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char clientid_anon2[ANON_CLIENT_LEN];
	struct le *le;
	int err = 0;

	if (!ccall || !msg) {
		return EINVAL;
	}

	switch (msg->msg_type) {
	case ECONN_CONF_START:
	case ECONN_CONF_CHECK:
		return ccall_handle_confstart_check(ccall, curr_time, msg_time,
			userid_sender, clientid_sender, msg);
		break;

	case ECONN_CONF_END:
		switch (ccall->state) {
		case CCALL_STATE_INCOMING:
			if (strncmp(msg->sessid_sender, ccall->convid_hash,
				    ECONN_ID_LEN) == 0) {
				set_state(ccall, CCALL_STATE_IDLE);
				ICALL_CALL_CB(ccall->icall, closeh, 
					      &ccall->icall, 0, &ccall->metrics,
					      ECONN_MESSAGE_TIME_UNKNOWN,
					      NULL, NULL, ccall->icall.arg);
			}
			else {
				info("ccall(%p): msg_recv ignoring CONFEND "
				     "from wrong session\n", ccall);
			}
			break;

		case CCALL_STATE_ACTIVE:
			/* Inform clients that the call is in fact still ongoing */
			if (userlist_is_keygenerator_me(ccall->userl)) {
				tmr_cancel(&ccall->tmr_send_check);
				tmr_start(&ccall->tmr_send_check, 0,
					  ccall_send_check_timeout, ccall);
			}
			break;

		default:
			info("ccall(%p): msg_recv ignoring CONFEND "
			     "due to state %s\n", ccall, ccall_state_name(ccall->state));
			break;
		}
		break;

	case ECONN_CONF_KEY:
		if (msg->resp) {
			const struct userinfo *kg = userlist_get_keygenerator(ccall->userl);
			if (!kg) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s.%s when keygenerator is NULL!\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender));
				ccall->request_key = true;
				return 0;
			}

			if (userlist_is_keygenerator_me(ccall->userl)) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s.%s when keygenerator is me!\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender));
				return 0;
			}

			if (!kg->userid_real || !kg->clientid_real) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s.%s when keygenerator is invalid!\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender));
				return 0;
			}

			if (!strcaseeq(userid_sender,   kg->userid_real) ||
			    !strcaseeq(clientid_sender, kg->clientid_real)) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s.%s when keygenerator is %s.%s\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender),
					anon_id(userid_anon2, kg->userid_real),
					anon_client(clientid_anon2, kg->clientid_real));
				return 0;
			}

			keystore_set_salt(ccall->keystore,
					  (const uint8_t*)ccall->convid_hash,
					  ccall->convid_hash ? strlen(ccall->convid_hash) : 0);
			LIST_FOREACH(&msg->u.confkey.keyl, le) {
				struct econn_key_info *k = le->data;

				if (!k)
					continue;
				info("ccall(%p): process key idx %u len %u\n",
				     ccall, k->idx, k->dlen);

				err = keystore_set_session_key(ccall->keystore,
							       k->idx,
							       k->data,
							       k->dlen);
				if (err && err != EALREADY) {
					warning("ccall(%p): msg_recv failed to set key "
						"for index %u\n",
						ccall, k->idx);
					return 0;
				}
			}
		}
		else {
			struct userinfo *u = NULL;

	    		if (!userlist_is_keygenerator_me(ccall->userl)) {
				warning("ccall(%p): msg_recv ignoring CONFKEY req from "
					"%s.%s when not the keygenerator!\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender));
				return 0;
			}

			u = userlist_find_by_real(ccall->userl,
						  userid_sender,
						  clientid_sender);
			if (!u) {
				warning("ccall(%p): msg_recv ignoring CONFKEY req from "
					"%s.%s because their user not found!\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender));
				return 0;
			}
			if (!u->incall_now) {
				warning("ccall(%p): msg_recv ignoring CONFKEY req from "
					"%s.%s because they arent in call!\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_client(clientid_anon, clientid_sender));
				return 0;
			}
			u->needs_key = true;
			ccall_send_keys(ccall, false);
		}
		break;

	case ECONN_REJECT:
		{
			const struct userinfo *self = userlist_get_self(ccall->userl);
			if (self && ccall->state == CCALL_STATE_INCOMING &&
			    strcaseeq(userid_sender, self->userid_real)) {
				tmr_cancel(&ccall->tmr_ring);
				ICALL_CALL_CB(ccall->icall, leaveh,
					      &ccall->icall, ICALL_REASON_STILL_ONGOING,
					      ECONN_MESSAGE_TIME_UNKNOWN, ccall->icall.arg);
				ccall->is_ringing = false;
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

int  ccall_sft_msg_recv(struct icall* icall,
			int status,
		        struct econn_message *msg)
{
	struct ccall *ccall = (struct ccall*)icall;
	const char *userid_peer = "SFT";
	const char *clientid_peer = "SFT";
	int err = 0;

	info("ccall(%p): sft_msg_recv status=%d\n", ccall, status);
	if (status != 0 && status == SFT_STATUS_NETWORK_ERROR) {
		warning("ccall(%p): sft failed responded with failure: %d\n",
			ccall, status);

		ccall_end_with_err(ccall, ENOSYS);

		return 0;
	}

	if (!msg) {
		warning("ccall(%p): sft_msg_recv err=%d(%m)\n", ccall, status, status);
		return 0;
	}

	if (ccall->convid_hash && !strcaseeq(ccall->convid_hash, msg->sessid_sender)) {
		warning("ccall(%p): sft_msg_recv ignoring message for other session, "
			"mine: %s incoming: %s\n", ccall, ccall->convid_hash, msg->sessid_sender);
		return 0;
	}

	if (str_isset(msg->src_userid)) {
		userid_peer = msg->src_userid;
	}

	if (ECONN_CONF_CONN == msg->msg_type) {
		switch (msg->u.confconn.status) {
		case ECONN_CONFCONN_OK:
			break;

		case ECONN_CONFCONN_REJECTED_BLACKLIST:
		case ECONN_CONFCONN_REJECTED_AUTH_INVALID:
		case ECONN_CONFCONN_REJECTED_AUTH_LIMIT:
		case ECONN_CONFCONN_REJECTED_AUTH_EXPIRED:
		case ECONN_CONFCONN_REJECTED_AUTH_CANTSTART:
			info("ccall(%p): sft_msg_recv call rejected "
				"by SFT reason: %d\n", ccall, msg->u.confconn.status);
			ccall->confconn_status = msg->u.confconn.status;
			tmr_start(&ccall->tmr_sft_reject, 1,
				ccall_sft_reject_timeout, ccall);
			break;
		}
	}
	else {
		if (ECONN_SETUP == msg->msg_type) {
			if (CCALL_STATE_CONNSENT != ccall->state) {
				info("ccall(%p): sft_msg_recv ignoring "
				     "SETUP from sft %s in state %s\n",
				     ccall,
				     msg->u.setup.url ? msg->u.setup.url : "NULL",
				     ccall_state_name(ccall->state));
				return 0;
			}

			set_state(ccall, CCALL_STATE_SETUPRECV);

			info("ccall(%p): sft_msg_recv url %s resolved %s\n",
			     ccall,
			     msg->u.setup.url,
			     ccall->sft_url ? "YES" : "NO");
			if (msg->u.setup.url) {
				if (!ccall->sft_url) {
					info("ccall(%p): sft_msg_recv setting sft url: %s\n",
					     ccall,
					     msg->u.setup.url);
					err = copy_sft(&ccall->sft_url, msg->u.setup.url);
					if (err) {
						goto out;
					}
				}
				if (!ccall->primary_sft_url) {
					info("ccall(%p): sft_msg_recv setting primary sft url: %s\n",
					     ccall,
					     msg->u.setup.url);
					err = copy_sft(&ccall->primary_sft_url, msg->u.setup.url);
					if (err) {
						goto out;
					}
					ccall->sft_tuple = mem_deref(ccall->sft_tuple);
					if (msg->u.setup.sft_tuple) {
						err = str_dup(&ccall->sft_tuple, msg->u.setup.sft_tuple);
						if (err) {
							goto out;
						}
	 				}
				}
			}
		}
		else if (ECONN_UPDATE == msg->msg_type) {
			if (CCALL_STATE_CONNSENT != ccall->state) {
				info("ccall(%p): sft_msg_recv ignoring "
				     "UPDATE from sft %s in state %s\n",
				     ccall,
				     msg->u.setup.url ? msg->u.setup.url : "NULL",
				     ccall_state_name(ccall->state));
				return 0;
			}
			set_state(ccall, CCALL_STATE_SETUPRECV);
		}
		if (!ccall->ecall) {
			create_ecall(ccall);
			info("ccall(%p): sft_url=[%s]\n", ccall, msg->u.setup.url);
		}

		err =  ecall_msg_recv(ccall->ecall, 0, 0, userid_peer, clientid_peer, msg);
		if (err) {
			warning("ccall(%p): sft_msg_recv ecall handling err %d\n", ccall, err);
			return err;
		}
	}

out:
	return err;
}

int  ccall_stats_struct(const struct ccall *ccall,
		        struct iflow_stats *stats)
{
	if (!ccall || !ccall->ecall)
		return EINVAL;

	return ecall_stats_struct(ccall->ecall, stats);
}

int ccall_stats(struct re_printf *pf, const struct icall *icall)
{
	const struct ccall *ccall = (const struct ccall*)icall;

	if (ccall && ccall->ecall) {
		return ecall_stats(pf, ccall->ecall);
	}
	else {
		return 0;
	}
}

int ccall_set_background(struct icall *icall, bool background)
{
	struct ccall *ccall = (struct ccall*)icall;

	if (!ccall)
		return EINVAL;

	/* If we are in incoming call state, and there is an ongoing timer,
	 * we should stop it, until the app comes back to foreground
	 */
	if (background) {
		if (CCALL_STATE_INCOMING == ccall->state) {
			if (tmr_isrunning(&ccall->tmr_ongoing)) {
				tmr_cancel(&ccall->tmr_ongoing);
			}
		}
	}
	else {
		if (CCALL_STATE_INCOMING == ccall->state) {
			tmr_start(&ccall->tmr_ongoing, CCALL_ONGOING_CALL_TIMEOUT,
				  ccall_ongoing_call_timeout, ccall);
		}
	}

	return 0;
}

int  ccall_debug(struct re_printf *pf, const struct icall* icall)
{
	const struct ccall *ccall = (const struct ccall*)icall;
	char userid_anon[ANON_ID_LEN];
	char userid_anon2[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	const struct userinfo *u;
	const struct userinfo *self = NULL;
	struct le *le;
	int err = 0;

	err = re_hprintf(pf, "\nCCALL SUMMARY %p:\n", ccall);
	if (err)
		goto out;

	err = re_hprintf(pf, "confstate: %s\n", ccall_state_name(ccall->state));
	if (err)
		goto out;

	if (ccall->sft_url) {
		err = re_hprintf(pf, "selected_sft: %s\n", ccall->sft_url);
		if (err)
			goto out;
	}

	if (ccall->primary_sft_url) {
		err = re_hprintf(pf, "primary_sft: %s\n", ccall->primary_sft_url);
		if (err)
			goto out;
	}

	err = re_hprintf(pf, "\n");
	if (err)
		goto out;

	err = userlist_debug(pf, ccall->userl);
	if (err)
		goto out;

	err = re_hprintf(pf, "\n");
	if (err)
		goto out;

	self = userlist_get_self(ccall->userl);
	if (!self) {
		err = ENOENT;
		goto out;
	}

	LIST_FOREACH(&ccall->saved_partl, le) {
		const struct econn_group_part *p = le->data;

		u = userlist_find_by_hash(ccall->userl, p->userid, p->clientid);
		if (!u && strcaseeq(self->userid_hash, p->userid) &&
		    strcaseeq(self->clientid_hash, p->clientid)) {
			u = self;
		}

		if (u) {
			err = re_hprintf(pf, "part hash: %s user: %s.%s ssrca: %u ssrcv: %u\n",
				anon_id(userid_anon, p->userid),
				anon_id(userid_anon2, u->userid_real),
				anon_client(clientid_anon, u->clientid_real),
				p->ssrca, p->ssrcv);
		}
		else {
			err = re_hprintf(pf, "part hash: %s user: unknown.unknown ssrca: %u ssrcv: %u\n",
				anon_id(userid_anon, p->userid),
				p->ssrca, p->ssrcv);
		}

		if (err)
			goto out;
	}

	if (ccall->ecall) {
		err = re_hprintf(pf, "%H", ecall_mfdebug, ccall->ecall);
	}
out:
	return err;
}

int ccall_activate(struct icall *icall, bool active)
{
	struct ccall *ccall = (struct ccall *)icall;

	info("ccall(%p): activate: active=%d\n", ccall, active);
	if (ccall->ecall) {
		ecall_activate(ccall->ecall, active);
	}

	return 0;
}

int ccall_restart(struct icall *icall)
{
	struct ccall *ccall = (struct ccall *)icall;

	info("ccall(%p): restart\n", ccall);
	if (ccall->ecall) {
		ecall_restart(ccall->ecall, ccall->call_type, false);
	}

	return 0;
}

static void ccall_connect_timeout(void *arg)
{
	struct ccall *ccall = arg;

	info("ccall(%p): connect_timeout state=%s\n",
	     ccall, ccall_state_name(ccall->state));
	if (ccall->state != CCALL_STATE_ACTIVE) {
		ccall_end_with_err(ccall, ETIMEDOUT);
	}
}

static void ccall_end_with_err(struct ccall *ccall, int err)
{
	int reason = ICALL_REASON_NORMAL;

	if (ccall == NULL) {
		return;
	}

	info("ccall(%p): ccall_end err=%d state=%s\n", ccall,
		err, ccall_state_name(ccall->state));

	ccall->error = err;

	switch(err) {
	case 0:
		break;

	case ETIMEDOUT:
		reason = ICALL_REASON_TIMEOUT;
		break;

	case ENOONEJOINED:
		reason = ICALL_REASON_NOONE_JOINED;
		break;

	case EEVERYONELEFT:
		reason = ICALL_REASON_EVERYONE_LEFT;
		break;

	case EACCES:
		reason = ICALL_REASON_AUTH_FAILED;
		break;

	default:
		reason = ICALL_REASON_ERROR;
		break;
	}

	switch(ccall->state) {
	case CCALL_STATE_NONE:
	case CCALL_STATE_IDLE:
		set_state(ccall, CCALL_STATE_IDLE);
		break;

	case CCALL_STATE_CONNSENT:
	case CCALL_STATE_INCOMING:
	case CCALL_STATE_WAITCONFIG:
	case CCALL_STATE_WAITCONFIG_OUTGOING:
		set_state(ccall, CCALL_STATE_IDLE);
		ICALL_CALL_CB(ccall->icall, leaveh,
			      &ccall->icall, reason,
			      ECONN_MESSAGE_TIME_UNKNOWN, ccall->icall.arg);
		break;

	case CCALL_STATE_TERMINATING:
		break;

	case CCALL_STATE_SETUPRECV:
	case CCALL_STATE_CONNECTING:
	case CCALL_STATE_CONNECTED:
	case CCALL_STATE_ACTIVE:
		set_state(ccall, CCALL_STATE_TERMINATING);
		break;
	}

	userlist_incall_clear(ccall->userl, false, false);
	if (ccall->ecall && ecall_get_econn(ccall->ecall))
		ecall_end(ccall->ecall);
	else
		set_state(ccall, CCALL_STATE_IDLE);

}

struct keystore *ccall_get_keystore(struct ccall *ccall)
{
	if (!ccall)
		return NULL;
	return ccall->keystore;
}

