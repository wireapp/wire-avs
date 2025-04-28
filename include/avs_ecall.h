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


struct ecall;
struct iflow_stats;

struct ecall_conf {
	struct econn_conf econf;
	int trace;
};


int  ecall_alloc(struct ecall **ecallp, struct list *ecalls,
		 enum icall_conv_type conv_type,
		 enum icall_call_type call_type,
		 const struct ecall_conf *conf,
		 struct msystem *msys,
		 const char *convid, const char *userid_self,
		 const char *clientid);
struct icall *ecall_get_icall(struct ecall* ecall);
int ecall_add_turnserver(struct ecall *ecall,
			 struct zapi_ice_server *srv);
int  ecall_start(struct ecall *ecall,
		 enum icall_call_type call_type,
		 bool audio_cbr);
int  ecall_answer(struct ecall *ecall,
		  enum icall_call_type call_type,
		  bool audio_cbr);
void ecall_transp_recv(struct ecall *ecall,
		       uint32_t curr_time, /* in seconds */
		       uint32_t msg_time, /* in seconds */
		       const char *userid_sender,
		       const char *clientid_sender,
		       const char *str);
int ecall_msg_recv(struct ecall *ecall,
		   uint32_t curr_time, /* in seconds */
		   uint32_t msg_time, /* in seconds */
		   const char *userid_sender,
		   const char *clientid_sender,
		   struct econn_message *msg);
void ecall_end(struct ecall *ecall);
void ecall_reject(struct ecall *ecall);
int ecall_set_sessid(struct ecall *ecall, const char *sessid);
void ecall_set_peer_userid(struct ecall *ecall, const char *userid);
void ecall_set_peer_clientid(struct ecall *ecall, const char *clientid);
int ecall_set_real_clientid(struct ecall *ecall, const char *clientid);
const char *ecall_get_peer_userid(const struct ecall *ecall);
const char *ecall_get_peer_clientid(const struct ecall *ecall);
struct ecall *ecall_find_convid(const struct list *ecalls,
				const char *convid);
struct ecall *ecall_find_userclient(const struct list *ecalls,
				    const char *userid,
				    const char *clientid);
int ecall_debug(struct re_printf *pf, const struct ecall *ecall);
int ecall_stats(struct re_printf *pf, const struct ecall *ecall);
int ecall_mfdebug(struct re_printf *pf, const struct ecall *ecall);
int ecall_stats_struct(const struct ecall *ecall,
		       struct iflow_stats *stats);
int ecall_activate(struct ecall *ecall, bool active);

//struct mediaflow *ecall_mediaflow(const struct ecall *ecall);
struct econn *ecall_get_econn(const struct ecall *ecall);
enum econn_state ecall_state(const struct ecall *ecall);
int ecall_media_start(struct ecall *ecall);
void ecall_media_stop(struct ecall *ecall);
int ecall_set_video_send_state(struct ecall *ecall, enum icall_vstate vstate);
bool ecall_is_answered(const struct ecall *ecall);
bool ecall_has_video(const struct ecall *ecall);
int ecall_sync_props(struct ecall *ecall, bool response);
const char *ecall_props_get_local(struct ecall *ecall, const char *key);
int ecall_props_set_local(struct ecall *ecall, const char *key, const char *value);
const char *ecall_props_get_remote(struct ecall *ecall, const char *key);
void ecall_trace(struct ecall *ecall, const struct econn_message *msg,
		 bool tx, enum econn_transport tp,
		 const char *fmt, ...);
int  ecall_restart(struct ecall *ecall,
		   enum icall_call_type call_type,
		   bool notify);

struct conf_part *ecall_get_conf_part(struct ecall *ecall);
void ecall_set_conf_part(struct ecall *ecall, struct conf_part *cp);
int ecall_update_ssrc(struct ecall *ecall, uint32_t ssrca, uint32_t ssrcv);

void ecall_set_clients(struct ecall* ecall,
		       struct list *clientl);

#define MAX_USER_DATA_SIZE (1024*64) // 64 kByte

typedef void (ecall_user_data_ready_h)(int size, void *arg);
typedef void (ecall_user_data_rcv_h)(uint8_t *data, size_t len, void *arg);
typedef void (ecall_user_data_file_rcv_h)(const char *location, void *arg);
typedef void (ecall_user_data_file_snd_h)(const char *name, bool success, void *arg);

typedef void (ecall_confmsg_h)(struct ecall *ecall,
			       const struct econn_message *msg,
			       void *arg);

int ecall_set_confmsg_handler(struct ecall *ecall,
			      ecall_confmsg_h confmsgh);

int ecall_dce_send(struct ecall *ecall, struct mbuf *mb);

int ecall_dce_sendmsg(struct ecall *ecall, struct econn_message *msg);

int ecall_set_quality_interval(struct ecall *ecall,
			       uint64_t interval);

int ecall_update_mute_state(const struct ecall* ecall);

typedef int (ecall_propsync_h)(struct ecall *ecall,
			       struct econn_message *msg,
			       void *arg);

int ecall_set_propsync_handler(struct ecall *ecall,
			       ecall_propsync_h propsynch);

/* Device pairing */
void ecall_set_devpair(struct ecall *ecall, bool devpair);
int  ecall_devpair_start(struct ecall *ecall);
int  ecall_devpair_answer(struct ecall *ecall,
			  struct econn_message *msg,
			  const char *pairid);
int  ecall_devpair_ack(struct ecall *ecall,
		       struct econn_message *msg,
		       const char *pairid);
int ecall_remove(struct ecall *ecall);


int ecall_add_decoders_for_user(struct ecall *ecall,
				const char *userid,
				const char *clientid,
				const char *userid_hash,
				uint32_t ssrca,
				uint32_t ssrcv);
int ecall_remove_decoders_for_user(struct ecall *ecall,
				   const char *userid,
				   const char *clientid,
				   uint32_t ssrca,
				   uint32_t ssrcv);
int ecall_sync_decoders(struct ecall *ecall);


int ecall_set_keystore(struct ecall *ecall,
		       struct keystore *keystore);


void propsync_get_states(struct econn_props *props,
			 bool *vstate_present, 
			 enum icall_vstate *vstate, 
			 bool *muted_present,
			 bool *muted);

int icall_send_reject_msg(struct icall *icall,
			  const struct list *clientl,
			  const char *userid_local,
			  const char *clientid_local);

int ecall_ping(struct ecall *ecall, bool reponse);

typedef int (ecall_ping_h)(struct ecall *ecall,
			   bool response,
			   void *arg);

int ecall_set_ping_handler(struct ecall *ecall,
			   ecall_ping_h *pingh);

