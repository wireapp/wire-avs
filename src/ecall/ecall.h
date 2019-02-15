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

#include "openssl/sha.h"

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
struct conf_part;

#ifndef _WIN32
#define ECALL_PACKED __attribute__((packed))
#else
#pragma pack (push, 1)
#define ECALL_PACKED
#endif

#define USER_MESSAGE_DATA               0
#define USER_MESSAGE_FILE_START         1
#define USER_MESSAGE_FILE               2
#define USER_MESSAGE_FILE_STATUS        3
#define USER_MESSAGE_FILE_STATUS_ACK    4
#define USER_MESSAGE_FILE_END           5
#define USER_MESSAGE_FILE_END_ACK       6

#define MAX_FILE_NAME_SIZE 128

struct data_payload {
	uint8_t data[MAX_USER_DATA_SIZE];
};

struct file_start_payload {
	uint32_t nblocks;
	char name[MAX_FILE_NAME_SIZE];
	uint8_t data[MAX_USER_DATA_SIZE];
};

struct file_status_payload {
	uint8_t data[MAX_USER_DATA_SIZE];
	uint8_t sha[SHA256_DIGEST_LENGTH];
};

struct user_data_message{
	uint8_t type;
	union payload {
		struct data_payload data_payload;
		struct file_start_payload file_start_payload;
		struct file_status_payload file_status_payload;
	} payload;
} ECALL_PACKED;

struct file_status {
	FILE *fp;
	uint32_t nblocks;
	uint32_t block_cnt;
	uint32_t byte_cnt;
	SHA256_CTX sha256_hash;
	char path[MAX_FILE_NAME_SIZE];
	char name[MAX_FILE_NAME_SIZE];
	bool active;
	struct ztime start_time;
};

struct user_data {
	struct dce_channel *dce_ch;
	ecall_user_data_ready_h *ready_h;
	ecall_user_data_rcv_h *rcv_h;
	ecall_user_data_file_rcv_h *f_rcv_h;
	ecall_user_data_file_snd_h *f_snd_h;
	void *arg;
	bool channel_open;
	struct tmr tmr;
	struct tmr ft_tmr;
	int32_t ft_chunk_size;
	uint8_t pending_msg_buf[sizeof(struct user_data_message)];
	size_t pending_msg_len;
	struct file_status file_snd;
	struct file_status file_rcv;
};

struct ecall {
	struct icall icall;

	struct le le;
	struct ecall_conf conf;
	struct msystem *msys;
	struct econn *econn;

	enum icall_conv_type conv_type;
	struct mediaflow *mf;
	struct dce *dce;
	struct dce_channel *dce_ch;
	struct user_data *usrd;

	struct tmr dc_tmr;
	struct tmr media_start_tmr;
	struct tmr update_tmr;

	struct econn_props *props_local;
	struct econn_props *props_remote;

	char *convid;
	char *userid_self;
	char *clientid_self;
	char *userid_peer;
	char *clientid_peer;

	uint32_t max_retries;
	uint32_t num_retries;

	struct {
		enum async_sdp async;
	} sdp;

	struct econn *econn_pending;
	bool answered;
	bool update;
	int32_t call_setup_time;
	int32_t call_estab_time;
	int32_t audio_setup_time;
	uint64_t ts_started;
	uint64_t ts_answered;
	enum media_crypto crypto;

	struct econn_transp transp;
	uint32_t magic;

	struct conf_part *conf_part;

	struct list tracel;
	uint64_t ts_start;

	bool devpair;

	bool audio_cbr;
	bool enable_video;
	bool group_mode;

	struct {
		int recv_state;
	} video;

	struct {
		int cbr_state;
	} audio;

	struct {
		struct tmr tmr;
		uint64_t interval;
	} quality;

	struct zapi_ice_server turnv[MAX_TURN_SERVERS];
	size_t turnc;
	bool turn_added;
};


bool ecall_stats_prepare(struct ecall *ecall, struct json_object *jobj,
			 int ecall_err);

struct conf_part *ecall_get_conf_part(struct ecall *ecall);
void ecall_set_conf_part(struct ecall *ecall, struct conf_part *cp);

int ecall_add_user_data_channel(struct ecall *ecall, bool should_open);
int ecall_create_econn(struct ecall *ecall);
void ecall_close(struct ecall *ecall, int err, uint32_t msg_time);

/* Ecall trace */


struct trace_entry {
	struct le le;

	uint64_t ts;
	bool tx;
	enum econn_transport tp;
	enum econn_msg msg_type;
	bool resp;
	uint32_t age;
};

int ecall_show_trace(struct re_printf *pf, const struct ecall *ecall);
