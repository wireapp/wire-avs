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

#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_icall.h"
#include "avs_econn.h"


static void msg_destructor(void *data)
{
	struct econn_message *msg = data;

	econn_message_reset(msg);
}


struct econn_message *econn_message_alloc(void)
{
	return mem_zalloc(sizeof(struct econn_message), msg_destructor);
}


int econn_message_init(struct econn_message *msg, enum econn_msg msg_type,
		       const char *sessid_sender)
{
	if (!msg)
		return EINVAL;

	memset(msg, 0, sizeof(*msg));

	msg->msg_type = msg_type;
	str_ncpy(msg->sessid_sender, sessid_sender,
		 sizeof(msg->sessid_sender));

	return 0;
}


void econn_message_reset(struct econn_message *msg)
{
	if (!msg)
		return;

	switch (msg->msg_type) {

	case ECONN_SETUP:
	case ECONN_UPDATE:
	case ECONN_GROUP_SETUP:
		msg->u.setup.sdp_msg = mem_deref(msg->u.setup.sdp_msg);
		msg->u.setup.props = mem_deref(msg->u.setup.props);
		msg->u.setup.url = mem_deref(msg->u.setup.url);
		msg->u.setup.sft_tuple = mem_deref(msg->u.setup.sft_tuple);
		break;

	case ECONN_PROPSYNC:
		msg->u.propsync.props = mem_deref(msg->u.propsync.props);
		break;

	case ECONN_DEVPAIR_PUBLISH:
		msg->u.devpair_publish.sdp =
			mem_deref(msg->u.devpair_publish.sdp);
		msg->u.devpair_publish.username =
			mem_deref(msg->u.devpair_publish.username);
		break;

	case ECONN_DEVPAIR_ACCEPT:
		msg->u.devpair_accept.sdp =
			mem_deref(msg->u.devpair_accept.sdp);
		break;

	case ECONN_ALERT:
		msg->u.alert.descr = mem_deref(msg->u.alert.descr);
		break;

	case ECONN_GROUP_START:
		msg->u.groupstart.props = mem_deref(msg->u.groupstart.props);
		break;

	case ECONN_CONF_CONN:
		msg->u.confconn.turnv = mem_deref(msg->u.confconn.turnv);
		msg->u.confconn.turnc = 0;
		msg->u.confconn.tool = mem_deref(msg->u.confconn.tool);
		msg->u.confconn.toolver = mem_deref(msg->u.confconn.toolver);
		msg->u.confconn.selective_audio = false;
		msg->u.confconn.selective_video = false;
		msg->u.confconn.vstreams = 0;
		msg->u.confconn.sft_url = mem_deref(msg->u.confconn.sft_url);
		msg->u.confconn.sft_tuple = mem_deref(msg->u.confconn.sft_tuple);
		msg->u.confconn.sft_username = mem_deref(msg->u.confconn.sft_username);
		msg->u.confconn.sft_credential = mem_deref(msg->u.confconn.sft_credential);
		break;

	case ECONN_CONF_START:
		msg->u.confstart.props = mem_deref(msg->u.confstart.props);
		msg->u.confstart.sft_url = mem_deref(msg->u.confstart.sft_url);
		msg->u.confstart.sft_tuple = mem_deref(msg->u.confstart.sft_tuple);
		msg->u.confstart.secret = mem_deref(msg->u.confstart.secret);
		list_flush(&msg->u.confstart.sftl);
		break;

	case ECONN_CONF_CHECK:
		msg->u.confcheck.sft_url = mem_deref(msg->u.confcheck.sft_url);
		msg->u.confcheck.sft_tuple = mem_deref(msg->u.confcheck.sft_tuple);
		msg->u.confcheck.secret = mem_deref(msg->u.confcheck.secret);
		list_flush(&msg->u.confcheck.sftl);
		break;

	case ECONN_CONF_PART:
		list_flush(&msg->u.confpart.partl);
		msg->u.confpart.entropy = mem_deref(msg->u.confpart.entropy);
		list_flush(&msg->u.confpart.sftl);
		break;

	case ECONN_CONF_STREAMS:
		list_flush(&msg->u.confstreams.streaml);
		msg->u.confstreams.mode = mem_deref(msg->u.confstreams.mode);
		break;

	case ECONN_PING:
		break;

	case ECONN_CONF_END:
		break;

	case ECONN_CONF_KEY:
		list_flush(&msg->u.confkey.keyl);
		break;
		

	default:
		break;
	}

	memset(msg, 0, sizeof(*msg));
}


bool econn_message_isrequest(const struct econn_message *msg)
{
	return msg ? !msg->resp : false;
}


int econn_message_print(struct re_printf *pf, const struct econn_message *msg)
{
	int err = 0;

	if (!msg)
		return 0;

	err = re_hprintf(pf, "%s | %s | sessid_sender=%s",
			 econn_msg_name(msg->msg_type),
			 msg->resp ? "Response" : "Request",
			 msg->sessid_sender);

	switch (msg->msg_type) {

	case ECONN_SETUP:
		err |= re_hprintf(pf, "\nsdp: \"%s\"\n",
				  msg->u.setup.sdp_msg);
		break;

	case ECONN_CANCEL:
		break;

	case ECONN_HANGUP:
		break;

	case ECONN_REJECT:
		break;

	case ECONN_CONF_PART:
		err |= re_hprintf(pf, " ts: %u.%u\n",
				  msg->u.confpart.timestamp,
				  msg->u.confpart.seqno);
		break;

	case ECONN_CONF_START:
		err |= re_hprintf(pf, " ts: %u.%u\n",
				  msg->u.confstart.timestamp,
				  msg->u.confstart.seqno);
		break;

	case ECONN_CONF_CHECK:
		err |= re_hprintf(pf, " ts: %u.%u\n",
				  msg->u.confcheck.timestamp,
				  msg->u.confcheck.seqno);
		break;

	default:
		break;
	}

	return err;
}


int econn_message_brief(struct re_printf *pf, const struct econn_message *msg)
{
	int err = 0;

	if (!msg)
		return 0;

	err = re_hprintf(pf, "%s | %s | sessid_sender=%s",
			 econn_msg_name(msg->msg_type),
			 msg->resp ? "Response" : "Request",
			 msg->sessid_sender);

	return err;
}
