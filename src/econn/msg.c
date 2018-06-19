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
#include "avs_media.h"
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

	err = re_hprintf(pf, "%s | %s | sessid_sender=%s\n",
			 econn_msg_name(msg->msg_type),
			 msg->resp ? "Response" : "Request",
			 msg->sessid_sender);

	switch (msg->msg_type) {

	case ECONN_SETUP:
		err |= re_hprintf(pf, "sdp: \"%s\"\n",
				  msg->u.setup.sdp_msg);
		break;

	case ECONN_CANCEL:
		break;

	case ECONN_HANGUP:
		break;

	case ECONN_REJECT:
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
