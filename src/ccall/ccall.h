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

#define CCALL_CONNECT_TIMEOUT          (   15000)
#define CCALL_SHOULD_RING_TIMEOUT      (   30000)
#define CCALL_RINGER_TIMEOUT           (   30000)

#define CCALL_CONFSTART_TIMEOUT_S      (     120)
#define CCALL_SEND_CHECK_TIMEOUT       (   60000)
#define CCALL_ONGOING_CALL_TIMEOUT     (   90000)
#define CCALL_ROTATE_KEY_TIMEOUT       (   30000)
#define CCALL_ROTATE_KEY_FIRST_TIMEOUT (    5000)
#define CCALL_DECRYPT_CHECK_TIMEOUT    (   10000)
#define CCALL_KEEPALIVE_TIMEOUT        (    5000)
#define CCALL_MAX_MISSING_PINGS        (       4)
#define CCALL_NOONE_JOINED_TIMEOUT     (  300000)
#define CCALL_EVERYONE_LEFT_TIMEOUT    (   30000)
#define CCALL_ROTATE_MLS_TIMEOUT       (    5000)
#define CCALL_MLS_KEY_AGE              (   60000)
#define CCALL_REQ_NEW_EPOCH_TIMEOUT    (82800000)

#define CCALL_SECRET_LEN               (      16)
#define CCALL_MAX_RECONNECT_ATTEMPTS   (       2)

#define CCALL_MAX_VSTREAMS             (       9)

struct sftconfig {
	struct le le;
	char *url;
};

enum sreason {
	CCALL_STOP_RINGING_NONE     = 0,
	CCALL_STOP_RINGING_ANSWERED = 1,
	CCALL_STOP_RINGING_REJECTED = 2
};

struct join_elem {
	struct ccall *ccall;
	enum icall_call_type call_type;
	bool audio_cbr;

	struct config_update_elem upe;
};

struct ccall {
	struct icall icall;

	struct zapi_ice_server *turnv;
	size_t turnc;

	char *convid_real;
	char *convid_hash;

	struct userlist *userl;

	struct mbuf confpart_data;
	struct list saved_partl;

	const struct ecall_conf *conf;
	enum ccall_state state;
	bool is_mls_call;

	char *primary_sft_url;
	char *sft_url;
	char *sft_tuple;
	bool sft_resolved;
	struct list sftl;

	uint8_t *secret;
	size_t  secret_len;

	uint64_t sft_timestamp;
	uint32_t sft_seqno;

	struct ecall *ecall;
	bool is_caller;
	bool is_ringing;
	enum sreason stop_ringing_reason;;
	struct keystore *keystore;

	enum icall_call_type call_type;
	enum icall_vstate vstate;

	bool someone_joined;
	bool someone_left;
	bool became_kg;
	bool request_key;
	uint32_t reconnect_attempts;
	uint32_t expected_ping;
	uint64_t last_ping;
	int received_confpart;
	int error;

	uint64_t epoch_start_ts;
	struct tmr tmr_connect;
	struct tmr tmr_call;
	struct tmr tmr_send_check;
	struct tmr tmr_ongoing;
	struct tmr tmr_rotate_key;
	struct tmr tmr_rotate_mls;
	struct tmr tmr_ring;
	struct tmr tmr_blacklist;
	struct tmr tmr_vstate;
	struct tmr tmr_decrypt_check;
	struct tmr tmr_keepalive;
	struct tmr tmr_alone;

	struct join_elem *je;
	struct config *cfg;

	uint64_t quality_interval;
};

