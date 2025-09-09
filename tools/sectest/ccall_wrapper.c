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

#include <re.h>
#include <avs.h>
#include "system.h"
#include "http.h"
#include "ccall_wrapper.h"
#include "ecall_wrapper.h"

#define SFT_URL      "http://sft01.avs.zinfra.io:8443"

static void ccall_wrapper_destructor(void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;

	tmr_cancel(&wrapper->call_timer);
	tmr_cancel(&wrapper->key_timer);
	mem_deref(wrapper->name);
	mem_deref(wrapper->icall);
	mem_deref(wrapper->callid);
	mem_deref(wrapper->userid);
}

static int ccall_send_handler(struct icall *icall,
			      const char *userid,
			      struct econn_message *msg,
			      struct list *targets,
			      bool my_clients_only,
			      void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;
	int err = 0;

	//printf("%s: %s send %s(%s) message\n", __FUNCTION__, wrapper->name,
	//       econn_msg_name(msg->msg_type), msg->resp ? "resp" : "req");

	/* Update the SFT URL with the selected one */
	if (msg->msg_type == ECONN_CONF_START)
		system_update_sft_url(msg->u.confstart.sft_url);

	/* Send messages to the client in conv_member */
	if (wrapper->conv_member) {
		err = ICALL_CALLE(wrapper->conv_member->icall, msg_recv,
				  0, 0, msg->src_userid, msg->src_clientid, msg);
		if (err)
			goto out;
	}

out:
	return err;
}

static int ccall_sft_response_handler(struct icall *icall,
				      int http_err,
				      struct econn_message *msg,
				      void *arg)
{
	int err = 0;

	err = ICALL_CALLE(icall, sft_msg_recv, http_err, msg);
	if (err)
		goto out;

out:
	return err;
}

static int ccall_sft_handler(struct icall *icall,
			     const char *url,
			     struct econn_message *msg,
			     void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;
	const char *cid = NULL;
	const char *c = NULL;
	int err = 0;

	//printf("%s: %s url %s\n", __FUNCTION__, wrapper->name,
	//       url);

	for (c = url; *c != '\0'; c++) {
		if (*c == '/')
			cid = c + 1;
	}

	wrapper->callid = (char*)mem_deref(wrapper->callid);
	err = str_dup(&wrapper->callid, cid);
	if (err)
		goto out;

	wrapper->userid = (char*)mem_deref(wrapper->userid);
	err = str_dup(&wrapper->userid, msg->src_userid);
	if (err)
		goto out;

	if (wrapper->contact_sft) {
		err = http_send_message(icall, url, msg, ccall_sft_response_handler, arg);
		if (err)
			goto out;
	}

out:
	return err;
}

static void ccall_start_handler(struct icall *icall,
				uint32_t msg_time,
				const char *userid_sender,
				const char *clientid_sender,
				bool video,
				bool should_ring,
				enum icall_conv_type conv_type,
				void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;
	int err = 0;

	printf("    %s answering call\n", wrapper->name);
	err = ICALL_CALLE(icall, answer,
			  ICALL_CALL_TYPE_NORMAL, false);
	if (err)
		printf("ERR answer returned err=%d\n", err);
}

static void ccall_answer_handler(struct icall *icall,
				 void *arg)
{
}

static void ccall_req_clients_handler(struct icall *icall,
				      void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;
	struct list clientl = LIST_INIT;
	struct icall_client *cli;

	if (wrapper->conv_member) {
		cli = icall_client_alloc(wrapper->conv_member->name, wrapper->conv_member->name);
		cli->in_subconv = true;
		if (cli) {
			//printf("adding client %s\n", wrapper->conv_member->name);
			list_append(&clientl, &cli->le, cli);
		}
	}
	ICALL_CALL(icall, set_clients,
		&clientl, 0);
}

static void ccall_datachan_estab_handler(struct icall *icall,
					 const char *userid,
					 const char *clientid,
					 bool update,
					 void *arg)
{
}

