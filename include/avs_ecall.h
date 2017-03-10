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


typedef int  (ecall_transp_send_h)(const char *userid_sender,
				   const char *msg, void *arg);


/**
 * Incoming call, may be called multiple times
 */
typedef void (ecall_conn_h)(const char *userid_sender,
			    bool video_call, void *arg);


/** Call has been aswered
 */
typedef void (ecall_answer_h)(void *arg);


/**
 * Missed call, may be called multiple times
 */
typedef void (ecall_missed_h)(uint32_t msg_time, const char *userid_sender,
			      bool video_call, void *arg);


/**
 * Want to start media, called once.
 */
typedef void (ecall_media_start_h)(void *arg);

/**
 * Call/media established, called after every UPDATE.
 */
typedef void (ecall_media_estab_h)(void *arg, bool update);

/**
 * Call/audio established, called after every UPDATE.
 */
typedef void (ecall_audio_estab_h)(void *arg, bool update);

/**
 * The Data-Channel was established, called after every UPDATE.
 */
typedef void (ecall_datachan_estab_h)(void *arg, bool update);

/**
 * We received a PROPSYNC from remote side, called multiple times.
 */
typedef void (ecall_propsync_h)(void *arg);


/**
 * The call was terminated, called once.
 */
typedef void (ecall_close_h)(int err, const char *metrics_json, void *arg);

struct ecall_conf {
	struct econn_conf econf;
	enum mediaflow_nat nat;
	int trace;
};


int  ecall_alloc(struct ecall **ecallp, struct list *ecalls,
		 const struct ecall_conf *conf,
		 struct msystem *msys,
		 const char *convid, const char *userid_self,
		 const char *clientid,
		 ecall_conn_h *connh,
		 ecall_answer_h *answerh,
		 ecall_missed_h *missedh,
		 ecall_media_estab_h *media_estabh,
		 ecall_audio_estab_h *audio_estabh,
		 ecall_datachan_estab_h *datachan_estabh,
		 ecall_propsync_h *propsynch,
		 ecall_close_h *closeh,
		 ecall_transp_send_h *sendh, void *arg);
int  ecall_set_turnserver(struct ecall *ecall, const struct sa *srv,
			  const char *user, const char *pass);
int  ecall_start(struct ecall *ecall);
int  ecall_answer(struct ecall *ecall);
void ecall_transp_recv(struct ecall *ecall,
		       uint32_t curr_time, /* in seconds */
		       uint32_t msg_time, /* in seconds */
		       const char *userid_sender,
		       const char *clientid_sender,
		       const char *str);
void ecall_msg_recv(struct ecall *ecall,
		    uint32_t curr_time, /* in seconds */
		    uint32_t msg_time, /* in seconds */
		    const char *userid_sender,
		    const char *clientid_sender,
		    struct econn_message *msg);
void ecall_end(struct ecall *ecall);
struct ecall *ecall_find_convid(const struct list *ecalls,
				const char *convid);
int ecall_debug(struct re_printf *pf, const struct ecall *ecall);
struct mediaflow *ecall_mediaflow(const struct ecall *ecall);
struct econn *ecall_get_econn(const struct ecall *ecall);
enum econn_state ecall_state(const struct ecall *ecall);
int ecall_media_start(struct ecall *ecall);
void ecall_media_stop(struct ecall *ecall);
int ecall_set_video_send_active(struct ecall *ecall, bool active);
bool ecall_is_answered(const struct ecall *ecall);
bool ecall_has_video(const struct ecall *ecall);
int ecall_propsync_request(struct ecall *ecall);
const char *ecall_props_get_local(struct ecall *ecall, const char *key);
const char *ecall_props_get_remote(struct ecall *ecall, const char *key);
void ecall_trace(const struct ecall *ecall, bool tx, const char *fmt, ...);
int  ecall_restart(struct ecall *ecall);

/* Marshalled functions */
struct ecall_marshal;

int  ecall_marshal_alloc(struct ecall_marshal **emp);
int  marshal_ecall_start(struct ecall_marshal *em, struct ecall *ecall);
int  marshal_ecall_answer(struct ecall_marshal *em, struct ecall *ecall);
int  marshal_ecall_transp_recv(struct ecall_marshal *em,
			       struct ecall *ecall,
			       uint64_t curr_time,
			       uint64_t msg_time,
			       const char *convid,
			       const char *userid,
			       const char *clientid,
			       const char *str);
int  marshal_ecall_msg_recv(struct ecall_marshal *em,
			    struct ecall *ecall,
			    uint64_t curr_time,
			    uint64_t msg_time,
			    const char *convid,
			    const char *userid,
			    const char *clientid,
			    struct econn_message *msg);
int marshal_ecall_end(struct ecall_marshal *em, struct ecall *ecall);

int  marshal_ecall_set_video_send_active(struct ecall_marshal *em,
					 struct ecall *ecall,
					 bool active);
int marshal_ecall_media_start(struct ecall_marshal *em, struct ecall *ecall);
int marshal_ecall_media_stop(struct ecall_marshal *em, struct ecall *ecall);



