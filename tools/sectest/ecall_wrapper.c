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
#include "ecall_wrapper.h"

#define SFT_URL      "http://sft01.avs.zinfra.io:8443"

static void ecall_wrapper_destructor(void *arg)
{
	struct ecall_wrapper *wrapper = (struct ecall_wrapper*)arg;

	tmr_cancel(&wrapper->call_timer);
	mem_deref(wrapper->name);
	mem_deref(wrapper->url);
	mem_deref(wrapper->icall);
}

static int ecall_sft_response_handler(struct icall *icall,
				      int http_err,
				      struct econn_message *msg,
				      void *arg)
{
	int err = 0;

	if (msg->msg_type == ECONN_SETUP) {
		err = ICALL_CALLE(icall, msg_recv,
				  0, 0, "SFT", "SFT", msg);
		if (err)
			goto out;
	}

out:
	return err;
}

static int ecall_sft_handler(struct icall *icall,
			     const char *url,
			     struct econn_message *msg,
			     void *arg)
{
	//struct ecall_wrapper *wrapper = (struct ecall_wrapper*)arg;
	int err = 0;

	err = http_send_message(icall, url, msg, ecall_sft_response_handler, arg);
	if (err)
		goto out;

out:
	return err;
}

static int ecall_send_handler(struct icall *icall,
			      const char *userid,
			      struct econn_message *msg,
			      struct list *targets,
			      bool my_clients_only,
			      void *arg)
{
	struct ecall_wrapper *wrapper = (struct ecall_wrapper*)arg;

	if (msg->msg_type == ECONN_SETUP) {
		return ecall_sft_handler(icall, wrapper->url, msg, arg);
	}

	return 0;
}

static void ecall_start_handler(struct icall *icall,
				uint32_t msg_time,
				const char *userid_sender,
				const char *clientid_sender,
				bool video,
				bool should_ring,
				enum icall_conv_type conv_type,
				void *arg)
{
	int err = 0;

	err = ICALL_CALLE(icall, answer,
			  ICALL_CALL_TYPE_NORMAL, false);
	if (err)
		printf("ERR ecall answer returned err=%d\n", err);
}

static void ecall_answer_handler(struct icall *icall,
				 void *arg)
{
}

static void ecall_datachan_estab_handler(struct icall *icall,
					 const char *userid,
					 const char *clientid,
					 bool update,
					 void *arg)
{
}

static void ecall_close_handler(struct icall *icall, int err,
				struct icall_metrics *metrics,
				uint32_t msg_time,
				const char *userid, const char *clientid,
				void *arg)
{
}

static void end_call_timeout(void *arg)
{
	struct ecall_wrapper *wrapper = (struct ecall_wrapper*)arg;

	ecall_stats_struct((struct ecall*)wrapper->icall,
			   &wrapper->stats);

	printf("    %s leaving the call\n", wrapper->name);
	ICALL_CALL(wrapper->icall, end);
}

static void ecall_audio_estab_handler(struct icall *icall, const char *userid,
				      const char *clientid, bool update,
				      void *arg)
{
	struct ecall_wrapper *wrapper = (struct ecall_wrapper*)arg;

	printf("    %s is in the call\n", wrapper->name);
	tmr_start(&wrapper->call_timer, 15000, end_call_timeout, wrapper);
}

static void ecall_confmsg_handler(struct ecall *ecall,
				  const struct econn_message *msg,
				  void *arg)
{
	struct ecall_wrapper *wrapper = (struct ecall_wrapper*)arg;
	struct econn_message *rmsg = NULL;
	char *str = NULL;
	struct le *le = NULL;
	struct mbuf mb;
	int err = 0;

	/* Store the first CONFPART contents for inspection */
	if (msg->msg_type == ECONN_CONF_PART &&
	    list_count(&wrapper->partl) == 0) {

		LIST_FOREACH(&msg->u.confpart.partl, le) {
			struct econn_group_part *p = le->data;
			struct econn_group_part *clone;

			if (strcmp(p->userid, wrapper->name) != 0) {
				clone = econn_part_alloc(p->userid, p->clientid);
				if (clone)
					list_append(&wrapper->partl, &clone->le, clone);
			}
		}
	}

	/* Authorise all other clients to test we dont get media */
	if (wrapper->fake_auth && msg->msg_type == ECONN_CONF_PART) {

		rmsg = econn_message_alloc();
		if (rmsg == NULL)
			return;

		str_ncpy(rmsg->src_userid, wrapper->name, ECONN_ID_LEN);
		str_ncpy(rmsg->src_clientid, wrapper->name, ECONN_ID_LEN);
		rmsg->msg_type = ECONN_CONF_PART;;
		rmsg->resp = true;

		str_ncpy(rmsg->dest_userid, "SFT", ECONN_ID_LEN);
		str_ncpy(rmsg->dest_clientid, "SFT", ECONN_ID_LEN);

		LIST_FOREACH(&msg->u.confpart.partl, le) {
			struct econn_group_part *p = le->data;
			struct econn_group_part *clone;

			if (strcmp(p->userid, wrapper->name) != 0) {
				clone = econn_part_alloc(p->userid, p->clientid);
				if (!clone)
					break;
				clone->ssrca = p->ssrca;
				clone->ssrcv = p->ssrcv;
				clone->authorized = true;
				list_append(&rmsg->u.confpart.partl, &clone->le, clone);

				err =  ecall_add_decoders_for_user((struct ecall*)wrapper->icall,
								   p->userid,
								   p->clientid,
								   p->userid,
								   p->ssrca,
								   p->ssrcv);
				if (err)
					goto out;
			}
		}

		ecall_sync_decoders((struct ecall*)wrapper->icall);

		err = econn_message_encode(&str, rmsg);
		if (err)
			goto out;

		mb.pos = 0;
		mb.size = str_len(str);
		mb.end = mb.size;
		mb.buf = (uint8_t *)str;

		printf("    %s faking auth message to SFT\n", wrapper->name);
		err =  ecall_dce_send(ecall, &mb);
		if (err) {
			goto out;
		}
	}

out:
	if (err)
		printf("ERR failed to handle confpart properly\n");
	mem_deref(rmsg);
	mem_deref(str);
	//ICALL_CALL(wrapper->icall, end);
}

