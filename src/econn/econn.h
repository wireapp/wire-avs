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


#define ECONN_MAGIC 0xec000033


/*
 * This object defines 1 (ONE) encrypted connection. It is a logical
 * mapping from the local client to the remote USER+CLIENT.
 */
struct econn {
	struct econn_conf conf;
	struct tmr tmr_local;
	struct econn_transp *transp;
	enum econn_state state;
	enum econn_dir dir;

	char *userid_self;
	char *clientid;

	char sessid_local[64];
	char sessid_remote[64];
	char userid_remote[64];
	char clientid_remote[32];
	int setup_err;
	int conflict;

	econn_conn_h *connh;
	econn_answer_h *answerh;
	econn_update_req_h *update_reqh;
	econn_update_resp_h *update_resph;
	econn_alert_h *alerth;
	econn_close_h *closeh;
	void *arg;
	int err;

	uint32_t magic;
};


void econn_handle_event(struct econn *conn, int event);
int  econn_transp_send(struct econn *conn, struct econn_message *msg);


