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

#if (defined ANDROID || defined __EMSCRIPTEN__ || defined LINUX)

#include <endian.h>
#define htonll(x) htobe64(x)

#endif

#define CCALL_CBR_ALWAYS_ON 1

static void ccall_connect_timeout(void *arg);
static void ccall_stop_ringing_timeout(void *arg);
static void ccall_ongoing_call_timeout(void *arg);
static void ccall_sync_vstate_timeout(void *arg);
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
				 bool update);
static int ccall_send_keys(struct ccall *ccall,
			   bool send_to_all);
static int ccall_request_keys(struct ccall *ccall);
static void ccall_stop_others_ringing(struct ccall *ccall);

static void ccall_track_keygenerator_change(struct ccall *ccall,
					    struct userinfo *prev_keygenerator);

static int alloc_message(struct econn_message **msgp,
			 struct ccall *ccall,
			 enum econn_msg type,
			 bool resp,
			 const char *src_userid,
			 const char *src_clientid,
			 const char *dest_userid,
			 const char *dest_clientid,
			 bool transient);

static void incall_clear(struct ccall *ccall,
			 bool force_decoder);

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
	tmr_cancel(&ccall->tmr_ring);
	tmr_cancel(&ccall->tmr_blacklist);
	tmr_cancel(&ccall->tmr_vstate);
	tmr_cancel(&ccall->tmr_decrypt_check);
	tmr_cancel(&ccall->tmr_keepalive);
	tmr_cancel(&ccall->tmr_alone);

	mem_deref(ccall->sft_url);
	mem_deref(ccall->primary_sft_url);
 	mem_deref(ccall->sft_tuple);
	mem_deref(ccall->convid_real);
	mem_deref(ccall->convid_hash);
	mem_deref(ccall->self);
	mem_deref(ccall->turnv);

	mem_deref(ccall->ecall);
	mem_deref(ccall->secret);
	mem_deref(ccall->keystore);

	list_flush(&ccall->sftl);
	list_flush(&ccall->partl);
	list_flush(&ccall->saved_partl);

	mbuf_reset(&ccall->confpart_data);
}