static void ccall_close_handler(struct icall *icall, int err,
				struct icall_metrics *metrics,
				uint32_t msg_time,
				const char *userid, const char *clientid,
				void *arg)
{
}

static void end_call_timeout(void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;

	ccall_stats_struct((struct ccall*)wrapper->icall, &wrapper->stats);
	printf("    %s leaving the call\n", wrapper->name);
	ICALL_CALL(wrapper->icall, end);
	tmr_cancel(&wrapper->key_timer);
}

/* Simulates a message from eve to try to set the key to a known value */
static void force_key_timeout(void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;
	struct econn_message *msg = NULL;
	struct econn_key_info *key = NULL;
	struct keystore *ks = NULL;
	uint32_t idx = 0;
	int err = 0;

	printf("    %s attempting to force key\n", wrapper->name);

	msg = econn_message_alloc();
	if (msg == NULL)
		return;

	/* eve pretends the message is from alice */
	str_ncpy(msg->src_userid, "alice", ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, "alice", ECONN_ID_LEN);
	msg->msg_type = ECONN_CONF_KEY;;
	msg->resp = true;

	str_ncpy(msg->dest_userid, wrapper->name, ECONN_ID_LEN);
	str_ncpy(msg->dest_clientid, wrapper->name, ECONN_ID_LEN);

	key = econn_key_info_alloc(E2EE_SESSIONKEY_SIZE);
	if (!key) {
		err = ENOMEM;
		goto out;
	}

	/* Key 0x10000 simuates next key after a client left */
	key->idx = 0x10000;
	memcpy(key->data, wrapper->attempt_key, E2EE_SESSIONKEY_SIZE);
	key->dlen = E2EE_SESSIONKEY_SIZE;

	list_append(&msg->u.confkey.keyl, &key->le, key);

	/* eve cannot fake the "from" here, because it comes from the crypto session */
	err = ICALL_CALLE(wrapper->icall, msg_recv,
			  0, 0, "eve", "eve", msg);
	if (err)
		goto out;

	ks = ccall_get_keystore((struct ccall*)wrapper->icall);
	if (ks) {
		err = keystore_get_next_session_key(ks, &idx,
						    wrapper->read_key,
						    E2EE_SESSIONKEY_SIZE);
		/* Its OK for this to fail */
		if (err)
			err = 0;
	}
out:
	mem_deref(msg);
}

/* Checks the current media key used */
static void check_key(struct ccall_wrapper *wrapper)
{
	struct keystore *ks = NULL;
	uint64_t updated_ts = 0;
	uint32_t kid = 0;
	int err = 0;

	ks = ccall_get_keystore((struct ccall*)wrapper->icall);
	if (ks) {
		err = keystore_get_current(ks, &kid, &updated_ts);
		if (err)
			warning("keystore_get_current failed (%d)\n", err);

		if (kid != wrapper->current_key_idx) {
			printf("    %s moved to key %u\n", wrapper->name, kid);
			wrapper->current_key_idx = kid;
		}
	}
}

/* Sets the MLS key to the next one. Called once per second to simulate clients getting
   keys over time
*/
static void mls_key_timeout(void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;

	if (wrapper->next_mls_key < wrapper->target_mls_key) {
		uint8_t key[E2EE_SESSIONKEY_SIZE];

		wrapper->next_mls_key++;
		memset(key, 0, E2EE_SESSIONKEY_SIZE);
		snprintf((char*)key, E2EE_SESSIONKEY_SIZE - 1, "session key %02u", wrapper->next_mls_key);
		printf("    %s is setting key %u\n", wrapper->name, wrapper->next_mls_key);
		ICALL_CALL(wrapper->icall, set_media_key,
			wrapper->next_mls_key, key, E2EE_SESSIONKEY_SIZE);

	}
	check_key(wrapper);
	tmr_start(&wrapper->key_timer, 1000, mls_key_timeout, wrapper);
}

