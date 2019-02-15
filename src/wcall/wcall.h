
struct wcall_marshal;
int wcall_marshal_alloc(struct wcall_marshal **wmp); 
struct wcall_marshal *wcall_get_marshal(void *wuser);

struct wcall *wcall_lookup(void *id, const char *convid);
int  wcall_add(void *id, struct wcall **wcallp, const char *convid, int conv_type);
void wcall_mcat_changed(void *id, enum mediamgr_state state);
void wcall_audio_route_changed(void *wuser, enum mediamgr_auplay new_route);

void wcall_invoke_incoming_handler(const char *convid,
			           uint32_t msg_time,
			           const char *userid,
			           int video_call,
			           int should_ring,
			           void *wuser);

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
		   int audio_cbr, void *extcodec_arg);
int wcall_i_answer(struct wcall *wcall, int call_type, int audio_cbr, void *extcodec_arg);
int  wcall_i_reject(struct wcall *wcall);
void wcall_i_end(struct wcall *wcall);
void wcall_i_set_video_send_state(struct wcall *wcall, int vstate);
void wcall_i_set_audio_send_cbr(struct wcall *wcall);
void wcall_i_mcat_changed(void *id, enum mediamgr_state state);
void wcall_i_audio_route_changed(enum mediamgr_auplay new_route);
void wcall_i_network_changed(void);

void wcall_i_invoke_incoming_handler(const char *convid,
				    uint32_t msg_time,
				    const char *userid,
				    int video_call,
				    int should_ring,
				    void *arg);

void wcall_i_destroy(void *id);
void wcall_marshal_destroy(void *id);