static void userinfo_destructor(void *arg)
{
	struct userinfo *ui = arg;

	list_unlink(&ui->le);
	ui->userid_real = mem_deref(ui->userid_real);
	ui->userid_hash = mem_deref(ui->userid_hash);
	ui->clientid_real = mem_deref(ui->clientid_real);
	ui->clientid_hash = mem_deref(ui->clientid_hash);
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

static int ccall_hash_conv(const uint8_t *secret,
			   uint32_t secretlen,
			   const char *convid, 
			   char **destid_hash)
{
	crypto_hash_sha256_state ctx;
	const char hexstr[] = "0123456789abcdef";
	const size_t hlen = 16;
	unsigned char hash[crypto_hash_sha256_BYTES];
	const size_t blen = min(hlen, crypto_hash_sha256_BYTES);
	char *dest = NULL;
	size_t i;
	int err = 0;

	err = crypto_hash_sha256_init(&ctx);
	if (err) {
		warning("ccall_hash_id: hash init failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, secret, secretlen);
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, (const uint8_t*)convid, strlen(convid));
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_final(&ctx, hash);
	if (err) {
		warning("ccall_hash_id: hash final failed\n");
		goto out;
	}

	dest = mem_zalloc(blen * 2 + 1, NULL);
	if (!dest) {
		err = ENOMEM;
		goto out;
	}

	for (i = 0; i < hlen; i++) {
		dest[i * 2]     = hexstr[hash[i] >> 4];
		dest[i * 2 + 1] = hexstr[hash[i] & 0xf];
	}

	*destid_hash = dest;

out:
	if (err) {
		mem_deref(dest);
	}
	return err;
}

static int ccall_hash_user(const uint8_t *secret,
			   uint32_t secretlen,
			   const char *userid, 
			   const char *clientid,
			   char **destid_hash)
{
	crypto_hash_sha256_state ctx;
	const char hexstr[] = "0123456789abcdef";
	const size_t hlen = 16;
	unsigned char hash[crypto_hash_sha256_BYTES];
	const size_t blen = min(hlen, crypto_hash_sha256_BYTES);
	char *dest = NULL;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	size_t i;
	int err = 0;

	if (!userid || !clientid)
		return EINVAL;

	err = crypto_hash_sha256_init(&ctx);
	if (err) {
		warning("ccall_hash_id: hash init failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, secret, secretlen);
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, (const uint8_t*)userid, strlen(userid));
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_update(&ctx, (const uint8_t*)clientid, strlen(clientid));
	if (err) {
		warning("ccall_hash_id: hash update failed\n");
		goto out;
	}

	err = crypto_hash_sha256_final(&ctx, hash);
	if (err) {
		warning("ccall_hash_id: hash final failed\n");
		goto out;
	}

	dest = mem_zalloc(blen * 2 + 1, NULL);
	if (!dest) {
		err = ENOMEM;
		goto out;
	}

	for (i = 0; i < hlen; i++) {
		dest[i * 2]     = hexstr[hash[i] >> 4];
		dest[i * 2 + 1] = hexstr[hash[i] & 0xf];
	}

	info("ccall_hash_user %s.%s hash %s\n",
		anon_id(userid_anon, userid),
		anon_client(clientid_anon, clientid),
		dest);
	*destid_hash = dest;

out:
	if (err) {
		mem_deref(dest);
	}
	return err;
}

static void ccall_hash_userinfo(struct ccall *ccall,
				const char *convid_real, 
				struct userinfo *info)
{

	info->userid_hash = mem_deref(info->userid_hash);
	info->clientid_hash = mem_deref(info->clientid_hash);
	ccall_hash_user(ccall->secret,
			ccall->secret_len,
			info->userid_real,
			info->clientid_real,
			&info->userid_hash);

	str_dup(&info->clientid_hash, "_");
}

static int ccall_set_secret(struct ccall *ccall,
			    const uint8_t *secret,
			    size_t secretlen)
{
	struct le *le;
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
	ccall_hash_conv(ccall->secret,
			secretlen,
			ccall->convid_real,
			&ccall->convid_hash);

	ccall_hash_userinfo(ccall, ccall->convid_real, ccall->self);

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		ccall_hash_userinfo(ccall, ccall->convid_real, u);
	}

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

	switch(ccall->state) {
	case CCALL_STATE_IDLE:
		ccall->keygenerator = NULL;
		ccall->received_confpart = false;
		keystore_reset(ccall->keystore);
		tmr_cancel(&ccall->tmr_rotate_key);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_cancel(&ccall->tmr_connect);
		tmr_cancel(&ccall->tmr_decrypt_check);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		break;

	case CCALL_STATE_INCOMING:
		ccall->keygenerator = NULL;
		ccall->received_confpart = false;
		keystore_reset_keys(ccall->keystore);
		tmr_cancel(&ccall->tmr_rotate_key);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_start(&ccall->tmr_ongoing, CCALL_ONGOING_CALL_TIMEOUT,
			  ccall_ongoing_call_timeout, ccall);
		tmr_cancel(&ccall->tmr_decrypt_check);
		tmr_cancel(&ccall->tmr_keepalive);
		tmr_cancel(&ccall->tmr_alone);
		break;

	case CCALL_STATE_CONNSENT:
		ccall->received_confpart = false;
		tmr_cancel(&ccall->tmr_ring);
		tmr_cancel(&ccall->tmr_send_check);
		tmr_start(&ccall->tmr_connect, CCALL_CONNECT_TIMEOUT,
			  ccall_connect_timeout, ccall);
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
		tmr_cancel(&ccall->tmr_connect);
		tmr_start(&ccall->tmr_decrypt_check, CCALL_DECRYPT_CHECK_TIMEOUT,
			  ccall_decrypt_check_timeout, ccall);
		tmr_start(&ccall->tmr_keepalive, CCALL_KEEPALIVE_TIMEOUT,
			  ccall_keepalive_timeout, ccall);
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
	int err = 0;

	if (CCALL_STATE_ACTIVE == ccall->state &&
	    ccall->keygenerator == ccall->self) {
		info("ccall(%p): send_check state=%s\n", ccall, ccall_state_name(ccall->state));

		ccall_send_msg(ccall, ECONN_CONF_CHECK,
			       true, NULL, false);
		if (err != 0) {
			warning("ccall(%p): send_check failed to send msg err=%d\n",
				ccall, err);
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
			      &ccall->icall, 0, NULL, ECONN_MESSAGE_TIME_UNKNOWN,
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

static void ccall_blacklisted_timeout(void *arg)
{
	struct ccall *ccall = arg;

	if (CCALL_STATE_CONNSENT == ccall->state) {
		info("ccall(%p): blacklisted_timeout\n", ccall);

		set_state(ccall, CCALL_STATE_IDLE);
		ICALL_CALL_CB(ccall->icall, leaveh,
			      &ccall->icall, ICALL_REASON_OUTDATED_CLIENT,
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
			    uint32_t msg_time)
{
	bool decrypt_attempted = false;
	bool decrypt_successful = false;

	if (!ccall || !ccall->keystore) {
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

	incall_clear(ccall, true);
	set_state(ccall, CCALL_STATE_CONNSENT);
	ccall_send_conf_conn(ccall, ccall->sft_url, true);
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
		ccall_reconnect(ccall, ECONN_MESSAGE_TIME_UNKNOWN);
		return;
	}

	if (!ccall->keygenerator) {
		info("ccall(%p): decrypt_check_timeout no keygenerator, waiting\n",
		     ccall);
		
		tmr_start(&ccall->tmr_decrypt_check, CCALL_DECRYPT_CHECK_TIMEOUT,
			  ccall_decrypt_check_timeout, ccall);
		return;
	}

	if (ccall->keygenerator == ccall->self) {
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

	ccall->expected_ping = 0;
	ccall->reconnect_attempts = 0;

	return 0;
}

static void ccall_keepalive_timeout(void *arg)
{
	struct ccall *ccall = arg;

	if (!ccall)
		return;

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
		ccall_reconnect(ccall, ECONN_MESSAGE_TIME_UNKNOWN);
	}

	tmr_start(&ccall->tmr_keepalive, CCALL_KEEPALIVE_TIMEOUT,
		  ccall_keepalive_timeout, ccall);
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
	struct le *le;
	int err = 0;

	if (ccall->keygenerator == ccall->self &&
	    CCALL_STATE_ACTIVE == ccall->state) {
		LIST_FOREACH(&ccall->partl, le) {
			struct userinfo *u = le->data;
			if (u && u->incall_now && u->se_approved &&
			    (u->needs_key || send_to_all)) {
				struct icall_client *c;

				c = icall_client_alloc(u->userid_real,
						       u->clientid_real);
				if (!c) {
					warning("ccall(%p): send_keys "
						"unable to alloc target\n", ccall);
					err = ENOMEM;
					goto out;
				}
				list_append(&targets, &c->le, c);
				u->needs_key = false;
			}
		}
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

	if (CCALL_STATE_ACTIVE == ccall->state &&
	    ccall->keygenerator == ccall->self) {
		info("ccall(%p): rotate_key state=%s\n", ccall, ccall_state_name(ccall->state));

		err = keystore_rotate(ccall->keystore);
		if (err) {
			warning("ccall(%p): rotate_key err=%m\n", ccall, err);
		}

		if (ccall->someone_left) {
			ccall_generate_session_key(ccall, false);
			ccall->someone_left = false;

		}
		tmr_start(&ccall->tmr_rotate_key,
			  CCALL_ROTATE_KEY_TIMEOUT,
			  ccall_rotate_key_timeout, ccall);
	}
}

static int ccall_request_keys(struct ccall *ccall)
{
	struct list targets = LIST_INIT;
	struct icall_client *c;
	int err = 0;

	if (!ccall->keygenerator) {
		warning("ccall(%p): request_keys keygenerator is NULL!\n", ccall);
		return 0;
	}

	if (ccall->keygenerator == ccall->self) {
		warning("ccall(%p): request_keys keygenerator is self!\n", ccall);
		return 0;
	}

	c = icall_client_alloc(ccall->keygenerator->userid_real,
			       ccall->keygenerator->clientid_real);
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
	struct ccall *ccall = arg;

	if (!ccall)
		return;

	ICALL_CALL_CB(ccall->icall, media_estabh,
		icall, userid, clientid, update, ccall->icall.arg);
	set_state(ccall, CCALL_STATE_ACTIVE);
	incall_clear(ccall, true);
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

	if (!ccall)
		return;

	ICALL_CALL_CB(ccall->icall, audio_levelh,
		      icall, levell, ccall->icall.arg);
}


static uint32_t incall_count(struct ccall *ccall)
{
	struct le *le;
	uint32_t incall = 0;

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		if (u && u->incall_now) {
			incall++;
		}
	}
	return incall;
}

static void incall_clear(struct ccall *ccall,
			 bool force_decoder)
{
	struct le *le;
	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		if (!u)
			continue;
		u->incall_now = u->incall_now && force_decoder;
		u->force_decoder = u->incall_now;
		u->incall_prev = false;
		u->ssrca = 0;
		u->ssrcv = 0;
	}
}

static void ecall_close_handler(struct icall *icall,
				int err,
				const char *metrics_json,
				uint32_t msg_time,
				const char *userid,
				const char *clientid,
				void *arg)
{
	struct ecall *ecall = (struct ecall*)icall;
	struct ccall *ccall = arg;
	bool should_end = false;
	struct le *le;

	should_end = incall_count(ccall) == 0;
	info("ccall(%p): ecall_close_handler err=%d ecall=%p should_end=%s parts=%u\n",
	     ccall, err, ecall, should_end ? "YES" : "NO", list_count(&ccall->partl));

	if (ecall != ccall->ecall) {
		mem_deref(ecall);
		return;
	}

	ccall->keygenerator = NULL;

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

	if (err == EAGAIN) {
		ccall->reconnect_attempts = 0;
		ccall->expected_ping = 0;

		ccall_reconnect(ccall, msg_time);
		return;
	}

	incall_clear(ccall, false);
	mem_deref(ecall);
	ccall->ecall = NULL;

	switch (ccall->error) {
	case 0:
	case ENOONEJOINED:
	case EEVERYONELEFT:
		if (should_end) {
			ccall_send_msg(ccall, ECONN_CONF_END,
				       false, NULL, false);
		}
		break;
	}

	if (should_end) {
		set_state(ccall, CCALL_STATE_IDLE);

		ICALL_CALL_CB(ccall->icall, closeh, 
			&ccall->icall, ccall->error, NULL, msg_time,
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


static int ccall_send_msg_sft(struct ccall *ccall,
			      const char *sft_url,
			      struct econn_message *msg)
{
	int err = 0;
	char *url = NULL;

	if (!sft_url) {
		return EINVAL;
	}

	if (ECONN_CONF_CONN == msg->msg_type ||
	    ECONN_SETUP == msg->msg_type ||
	    ECONN_UPDATE == msg->msg_type) {
		// Send these messages to the SFT
		int len;
		const char *fmt = "%ssft/%s";

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

		if (ECONN_SETUP == msg->msg_type &&
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
				     void *arg)
{
	struct ccall *ccall = arg;
	(void) userid;
	(void) targets;

	return ccall_send_msg_sft(ccall, ccall->sft_url, msg);
}

static struct userinfo *find_userinfo_by_real(const struct ccall *ccall,
					      const char *userid_real,
					      const char *clientid_real)
{
	struct le *le;

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;

		if (u && u->userid_real && u->clientid_real) {
			if (strcaseeq(u->userid_real, userid_real) &&
			    strcaseeq(u->clientid_real, clientid_real)) {
				return u;
			}
		}
	}
	return NULL;
}

static struct userinfo *find_userinfo_by_hash(const struct ccall *ccall,
					      const char *userid_hash,
					      const char *clientid_hash)
{
	struct le *le;

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;

		if (u && u->userid_hash && u->clientid_hash) {
			if (strcaseeq(u->userid_hash, userid_hash) &&
			    strcaseeq(u->clientid_hash, clientid_hash)) {
				return u;
			}
		}
	}
	return NULL;
}

static int send_confpart_response(struct ccall *ccall)
{
	struct econn_message *msg;
	char *str = NULL;
	struct le *le = NULL;
	struct mbuf mb;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	int err = 0;

	if (!ccall)
		return EINVAL;

	err = alloc_message(&msg, ccall, ECONN_CONF_PART, true,
		ccall->self->userid_hash, ccall->self->clientid_hash,
		"SFT", "SFT", false);
	if (err) {
		goto out;
	}

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		if (u && u->se_approved && u->incall_now) {
			struct econn_group_part *part = econn_part_alloc(u->userid_hash,
									 u->clientid_hash);
			if (!part) {
				err = ENOMEM;
				goto out;
			}
			part->ssrca = u->ssrca;
			part->ssrcv = u->ssrcv;
			part->authorized = true;
			list_append(&msg->u.confpart.partl, &part->le, part);

			info("ccall(%p) send_confpart adding %s.%s hash %s.%s "
			     " ssrca %u ssrcv %u\n",
			     ccall,
			     anon_id(userid_anon, u->userid_real),
			     anon_client(clientid_anon, u->clientid_real),
			     u->userid_hash,
			     u->clientid_hash,
			     u->ssrca,
			     u->ssrcv);
		}
	}

	err = econn_message_encode(&str, msg);
	if (err) {
		warning("ccall(%p): send_confpart_resp: econn_message_encode"
			" failed (%m)\n", err);
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
			" failed (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(str);
	mem_deref(msg);

	return err;
}

int  ccall_request_video_streams(struct icall *icall,
				 struct list *clientl,
				 enum icall_stream_mode mode)
{
	struct ccall *ccall = (struct ccall*)icall;
	struct econn_stream_info *sinfo;
	struct econn_message *msg;
	char *str = NULL;
	struct mbuf mb;
	struct le *le = NULL;
	int err = 0;

	if (!ccall)
		return EINVAL;

	err = alloc_message(&msg, ccall, ECONN_CONF_STREAMS, false,
		ccall->self->userid_hash, ccall->self->clientid_hash,
		"SFT", "SFT", false);
	if (err) {
		goto out;
	}

	str_dup(&msg->u.confstreams.mode, "list");
	LIST_FOREACH(clientl, le) {
		struct icall_client *cli = le->data;
		struct userinfo *user;

		user = find_userinfo_by_real(ccall, cli->userid, cli->clientid);
		if (user) {
			sinfo = econn_stream_info_alloc(user->userid_hash, 0);
			if (!sinfo) {
				err = ENOMEM;
				goto out;
			}
		
			list_append(&msg->u.confstreams.streaml, &sinfo->le, sinfo);
		}
	}

	info("ccall(%p): request_video_streams mode: %u clients: %u matched: %u\n",
	     ccall,
	     mode,
	     list_count(clientl),
	     list_count(&msg->u.confstreams.streaml));

	err = econn_message_encode(&str, msg);
	if (err) {
		warning("ccall(%p): request_video_streams: econn_message_encode"
			" failed (%m)\n", err);
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

static  int ecall_propsync_handler(struct ecall *ecall,
				   struct econn_message *msg,
				   void *arg)
{
	struct ccall *ccall = arg;
	struct userinfo *user;
	int vstate = ICALL_VIDEO_STATE_STOPPED;
	bool vstate_present = false;
	bool muted = false;
	bool muted_present = false;
	bool group_changed = false;
	const char *vr;
	const char *mt;

	if (!ccall || !msg) {
		return EINVAL;
	}

	vr = econn_props_get(msg->u.propsync.props, "videosend");
	mt = econn_props_get(msg->u.propsync.props, "muted");
	info("ccall(%p): ecall_propsync_handler ecall: %p"
		" remote %s.%s video '%s' muted '%s' %s\n",
	     ccall, ecall, msg->src_userid, msg->src_clientid,
	     vr ? vr : "", mt ? mt : "", msg->resp ? "resp" : "req");

	user = find_userinfo_by_hash(ccall, msg->src_userid, msg->src_clientid);
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
	return 0;
}

static bool partlist_sort_h(struct le *le1, struct le *le2, void *arg)
{
	struct userinfo *a, *b;

	a = le1->data;
	b = le2->data;

	return a->listpos <= b->listpos;
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


static void ecall_confpart_handler(struct ecall *ecall,
				   const struct econn_message *msg,
				   void *arg)
{
	struct ccall *ccall = arg;
	struct le *le, *cle;
	bool first = true;
	bool list_changed = false;
	bool sync_decoders = false;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct userinfo *prev_keygenerator;
	bool missing_parts = false;
	uint32_t listpos = 0;
	int err = 0;

	uint64_t timestamp = msg->u.confpart.timestamp;
	uint32_t seqno = msg->u.confpart.seqno;
	bool should_start = msg->u.confpart.should_start;
	const struct list *partlist = &msg->u.confpart.partl;

	info("ccall(%p): ecall_confpart_handler ecall: %p "\
	     "should_start %s partl: %u members ts: %lu sn: %u\n",
	     ccall, ecall, should_start ? "YES" : "NO",
	     list_count(partlist), timestamp, seqno);

	if (!ccall || ecall != ccall->ecall) {
		return;
	}

	ccall->received_confpart = true;
	ccall_keep_confpart_data(ccall, msg);

	if (should_start && ccall->is_caller) {
		ccall->sft_timestamp = timestamp;
		ccall->sft_seqno = seqno;

		info("ccall(%p): sending CONFSTART from should_start\n", ccall);
		ccall_send_msg(ccall, ECONN_CONF_START,
			       false, NULL, false);
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
		
	LIST_FOREACH(&ccall->partl, cle) {
		struct userinfo *u = cle->data;
		if (!u)
			continue;
		u->incall_prev = u->incall_now;
		u->incall_now = false;
		u->listpos = 0xFFFFFFFF;
	}

	ccall->self->listpos = 0xFFFFFFFF;
	prev_keygenerator = ccall->keygenerator;
	ccall->keygenerator = NULL;
	LIST_FOREACH(partlist, le) {
		struct econn_group_part *p = le->data;

		first = le == partlist->head;

		assert(p != NULL);

		if (p && strcaseeq(ccall->self->userid_hash, p->userid) &&
		    strcaseeq(ccall->self->clientid_hash, p->clientid)) {
			if (first) {
				info("ccall(%p): setting self as keygenerator\n", ccall);
				ccall->keygenerator = ccall->self;
			}
			ccall->self->listpos = listpos;
			listpos++;
			continue;
		}

		struct userinfo *u = find_userinfo_by_hash(ccall, p->userid, p->clientid);
		if (u) {
			bool muted;

			if (first && u->se_approved) {
				info("ccall(%p): setting %s.%s as keygenerator\n",
				     ccall,
				     anon_id(userid_anon, u->userid_real),
				     anon_client(clientid_anon, u->clientid_real));

				ccall->keygenerator = u;
			}
			if (u->incall_prev && 
			    (u->ssrca != p->ssrca ||
			    u->ssrcv != p->ssrcv)) {
				if (ccall->ecall) {
					ecall_remove_decoders_for_user(ccall->ecall,
								       u->userid_hash,
								       u->clientid_hash,
								       u->ssrca,
								       u->ssrcv);
				}
				ccall->someone_left = true;
				u->incall_prev = false;
				sync_decoders = true;
				list_changed = true;
			}

			u->incall_now = true;
			u->ssrca = p->ssrca;
			u->ssrcv = p->ssrcv;

			switch (p->muted_state) {
			case MUTED_STATE_UNKNOWN:
				muted = u->muted;
				break;
			case MUTED_STATE_MUTED:
				muted = true;
				break;
			case MUTED_STATE_UNMUTED:
				muted = false;
				break;
			}

			if (muted != u->muted) {
				u->muted = muted;
				list_changed = true;
			}

			u->listpos = listpos;
			listpos++;
		}
		else {
			warning("ccall(%p): ecall_confpart_handler didnt find part for %s\n",
			     ccall, anon_id(userid_anon, p->userid));
			u = mem_zalloc(sizeof(*u), userinfo_destructor);
			if (!u) {
				warning("ccall(%p): ecall_confpart_handler "
					"couldnt alloc part of %s\n",
				     ccall, anon_id(userid_anon, p->userid));
				return;
			}
			str_dup(&u->userid_hash, p->userid);
			str_dup(&u->clientid_hash, p->clientid);
			u->ssrca = p->ssrca;
			u->ssrcv = p->ssrcv;
			u->incall_now = true;
			list_append(&ccall->partl, &u->le, u);
			missing_parts = true;
			u->listpos = listpos;
			listpos++;
		}

	}

	LIST_FOREACH(&ccall->partl, cle) {
		struct userinfo *u = cle->data;

		if (u && u->se_approved) {
			if (u->force_decoder ||
			   (u->incall_now && !u->incall_prev)) {
				if (ccall->ecall && (u->ssrca != 0 || u->ssrcv != 0)) {
					info("ccall(%p): add_decoders_for_user: ecall: %p "
					        "u: %s.%s a: %u v: %u\n",
						ccall, ccall->ecall,
						anon_id(userid_anon, u->userid_real),
						anon_client(clientid_anon, u->clientid_real),
						u->ssrca, u->ssrcv);
					ecall_add_decoders_for_user(ccall->ecall,
								    u->userid_real,
								    u->clientid_real,
								    u->userid_hash,
								    u->ssrca,
								    u->ssrcv);

					sync_decoders = true;
				}
				u->force_decoder = false;
				u->needs_key = true;
				list_changed = true;
			}
			else if (!u->incall_now && u->incall_prev) {
				if (ccall->ecall) {
					ecall_remove_decoders_for_user(ccall->ecall,
								       u->userid_real,
								       u->clientid_real,
								       u->ssrca,
								       u->ssrcv);
					ccall->someone_left = true;
					sync_decoders = true;
				}
				if (u->video_state != ICALL_VIDEO_STATE_STOPPED) {
					ICALL_CALL_CB(ccall->icall, vstate_changedh,
						      &ccall->icall, u->userid_real,
						      u->clientid_real,
						      ICALL_VIDEO_STATE_STOPPED,
						      ccall->icall.arg);
				}
				u->ssrca = u->ssrcv = 0;
				u->video_state = ICALL_VIDEO_STATE_STOPPED;
				list_changed = true;
			}
		}
	}

	if (sync_decoders) {
		info("ccall(%p): sync_decoders\n", ccall);
		ecall_sync_decoders(ccall->ecall);
	}

	list_sort(&ccall->partl, partlist_sort_h, NULL);
	send_confpart_response(ccall);

	ccall_track_keygenerator_change(ccall, prev_keygenerator);

	if (list_changed) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			&ccall->icall, ccall->icall.arg);	

		if (ccall->keygenerator == ccall->self) {
			err = ccall_send_keys(ccall, false);
			if (err) {
				warning("ccall(%p): send_keys failed\n", ccall);
			}
		}

		if (ccall->ecall) {
			err = ecall_sync_props(ccall->ecall, true);
			if (err) {
				warning("ccall(%p): sync_props failed\n", ccall);
			}
		}
	}

	if (missing_parts) {
		ICALL_CALL_CB(ccall->icall, req_clientsh,
		      &ccall->icall, ccall->icall.arg);
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
		err = str_dup(&msg->u.confcheck.sft_url, ccall->sft_url);
		if (err) {
			goto out;
		}
		if (ccall->sft_tuple) {
			err = str_dup(&msg->u.confcheck.sft_tuple, ccall->sft_tuple);
			if (err) {
				goto out;
			}
		}
		str_ncpy(msg->sessid_sender, ccall->convid_hash, ECONN_ID_LEN);
	}
	else if (type == ECONN_CONF_KEY) {
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
					ccall->keygenerator == ccall->self ? "YES" : "NO");
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

static int  ccall_send_msg(struct ccall *ccall,
			   enum econn_msg type,
			   bool resp,
			   struct list *targets,
			   bool transient)
{
	struct econn_message *msg = NULL;
	int err = 0;

	if (targets) {
		info("ccall(%p): send_msg type: %s targets: %zu\n",
			ccall, econn_msg_name(type), list_count(targets));
	}
	else {
		info("ccall(%p): send_msg type: %s userid=%s clientid=%s\n",
		     ccall, econn_msg_name(type), ccall->self->userid_real, ccall->self->clientid_real);
	}

	err = alloc_message(&msg, ccall, type, resp,
			    ccall->self->userid_real, ccall->self->clientid_real,
			    NULL, NULL,
			    transient);

	if (err) {
		warning("ccall(%p): failed to alloc message: %m\n", ccall, err);
		goto out;
	}

	err = ICALL_CALL_CBE(ccall->icall, sendh,
			     &ccall->icall,
			     ccall->self->userid_real,
			     msg, targets, ccall->icall.arg);

	if (err != 0) {
		goto out;
	}

out:
	mem_deref(msg);

	return err;
}

static int  ccall_send_conf_conn(struct ccall *ccall,
				 const char *sft_url,
				 bool update)
{
	enum econn_msg type = ECONN_CONF_CONN;
	bool resp = false;
	struct econn_message *msg = NULL;
	struct zapi_ice_server *turnv;
	size_t turnc;
	int err = 0;

	if (!ccall || !sft_url) {
		return EINVAL;
	}

	info("ccall(%p): send_msg_sft url: %s type: %s resp: %s\n",
	     ccall,
	     sft_url,
	     econn_msg_name(type),
	     resp ? "YES" : "NO");

	err = alloc_message(&msg, ccall, type, resp,
			    ccall->self->userid_hash, ccall->self->clientid_hash,
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
	msg->u.confconn.update = update;
	msg->u.confconn.selective_audio = true;
	msg->u.confconn.selective_video = true;
	msg->u.confconn.vstreams = CCALL_MAX_VSTREAMS;

	if (ccall->primary_sft_url && 
	    strcmp(ccall->primary_sft_url, sft_url) != 0) {
		err = str_dup(&msg->u.confconn.sft_url, ccall->primary_sft_url);
		if (err) {
			goto out;
		}
		if (ccall->sft_tuple) {
			err = str_dup(&msg->u.confconn.sft_tuple, ccall->sft_tuple);
			if (err) {
				goto out;
			}
		}
	}

	err = ccall_send_msg_sft(ccall, sft_url, msg);
	if (err != 0) {
		goto out;
	}

out:
	mem_deref(msg);

	return err;
}

static int create_ecall(struct ccall *ccall)
{
	struct ecall *ecall = NULL;
	struct msystem *msys = msystem_instance();
	size_t i;
	int err = 0;

	assert(ccall->ecall == NULL);

	err = ecall_alloc(&ecall, NULL,
			  ICALL_CONV_TYPE_CONFERENCE,
			  ccall->conf,
			  msys,
			  ccall->convid_real,
			  ccall->self->userid_hash,
			  ccall->self->clientid_hash);

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
			    NULL, // ecall_quality_handler,
			    NULL, // ecall_req_clients_handler,
			    NULL, // ecall_norelay_handler,
			    ecall_aulevel_handler,
			    ccall);

	ecall_set_confmsg_handler(ecall, ecall_confmsg_handler);
	ecall_set_propsync_handler(ecall, ecall_propsync_handler);
	ecall_set_ping_handler(ecall, ecall_ping_handler);
	ecall_set_keystore(ecall, ccall->keystore);
	err = ecall_set_real_clientid(ecall, ccall->self->clientid_real);
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

out:
	if (err) {
		mem_deref(ecall);
	}

	return err;
}

int ccall_alloc(struct ccall **ccallp,
		const struct ecall_conf *conf,		 
		const char *convid,
		const char *userid_self,
		const char *clientid)
{
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

	ccall->self = mem_zalloc(sizeof(*ccall->self), userinfo_destructor);
	if (!ccall->self) {
		err = ENOMEM;
		goto out;
	}
	err = str_dup(&ccall->convid_real, convid);
	err |= str_dup(&ccall->self->userid_real, userid_self);
	err |= str_dup(&ccall->self->clientid_real, clientid);
	if (err)
		goto out;

	ccall->turnv = mem_zalloc(MAX_TURN_SERVERS * sizeof(*ccall->turnv), NULL);
	if (!ccall->turnv) {
		err = ENOMEM;
		goto out;
	}

	err = keystore_alloc(&ccall->keystore);
	if (err)
		goto out;

	randombytes_buf(&secret, CCALL_SECRET_LEN);
	info("ccall(%p) set_secret from alloc\n", ccall);
	ccall_set_secret(ccall, secret, CCALL_SECRET_LEN);

	mbuf_init(&ccall->confpart_data);
	ccall->conf = conf;
	ccall->state = CCALL_STATE_IDLE;
	ccall->stop_ringing_reason = CCALL_STOP_RINGING_NONE;

	tmr_init(&ccall->tmr_connect);
	tmr_init(&ccall->tmr_call);
	tmr_init(&ccall->tmr_send_check);
	tmr_init(&ccall->tmr_ongoing);
	tmr_init(&ccall->tmr_ring);
	tmr_init(&ccall->tmr_blacklist);
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
			    ccall_debug,
			    ccall_stats);
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

static bool ccall_can_connect_primary_sft(struct ccall *ccall)
{
	struct zapi_ice_server *sftv;
	size_t sft = 0, sftc = 0;
	char *url = NULL;
	bool found = false;
	int err = 0;

	if (!ccall->primary_sft_url) {
		return false;
	}
	sftv = config_get_sftservers_all(ccall->cfg, &sftc);

	info("ccall(%p): can_connect_primary %zu sfts in sft_servers_all\n",
		ccall, sftc);
	if (sftc == 0) {
		/* If no sfts in config, assume legacy behaviour:
		   we can connect to all SFTs */
		return true;
	}

	for (sft = 0; sft < sftc && !found; sft++) {
		err = copy_sft(&url, sftv[sft].url);
		if (err) {
			continue;
		}
		if (strcmp(ccall->primary_sft_url, url) == 0) {
			info("ccall(%p): can_connect_primary found sft %s in calls/conf\n",
				ccall, url);
			found = true;
		}
		url = mem_deref(url);
	}
	return found;
}

static void config_update_handler(struct call_config *cfg, void *arg)
{
	struct join_elem *je = arg;
	struct ccall *ccall = je->ccall;
	struct zapi_ice_server *urlv;
	size_t urlc, sft;
	char *url = NULL;
	int err = 0;
	int state = ccall->state;

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

	info("ccall(%p): cfg_update received %zu sfts state: %s\n",
	     ccall,
	     urlc,
	     ccall_state_name(ccall->state));
	
	if (!urlv || !urlc) {
		warning("ccall(%p): no SFT server configured\n", ccall);
		ccall_end_with_err(ccall, ENOTSUP);
		goto out;
	}

	switch (ccall->state) {
	case CCALL_STATE_IDLE:
	case CCALL_STATE_INCOMING:
		break;
		
	default:
		warning("ccall(%p): cfg_update in state %s, ignoring\n",
			ccall, ccall_state_name(ccall->state));
		goto out;
	}

	set_state(ccall, CCALL_STATE_CONNSENT);
	ccall->call_type = je->call_type;

	ICALL_CALL_CB(ccall->icall, req_clientsh,
		      &ccall->icall, ccall->icall.arg);

	if (CCALL_STATE_INCOMING == state && ccall->primary_sft_url) {
		if (ccall_can_connect_primary_sft(ccall)) {
			info("ccall(%p): cfg_update connecting to primary_sft %s\n",
			     ccall,
			     ccall->primary_sft_url);
			err = ccall_send_conf_conn(ccall, ccall->primary_sft_url, false);
			if (err) {
				warning("ccall(%p): cfg_update failed to send "
					"confconn to sft %s err=%d\n",
					ccall, url, err);
			}
			else {
				return;
			}
		}
	}

	urlc = MIN(urlc, 3);
	info("ccall(%p): cfg_update connecting to %u sfts from calls/conf",
		ccall, urlc);
	for (sft = 0; sft < urlc; sft++) {
		/* If one SFT fails, keep trying the rest */
		err = copy_sft(&url, urlv[sft].url);
		if (err) {
			continue;
		}
		err = ccall_send_conf_conn(ccall, url, false);
		if (err) {
			warning("ccall(%p): cfg_update failed to send "
				"confconn to sft %s err=%d\n",
				ccall, url, err);
		}
		url = mem_deref(url);
	}
 out:
	ccall->je = mem_deref(ccall->je);
}

static void je_destructor(void *arg)
{
	struct join_elem *je = arg;

	config_unregister_update_handler(&je->upe);
}

static int  ccall_req_cfg_join(struct ccall *ccall,
			       enum icall_call_type call_type,
			       bool audio_cbr)
{
	struct join_elem *je;
	int err;

	je = mem_zalloc(sizeof(*je), je_destructor);
	if (!je)
		return ENOMEM;

	if (ccall->je) {
		ccall->je = mem_deref(ccall->je);
	}

	ccall->je = je;

	je->ccall = ccall;
	je->call_type = call_type;
	je->audio_cbr = audio_cbr;
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
		err = ccall_req_cfg_join(ccall, call_type, audio_cbr);
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
	int err;

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
		err = ccall_req_cfg_join(ccall, call_type, audio_cbr);
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
		ICALL_CALL_CB(ccall->icall, req_clientsh,
			      &ccall->icall, ccall->icall.arg);

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
	bool changed = false;
	int istate = (int)state;
	int err;

	if (!ccall)
		return EINVAL;

	info("ccall(%p) set_vstate ecall: %p state: %d\n",
	      ccall,
	      ccall->ecall,
	      istate);
	ccall->vstate = state;

	if (!ccall->ecall) {
		return 0;
	}
	err = ecall_set_video_send_state(ccall->ecall, state);
	if (err) {
		return err;
	}

	if (ccall->self && istate != ccall->self->video_state) {
		ccall->self->video_state = istate;
		changed = true;
	}

	if (changed) {
		ICALL_CALL_CB(ccall->icall, vstate_changedh, &ccall->icall,
			      ccall->self->userid_real,
			      ccall->self->clientid_real,
			      state, ccall->icall.arg);

		ICALL_CALL_CB(ccall->icall, group_changedh,
			&ccall->icall, ccall->icall.arg);	
	}
	return 0;
}

int  ccall_get_members(struct icall *icall, struct wcall_members **mmp)
{
	struct ccall *ccall = (struct ccall*)icall;
	struct wcall_members *mm;
	struct le *le;
	size_t n = 0;
	int err = 0;

	if (!ccall)
		return EINVAL;

	mm = mem_zalloc(sizeof(*mm), members_destructor);
	if (!mm) {
		err = ENOMEM;
		goto out;
	}

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		if (u && u->incall_now) {
			n++;
		}
	}

	n++;
	mm->membv = mem_zalloc(sizeof(*(mm->membv)) * n, NULL);
	if (!mm->membv) {
		err = ENOMEM;
		goto out;
	}
	{
		struct userinfo *u = ccall->self;
		struct wcall_member *memb = &(mm->membv[mm->membc]);

		str_dup(&memb->userid, u->userid_real);
		str_dup(&memb->clientid, u->clientid_real);
		memb->audio_state = (CCALL_STATE_ACTIVE == ccall->state) ?
				    ICALL_AUDIO_STATE_ESTABLISHED :
				    ICALL_AUDIO_STATE_CONNECTING;
		memb->video_recv = u->video_state;
		memb->muted = msystem_get_muted() ? 1 : 0;

		(mm->membc)++;
	}
	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		if (u && u->se_approved && u->incall_now) {
			struct wcall_member *memb = &(mm->membv[mm->membc]);

			str_dup(&memb->userid, u->userid_real);
			str_dup(&memb->clientid, u->clientid_real);
			memb->audio_state = u->ssrca > 0 ?
					    ICALL_AUDIO_STATE_ESTABLISHED :
					    ICALL_AUDIO_STATE_CONNECTING;
			memb->video_recv = u->video_state;
			memb->muted = u->muted ? 1 : 0;

			(mm->membc)++;
		}
	}

 out:
	if (err)
		mem_deref(mm);
	else {
		if (mmp)
			*mmp = mm;
	}

	return err;
}

int  ccall_set_quality_interval(struct icall *icall, uint64_t interval)
{
	return 0;
}

static void ccall_stop_others_ringing(struct ccall *ccall)
{
	struct list targets = LIST_INIT;
	struct le *le = NULL;
	enum econn_msg msgtype;

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

	LIST_FOREACH(&ccall->partl, le) {
		struct userinfo *u = le->data;
		if (u && strcaseeq(u->userid_real, ccall->self->userid_real) &&
		    !strcaseeq(u->clientid_real, ccall->self->clientid_real)) {
			struct icall_client *c;

			c = icall_client_alloc(u->userid_real,
					       u->clientid_real);
			if (!c) {
				warning("ccall(%p): stop_others_ringing "
					"unable to alloc target\n", ccall);
				goto out;
			}
			list_append(&targets, &c->le, c);
		}
	}

	info("ccall(%p): stop_others_ringing state=%s targets=%u\n",
	     ccall,
	     ccall_state_name(ccall->state),
	     list_count(&targets));

	if (list_count(&targets) > 0) {
		ccall_send_msg(ccall, msgtype,
			       true, &targets,
			       false);
	}

out:
	list_flush(&targets);
}

static void ccall_track_keygenerator_change(struct ccall *ccall,
					    struct userinfo *prev_keygenerator)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	uint32_t kid = 0;
	uint64_t updated_ts = 0;
	int err = 0;

	if (!ccall ||
	    ccall->keygenerator == NULL ||
	    ccall->keygenerator == prev_keygenerator ||
	    !ccall->keystore) {
		return;
	}

	if (ccall->keygenerator == ccall->self) {
		info("ccall(%p): track_keygenerator: new keygenerator is me\n",
		      ccall);

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

		tmr_start(&ccall->tmr_send_check,
			  prev_keygenerator ? 0 : CCALL_SEND_CHECK_TIMEOUT,
			  ccall_send_check_timeout, ccall);
		tmr_start(&ccall->tmr_rotate_key,
			  CCALL_ROTATE_KEY_FIRST_TIMEOUT,
			  ccall_rotate_key_timeout, ccall);
	}
	else {
		info("ccall(%p): track_keygenerator: new keygenerator is %s.%s\n",
		      ccall,
		      anon_id(userid_anon, ccall->keygenerator->userid_real),
		      anon_client(clientid_anon, ccall->keygenerator->clientid_real));

		if (ccall->request_key) {
			info("ccall(%p): requesting key resend from %s.%s\n",
			     ccall,
			     anon_id(userid_anon, ccall->keygenerator->userid_real),
			     anon_client(clientid_anon, ccall->keygenerator->clientid_real));
			ccall_request_keys(ccall);
			ccall->request_key = false;
		}

		tmr_cancel(&ccall->tmr_send_check);
		tmr_cancel(&ccall->tmr_rotate_key);
	}

}

void ccall_set_clients(struct icall* icall, struct list *clientl)
{
	struct le *le;
	struct ccall *ccall = (struct ccall*)icall;
	bool list_changed = false;
	bool sync_decoders = false;
	struct userinfo *user;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct userinfo *prev_keygenerator;

	info("ccall(%p): set_clients %zu clients\n", ccall, list_count(clientl));

	prev_keygenerator = ccall->keygenerator;

	LIST_FOREACH(clientl, le) {
		struct icall_client *cli = le->data;

		if (!cli)
			continue;
		user = find_userinfo_by_real(ccall, cli->userid, cli->clientid);
		if (user) {
			ccall_hash_userinfo(ccall, ccall->convid_real, user);
			info("ccall(%p): set_clients approving found client\n", ccall);
			user->se_approved = true;
		}
		else {
			struct userinfo *u = mem_zalloc(sizeof(*u), userinfo_destructor);
			if (!u) {
				warning("ccall(%p): set_clients couldnt alloc user\n", ccall);
				return;
			}
			str_dup(&u->userid_real, cli->userid);
			str_dup(&u->clientid_real, cli->clientid);
			ccall_hash_userinfo(ccall, ccall->convid_real, u);
			user = find_userinfo_by_hash(ccall, u->userid_hash, u->clientid_hash);
			if (user && !user->se_approved) {
				info("ccall(%p): set_clients approving found client\n", ccall);
				user->userid_real = mem_deref(user->userid_real);
				user->clientid_real = mem_deref(user->clientid_real);
				str_dup(&user->userid_real, cli->userid);
				str_dup(&user->clientid_real, cli->clientid);
				user->se_approved = true;
				list_changed = true;

				if ((user->ssrca != 0 || user->ssrcv != 0)) {
					info("ccall(%p): add_decoders_for_user: ecall: %p "
						"u: %s.%s a: %u v: %u\n",
						ccall, ccall->ecall,
						anon_id(userid_anon, u->userid_real),
						anon_client(clientid_anon, u->clientid_real),
						user->ssrca, user->ssrcv);
					ecall_add_decoders_for_user(ccall->ecall,
								    user->userid_real,
								    user->clientid_real,
								    user->userid_hash,
								    user->ssrca,
								    user->ssrcv);
					sync_decoders = true;
				}

				if (user->listpos == 0) {
					ccall->keygenerator = user;
					info("ccall(%p): setting keygenerator from "
					     "updated list\n",
					     ccall);

					ccall_track_keygenerator_change(ccall,
									prev_keygenerator);
				}

				user->needs_key = true;
				user->force_decoder = false;
				mem_deref(u);
			}
			else {
				info("ccall(%p): set_clients adding new client\n", ccall);
				u->se_approved = true;
				list_append(&ccall->partl, &u->le, u);
			}
		}
	}

	if (sync_decoders) {
		info("ccall(%p): sync_decoders\n", ccall);
		ecall_sync_decoders(ccall->ecall);
	}

	if (list_changed) {
		ICALL_CALL_CB(ccall->icall, group_changedh,
			      &ccall->icall, ccall->icall.arg);	
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
		ccall_stop_others_ringing(ccall);
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
	int err = 0;

	if (!ccall || !msg ||
	    !userid_sender || !clientid_sender)
		return EINVAL;

	if (ECONN_CONF_START == msg->msg_type) {
		msg_ts = msg->u.confstart.timestamp;
		msg_seqno = msg->u.confstart.seqno;
		msg_sft_url = msg->u.confstart.sft_url;
		msg_sft_tuple = msg->u.confstart.sft_tuple;
		msg_secret = msg->u.confstart.secret;
		msg_secretlen = msg->u.confstart.secretlen;

		valid_call = msg->age < CCALL_CONFSTART_TIMEOUT_S;
	}
	else if (ECONN_CONF_CHECK == msg->msg_type) {
		msg_ts = msg->u.confcheck.timestamp;
		msg_seqno = msg->u.confcheck.seqno;
		msg_sft_url = msg->u.confcheck.sft_url;
		msg_sft_tuple = msg->u.confcheck.sft_tuple;
		msg_secret = msg->u.confcheck.secret;
		msg_secretlen = msg->u.confcheck.secretlen;

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
	     ccall->self == ccall->keygenerator ? "YES" : "NO");

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
		ccall->keygenerator = NULL;
	}

	switch (ccall->state) {
	case CCALL_STATE_IDLE:
		{
			should_ring = econn_message_isrequest(msg) &&
				ECONN_CONF_START == msg->msg_type &&
				((msg->age * 1000) < CCALL_SHOULD_RING_TIMEOUT);
			if (ccall->self && strcaseeq(userid_sender,
						     ccall->self->userid_real)) {
				should_ring = false;
			}

			ccall->is_ringing = should_ring;
			set_state(ccall, CCALL_STATE_INCOMING);

			ICALL_CALL_CB(ccall->icall, starth,
				      &ccall->icall, msg->time,
				      userid_sender, clientid_sender,
				      video, should_ring, ICALL_CONV_TYPE_CONFERENCE,
				      ccall->icall.arg);

			if (should_ring) {
				tmr_start(&ccall->tmr_ring,
					  CCALL_RINGER_TIMEOUT,
					  ccall_stop_ringing_timeout, ccall);
			}
		}
		break;

	case CCALL_STATE_INCOMING:
		if (strcaseeq(userid_sender, ccall->self->userid_real) &&
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
		break;

	case CCALL_STATE_CONNSENT:
	case CCALL_STATE_SETUPRECV:
	case CCALL_STATE_CONNECTING:
	case CCALL_STATE_CONNECTED:
		if (ts_cmp > 0) {
			/* If remote call is earlier, drop connection and
			   reconnect to the earlier call */
			ecall_end(ccall->ecall);
			ccall->ecall = NULL;

			set_state(ccall, CCALL_STATE_CONNSENT);
			return ccall_send_conf_conn(ccall, ccall->sft_url, false);
		}
		break;

	case CCALL_STATE_ACTIVE:
		if (ts_cmp > 0) {
			/* If remote call is earlier, drop connection and
			   reconnect to the earlier call */
			ecall_end(ccall->ecall);
			ccall->ecall = NULL;

			set_state(ccall, CCALL_STATE_CONNSENT);
			return ccall_send_conf_conn(ccall, ccall->sft_url, false);
		}
		else if (ts_cmp < 0 && ccall->self == ccall->keygenerator) {
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
					      &ccall->icall, 0, NULL,
					      ECONN_MESSAGE_TIME_UNKNOWN,
					      NULL, NULL, ccall->icall.arg);
			}
			break;

		case CCALL_STATE_ACTIVE:
			/* Inform clients that the call is in fact still ongoing */
			if (ccall->keygenerator == ccall->self) {
				tmr_cancel(&ccall->tmr_send_check);
				tmr_start(&ccall->tmr_send_check, 0,
					  ccall_send_check_timeout, ccall);
			}
			break;

		default:
			break;
		}
		break;

	case ECONN_CONF_KEY:
		if (msg->resp) {
			if (!ccall->keygenerator) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s when keygenerator is NULL!\n",
					ccall, anon_id(userid_anon, msg->src_userid));
				ccall->request_key = true;
				return 0;
			}

			if (ccall->keygenerator == ccall->self) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s when keygenerator is me!\n",
					ccall, anon_id(userid_anon, msg->src_userid));
				return 0;
			}

			if (!strcaseeq(userid_sender,   ccall->keygenerator->userid_real) ||
			    !strcaseeq(clientid_sender, ccall->keygenerator->clientid_real)) {
				warning("ccall(%p): msg_recv ignoring CONFKEY resp from "
					"%s when keygenerator is %s\n",
					ccall, anon_id(userid_anon, userid_sender),
					anon_id(userid_anon2, ccall->keygenerator->userid_real));
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

			if (ccall->keygenerator != ccall->self) {
				warning("ccall(%p): msg_recv ignoring CONFKEY req from "
					"%s when not the keygenerator!\n",
					ccall, anon_id(userid_anon, msg->src_userid));
				return 0;
			}

			u = find_userinfo_by_real(ccall,
						  msg->src_userid,
						  msg->src_clientid);
			if (!u) {
				warning("ccall(%p): msg_recv ignoring CONFKEY req from "
					"%s because their user not found!\n",
					ccall, anon_id(userid_anon, msg->src_userid));
				return 0;
			}
			if (!u->incall_now) {
				warning("ccall(%p): msg_recv ignoring CONFKEY req from "
					"%s because they arent in call!\n",
					ccall, anon_id(userid_anon, msg->src_userid));
				return 0;
			}
			u->needs_key = true;
			ccall_send_keys(ccall, false);
		}
		break;

	case ECONN_REJECT:
		if (ccall->state == CCALL_STATE_INCOMING &&
		    strcaseeq(userid_sender, ccall->self->userid_real)) {
			tmr_cancel(&ccall->tmr_ring);
			ICALL_CALL_CB(ccall->icall, leaveh,
				      &ccall->icall, ICALL_REASON_STILL_ONGOING,
				      ECONN_MESSAGE_TIME_UNKNOWN, ccall->icall.arg);
			ccall->is_ringing = false;
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
			info("ccall(%p): sft_msg_recv call rejected "
				"due to blacklist\n", ccall);
			tmr_start(&ccall->tmr_blacklist, 1,
				ccall_blacklisted_timeout, ccall);
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
					if (msg->u.setup.sft_tuple) {
						err = str_dup(&ccall->sft_tuple, msg->u.setup.sft_tuple);
						if (err) {
							goto out;
						}
	 				}
				}
			}
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

int  ccall_debug(struct re_printf *pf, const struct icall* icall)
{
	const struct ccall *ccall = (const struct ccall*)icall;
	char userid_anon[ANON_ID_LEN];
	char userid_anon2[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct userinfo *u;
	struct le *le;
	int err = 0;

	err = re_hprintf(pf, "\nCCALL SUMMARY %p:\n", ccall);
	if (err)
		goto out;

	err = re_hprintf(pf, "confstate: %s:\n", ccall_state_name(ccall->state));
	if (err)
		goto out;

	err = re_hprintf(pf, "\n");
	if (err)
		goto out;

	LIST_FOREACH(&ccall->partl, le) {
		u = le->data;
		err = re_hprintf(pf, "user hash: %s user: %s.%s incall: %s auth: %s ssrca: %u ssrcv: %u muted: %s vidstate: %s\n",
			anon_id(userid_anon, u->userid_hash),
			anon_id(userid_anon2, u->userid_real),
			anon_client(clientid_anon, u->clientid_real),
			u->incall_now ? "true" : "false",
			u->se_approved ? "true" : "false",
			u->ssrca, u->ssrcv,
			u->muted ? "true" : "false",
			icall_vstate_name(u->video_state));
		if (err)
			goto out;
	}

	err = re_hprintf(pf, "\n");
	if (err)
		goto out;

	LIST_FOREACH(&ccall->saved_partl, le) {
		const struct econn_group_part *p = le->data;

		u = find_userinfo_by_hash(ccall, p->userid, p->clientid);
		if (!u && strcaseeq(ccall->self->userid_hash, p->userid) &&
		    strcaseeq(ccall->self->clientid_hash, p->clientid)) {
			u = ccall->self;
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

	incall_clear(ccall, false);
	if (ccall->ecall)
		ecall_end(ccall->ecall);
	else
		set_state(ccall, CCALL_STATE_IDLE);

}