static void ccall_audio_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update,
				      void *arg)
{
	struct ccall_wrapper *wrapper = (struct ccall_wrapper*)arg;

	printf("    %s is in the call\n", wrapper->name);
	if (wrapper->eavesdropper) {
		printf("    %s informing eavesdropper\n", wrapper->name);
		ecall_wrapper_join_call(wrapper->eavesdropper, wrapper->callid);
	}

	msystem_stop_silencing();

	if (wrapper->attempt_force_key) {
		tmr_start(&wrapper->key_timer, 5000, force_key_timeout, wrapper);
	}

	if (wrapper->target_mls_key > 0) {
		/* Set MLS keys & give longer timeout for MLS tests */
		tmr_start(&wrapper->key_timer, 5000, mls_key_timeout, wrapper);
	}
	tmr_start(&wrapper->call_timer, wrapper->test_timeout, end_call_timeout, wrapper);
}

static void ccall_media_estab_handler(struct icall *icall,
				      const char *userid,
				      const char *clientid,
				      bool update,
				      void *arg)
{
	int err;

	err = ICALL_CALLE(icall, media_start);
	if (err) {
		warning("icall_media_start failed (%d)\n", err);
	}
}

struct ccall_wrapper *init_ccall(const char *name,
			         const char *convid,
				 bool contact_sft,
				 bool mls_call)
{
	struct ccall_wrapper *wrapper = NULL;
	struct ccall *ccall = NULL;
	int err = 0;

	wrapper = (struct ccall_wrapper*)mem_zalloc(sizeof(struct ccall_wrapper),
						    ccall_wrapper_destructor);
	if (!wrapper)
		return NULL;

	err = str_dup(&wrapper->name, name);
	if (err)
		goto out;

	err = ccall_alloc(&ccall, 
			  NULL,		 
			  convid,
			  name,
			  name,
			  mls_call,
			  false);
	if (err)
		goto out;

	wrapper->icall = ccall_get_icall(ccall);
	icall_set_callbacks(wrapper->icall,
			    ccall_send_handler,
			    ccall_sft_handler,
			    ccall_start_handler,
			    ccall_answer_handler,
			    ccall_media_estab_handler,
			    ccall_audio_estab_handler,
			    ccall_datachan_estab_handler,
			    NULL, // media_stopped_handler,
			    NULL, // group_changed_handler,
			    NULL, // leave_handler,
			    ccall_close_handler,
			    NULL, // metrics_handler,
			    NULL, // vstate_handler,
			    NULL, // audiocbr_handler,
			    NULL, // muted_changed_handler,
			    NULL, // quality_handler,
			    NULL, // no_relay_handler,
			    ccall_req_clients_handler,
			    NULL, // aulevel_handler,
			    NULL, // req_new_epoch_handler,
			    wrapper);
	err = ICALL_CALLE(wrapper->icall, add_sft,
		SFT_URL);
	if (err)
		goto out;

	err = ccall_set_config(ccall, system_get_config());
	if (err)
		goto out;

	tmr_init(&wrapper->call_timer);
	tmr_init(&wrapper->key_timer);
	wrapper->contact_sft = contact_sft;
	wrapper->test_timeout = 15000;
out:
	if (err)
		wrapper = (struct ccall_wrapper*)mem_deref(wrapper);
		
	return wrapper;
}

void ccall_wrapper_set_eavesdropper(struct ccall_wrapper *wrapper,
				struct ecall_wrapper *eavesdropper)
{
	wrapper->eavesdropper = eavesdropper;
}

void ccall_attempt_force_key(struct ccall_wrapper *wrapper, 
			    uint8_t set_key[E2EE_SESSIONKEY_SIZE])
{
	wrapper->attempt_force_key = true;
	memcpy(wrapper->attempt_key, set_key, E2EE_SESSIONKEY_SIZE);
}

void ccall_set_target_mls_key(struct ccall_wrapper *wrapper, 
			    uint32_t mls_key)
{
	wrapper->target_mls_key = mls_key;
}


