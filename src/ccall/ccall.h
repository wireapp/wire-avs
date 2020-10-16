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

#define CCALL_CONNECT_TIMEOUT          ( 15000)
#define CCALL_SHOULD_RING_TIMEOUT      ( 30000)
#define CCALL_RINGER_TIMEOUT           ( 30000)

#define CCALL_CONFSTART_TIMEOUT_S      (   120)
#define CCALL_SEND_CHECK_TIMEOUT       ( 60000)
#define CCALL_ONGOING_CALL_TIMEOUT     ( 90000)
#define CCALL_ROTATE_KEY_TIMEOUT       ( 30000)
#define CCALL_ROTATE_KEY_FIRST_TIMEOUT (  5000)

#define CCALL_SECRET_LEN               (    16)

struct userinfo {
	struct le le;
	char *userid_real;
	char *userid_hash;
	char *clientid_real;
	char *clientid_hash;

	uint32_t ssrca;
	uint32_t ssrcv;
	int video_state;
	bool muted;

	bool needs_key;

	bool incall_now;
	bool incall_prev;
	bool se_approved;
	bool force_decoder;
	uint32_t listpos;
};

struct sftconfig {
	struct le le;
	char *url;
};

enum sreason {
	CCALL_STOP_RINGING_NONE     = 0,
	CCALL_STOP_RINGING_ANSWERED = 1,
	CCALL_STOP_RINGING_REJECTED = 2
};

struct ccall {
	struct icall icall;

	struct zapi_ice_server *turnv;
	size_t turnc;

	char *convid_real;
	char *convid_hash;
	struct userinfo *self;
	struct userinfo *keygenerator; // points to self or a member of partl
	struct list partl;
	struct mbuf confpart_data;

	const struct ecall_conf *conf;
	enum ccall_state state;

	char *sft_url;
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

	bool someone_left;
	bool request_key;
	int error;

	struct tmr tmr_connect;
	struct tmr tmr_call;
	struct tmr tmr_send_check;
	struct tmr tmr_ongoing;
	struct tmr tmr_rotate_key;
	struct tmr tmr_ring;
	struct tmr tmr_blacklist;
	struct tmr tmr_vstate;

	struct config *cfg;
};

