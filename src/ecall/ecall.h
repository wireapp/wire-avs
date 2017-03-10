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


enum async_sdp {
	ASYNC_NONE = 0,
	ASYNC_OFFER,
	ASYNC_ANSWER,
	ASYNC_COMPLETE
};


/*
 * The ECALL object exist for each 1:1 conversation and represents
 * the logical calling state between this device/conversation and
 * the remote user/device.
 *
 * An ECALL object has multiple ECONN objects
 *
 * An ECALL object has a single mediaflow object
 */
struct ecall {

	struct le le;
	struct ecall_conf conf;
	struct msystem *msys;
	struct econn *econn;

	struct mediaflow *mf;
	struct dce *dce;
	struct dce_channel *dce_ch;

	struct tmr tmr;

	struct econn_props *props_local;
	struct econn_props *props_remote;

	char *convid;
	char *userid_self;
	char *clientid;

	struct {
		enum async_sdp async;
	} sdp;

	struct {
		struct sa srv;
		char *user;
		char *pass;
	} turn;

	struct econn *econn_pending;
	bool answered;
	bool update;
	int32_t call_setup_time;
	int32_t call_estab_time;
	int32_t audio_setup_time;
	uint64_t ts_started;
	uint64_t ts_answered;

	struct econn_transp transp;
	ecall_conn_h *connh;
	ecall_answer_h *answerh;
	ecall_missed_h *missedh;
	ecall_transp_send_h *sendh;
	ecall_media_estab_h *media_estabh;
	ecall_audio_estab_h *audio_estabh;
	ecall_datachan_estab_h *datachan_estabh;
	ecall_propsync_h *propsynch;
	ecall_close_h *closeh;
	void *arg;

	uint32_t magic;
};


bool ecall_stats_prepare(struct ecall *ecall, struct json_object *jobj,
			 int ecall_err);
