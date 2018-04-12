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
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_econn.h"
#include "avs_log.h"


const char *econn_msg_name(enum econn_msg msg)
{
	switch (msg) {

	case ECONN_SETUP:		return "SETUP";
	case ECONN_CANCEL:		return "CANCEL";
	case ECONN_UPDATE:		return "UPDATE";
	case ECONN_HANGUP:		return "HANGUP";
	case ECONN_REJECT:		return "REJECT";
	case ECONN_PROPSYNC:		return "PROPSYNC";
	case ECONN_GROUP_START:		return "GROUPSTART";
	case ECONN_GROUP_LEAVE:		return "GROUPLEAVE";
	case ECONN_GROUP_CHECK:		return "GROUPCHECK";
	case ECONN_GROUP_SETUP:		return "GROUPSETUP";
	case ECONN_DEVPAIR_PUBLISH:     return "DEVPAIR_PUBLISH";
	case ECONN_DEVPAIR_ACCEPT:      return "DEVPAIR_ACCEPT";
	case ECONN_ALERT:               return "ALERT";
	default:            return "???";
	}
}


const char *econn_state_name(enum econn_state st)
{
	switch (st) {

	case ECONN_IDLE:                 return "Idle";
	case ECONN_PENDING_OUTGOING:     return "Pending-Outgoing";
	case ECONN_PENDING_INCOMING:     return "Pending-Incoming";
	case ECONN_CONFLICT_RESOLUTION:  return "Conflict-Resolution";
	case ECONN_ANSWERED:             return "Answered";
	case ECONN_DATACHAN_ESTABLISHED: return "DatachanEstablished";
	case ECONN_HANGUP_SENT:          return "HangupSent";
	case ECONN_HANGUP_RECV:          return "HangupRecv";
	case ECONN_TERMINATING:          return "Terminating";
	case ECONN_UPDATE_RECV:          return "UpdateRecv";
	case ECONN_UPDATE_SENT:          return "UpdateSent";

	default: return "???";
	}
}


const char *econn_dir_name(enum econn_dir dir)
{
	switch (dir) {

	case ECONN_DIR_UNKNOWN:  return "Unknown";
	case ECONN_DIR_OUTGOING: return "Outgoing";
	case ECONN_DIR_INCOMING: return "Incoming";
	default: return "???";
	}
}


const char *econn_transp_name(enum econn_transport tp)
{
	switch (tp) {

	case ECONN_TRANSP_BACKEND:    return "Backend";
	case ECONN_TRANSP_DIRECT:     return "Direct";
	default:                      return "???";
	}
}


bool econn_iswinner(const char *userid_self, const char *clientid,
		    const char *userid_remote, const char *clientid_remote)
{
	int r;

	r = strcmp(userid_self, userid_remote);

	if (r == 0) {

		r = strcmp(clientid, clientid_remote);
	}

	return r > 0;
}


bool econn_is_creator(const char *userid_self, const char *userid_remote,
		      const struct econn_message *msg)
{
	if (!str_isset(userid_self) || !str_isset(userid_remote) || !msg)
		return false;

	return 0 != str_casecmp(userid_self, userid_remote) &&
		econn_message_isrequest(msg) &&
		msg->msg_type == ECONN_SETUP;
}


enum econn_transport econn_transp_resolve(enum econn_msg type)
{
	switch (type) {

	case ECONN_SETUP:		return ECONN_TRANSP_BACKEND;
	case ECONN_CANCEL:		return ECONN_TRANSP_BACKEND;
	case ECONN_UPDATE:		return ECONN_TRANSP_BACKEND;
	case ECONN_HANGUP:		return ECONN_TRANSP_DIRECT;
	case ECONN_REJECT:		return ECONN_TRANSP_BACKEND;
	case ECONN_PROPSYNC:		return ECONN_TRANSP_DIRECT;
	case ECONN_GROUP_START:		return ECONN_TRANSP_BACKEND;
	case ECONN_GROUP_LEAVE:		return ECONN_TRANSP_BACKEND;
	case ECONN_GROUP_CHECK:		return ECONN_TRANSP_BACKEND;
	case ECONN_GROUP_SETUP:		return ECONN_TRANSP_BACKEND;
	case ECONN_DEVPAIR_PUBLISH:     return ECONN_TRANSP_BACKEND;
	case ECONN_DEVPAIR_ACCEPT:      return ECONN_TRANSP_BACKEND;
	case ECONN_ALERT:		return ECONN_TRANSP_BACKEND;

	default:
		warning("econn: transp_resolv: message type %d"
			" not supported\n", type);
		return (enum econn_transport)-1;
	}
}
