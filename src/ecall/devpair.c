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
#include "avs_base.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_uuid.h"
#include "avs_msystem.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_icall.h"
#include "avs_ecall.h"
#include "avs_jzon.h"
#include "avs_version.h"
#include "avs_ztime.h"
#include "ecall.h"


int ecall_devpair_start(struct ecall *ecall)
{
	if (!ecall)
		return EINVAL;

	return ecall_start(ecall, false, false, NULL);
}


int ecall_devpair_ack(struct ecall *ecall,
		      struct econn_message *msg,
		      const char *pairid)
{
	struct econn_message *setup_msg;

	if (!ecall)
		return EINVAL;

	if (!ecall->econn) {
		warning("ecall: devpair_ack: not started\n");

		return EPROTO;
	}

	setup_msg = econn_message_alloc();
	econn_message_init(setup_msg, ECONN_SETUP, pairid);
	setup_msg->resp = true;
	str_dup(&setup_msg->u.setup.sdp_msg, msg->u.devpair_accept.sdp);

	/* forward the received message to ECONN */
	econn_recv_message(ecall_get_econn(ecall),
			   "devpair",
			   pairid,
			   setup_msg);

	mem_deref(setup_msg);

	return 0;
}


int ecall_devpair_answer(struct ecall *ecall,
			 struct econn_message *msg,
			 const char *pairid)
{
	struct econn_message *setup_msg;
	int err;

	if (!ecall)
		return EINVAL;

	if (ecall->econn) {
		warning("ecall: devpair_start: already in progress "
			"(econn=%s)\n",
			econn_state_name(econn_current_state(ecall->econn)));
		return EALREADY;
	}

	if (ecall->turnc == 0) {
		warning("ecall: devpair_start: no TURN servers "
			"-- cannot start\n");
		return EINTR;
	}

	err = ecall_create_econn(ecall);
	if (err) {
		warning("ecall: start: create_econn failed: %m\n", err);
		return err;
	}

	setup_msg = econn_message_alloc();
	econn_message_init(setup_msg, ECONN_SETUP, pairid);
	str_dup(&setup_msg->u.setup.sdp_msg, msg->u.devpair_publish.sdp);

	/* forward the received message to ECONN */
	econn_recv_message(ecall_get_econn(ecall),
			   "devpair",
			   pairid,
			   setup_msg);

	mem_deref(setup_msg);

	return 0;
}


void ecall_set_devpair(struct ecall *ecall, bool devpair)
{
	if (!ecall)
		return;

	ecall->devpair = devpair;
}
