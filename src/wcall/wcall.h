
int wcall_marshal_init(void);
void wcall_marshal_close(void);

struct wcall *wcall_lookup(void *id, const char *convid);
int  wcall_add(void *id, struct wcall **wcallp, const char *convid, bool group);
void wcall_mcat_changed(void *id, enum mediamgr_state state);
void wcall_audio_route_changed(enum mediamgr_auplay new_route);

/* Internal API functions */
void wcall_i_recv_msg(void *id,
		      struct econn_message *msg,
		      uint32_t curr_time,
		      uint32_t msg_time,
		      const char *convid,
		      const char *userid,
		      const char *clientid);
void wcall_i_config_update(void *id, int err, const char *json_str);
void wcall_i_resp(void *id,
		  int status, const char *reason, void *arg);
int  wcall_i_start(struct wcall *wcall, int is_video_call, int group,
		   int audio_cbr);
int  wcall_i_answer(struct wcall *wcall, int audio_cbr);
int  wcall_i_reject(struct wcall *wcall);
void wcall_i_end(struct wcall *wcall);
void wcall_i_set_video_send_active(struct wcall *wcall, bool active);
void wcall_i_set_audio_send_cbr(struct wcall *wcall);
void wcall_i_mcat_changed(void *id, enum mediamgr_state state);
void wcall_i_audio_route_changed(enum mediamgr_auplay new_route);
void wcall_i_network_changed(void);