struct ecall_wrapper *init_ecall(const char *name, bool fake_auth)
{
	struct ecall_wrapper *wrapper = NULL;
	struct ecall *ecall = NULL;
	int err = 0;

	wrapper = (struct ecall_wrapper*)mem_zalloc(sizeof(struct ecall_wrapper),
						    ecall_wrapper_destructor);
	if (!wrapper)
		return NULL;

	err = str_dup(&wrapper->name, name);
	if (err)
		goto out;

	wrapper->fake_auth = fake_auth;
	err = ecall_alloc(&ecall, NULL,
			  ICALL_CONV_TYPE_CONFERENCE,
			  ICALL_CALL_TYPE_NORMAL,
			  NULL,
			  msystem_instance(),
			  "guess",
			  name,
			  name);
	if (err)
		goto out;

	wrapper->icall = ecall_get_icall(ecall);
	icall_set_callbacks(wrapper->icall,
			    ecall_send_handler,
			    NULL, // sft_handler,
			    ecall_start_handler,
			    ecall_answer_handler,
			    NULL, // media_estab_handler,
			    ecall_audio_estab_handler,
			    ecall_datachan_estab_handler,
			    NULL, // media_stopped_handler,
			    NULL, // group_changed_handler,
			    NULL, // leave_handler,
			    ecall_close_handler,
			    NULL, // metrics_handler,
			    NULL, // vstate_handler,
			    NULL, // audiocbr_handler,
			    NULL, // muted_changed_handler,
			    NULL, // quality_handler,
			    NULL, // no_relay_handler,
			    NULL, // req_clients_handler,
			    NULL, // aulevel_handler,
			    NULL, // req_new_epoch_handler,
			    wrapper);
	err = ICALL_CALLE(wrapper->icall, add_sft,
		SFT_URL);
	if (err)
		goto out;

	ecall_set_confmsg_handler(ecall, ecall_confmsg_handler);
	tmr_init(&wrapper->call_timer);
out:
	if (err)
		wrapper = (struct ecall_wrapper*)mem_deref(wrapper);
		
	return wrapper;
}


void ecall_wrapper_join_call(struct ecall_wrapper *wrapper,
			     const char *callid)
{
	struct econn_message *msg = NULL;
	const char *sft_url = NULL;
	size_t len = 0;
	int err = 0;

	printf("    %s attempting to join call %s\n", wrapper->name, callid);

	msg = econn_message_alloc();
	if (msg == NULL)
		return;

	str_ncpy(msg->src_userid, wrapper->name, ECONN_ID_LEN);
	str_ncpy(msg->src_clientid, wrapper->name, ECONN_ID_LEN);
	msg->msg_type = ECONN_CONF_CONN;;
	msg->resp = false;

	str_ncpy(msg->dest_userid, "SFT", ECONN_ID_LEN);
	str_ncpy(msg->dest_clientid, "SFT", ECONN_ID_LEN);

	err = str_dup(&msg->u.confconn.tool, avs_version_str());
	if (err) {
		goto out;
	}
	err = str_dup(&msg->u.confconn.toolver, avs_version_short());
	if (err) {
		goto out;
	}
	msg->u.confconn.update = false;
	msg->u.confconn.selective_audio = true;
	msg->u.confconn.selective_video = true;
	msg->u.confconn.vstreams = 4;

	sft_url = system_get_sft_url();
	len = strlen(sft_url) + 5 + strlen(callid) + 2;

	wrapper->url = mem_zalloc(len, NULL);

	snprintf(wrapper->url, len - 1, "%s/sft/%s", sft_url, callid);

	ecall_sft_handler(wrapper->icall, wrapper->url, msg, wrapper);

out:
	mem_deref(msg);
}


