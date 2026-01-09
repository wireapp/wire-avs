
struct wcall_marshal;
struct calling_instance;

struct str_le {
	struct le le;
	char *str;
};

struct calling_instance *wuser2inst(WUSER_HANDLE wuser);

bool wcall_is_ready(struct calling_instance *inst,
		    int conv_type);

void wcall_invoke_ready(struct calling_instance *inst);

int wcall_marshal_alloc(struct wcall_marshal **wmp); 
struct wcall_marshal *wcall_get_marshal(struct calling_instance *inst);

struct wcall *wcall_lookup(struct calling_instance *inst, const char *convid);
int  wcall_add(struct calling_instance *inst,
	       struct wcall **wcallp, const char *convid, int conv_type);
void wcall_mcat_changed(struct calling_instance *inst,
			enum mediamgr_state state);
void wcall_audio_route_changed(struct calling_instance *inst,
			       enum mediamgr_auplay new_route);

void wcall_invoke_incoming_handler(const char *convid,
				   uint32_t msg_time,
				   const char *userid,
				   const char *clientid,
				   int video_call,
				   int should_ring,
				   int conv_type,
				   void *wuser);

/* Internal API functions */
void wcall_i_recv_msg(struct calling_instance *inst,
		      struct econn_message *msg,
		      uint32_t curr_time,
		      uint32_t msg_time,
		      const char *convid,
		      const char *userid,
		      const char *clientid,
		      int conv_type);
void wcall_i_config_update(struct calling_instance *inst,
			   int err, const char *json_str);
void wcall_i_resp(struct calling_instance *inst,
		  int status, const char *reason, void *arg);
void wcall_i_sft_resp(struct calling_instance *inst,
		      int status,
		      struct econn_message *msg,
		      void *arg);
int  wcall_i_start(struct wcall *wcall,
		   int is_video_call, int group,
		   int audio_cbr);
int wcall_i_answer(struct wcall *wcall,
		   int call_type, int audio_cbr);
int  wcall_i_reject(struct wcall *wcall);
void wcall_i_end(struct wcall *wcall);
void wcall_i_set_video_send_state(struct wcall *wcall, int vstate);
void wcall_i_set_audio_send_cbr(struct wcall *wcall);
void wcall_i_mcat_changed(struct calling_instance *inst,
			  enum mediamgr_state state);
void wcall_i_audio_route_changed(enum mediamgr_auplay new_route);
void wcall_i_network_changed(void);

void wcall_i_invoke_incoming_handler(const char *convid,
				    uint32_t msg_time,
				    const char *userid,
				    const char *clientid,
				    int video_call,
				    int should_ring,
				    int conv_type,
				    void *arg);

int wcall_i_dce_send(struct wcall *wcall, struct mbuf *mb);

void wcall_i_set_media_laddr(struct wcall *wcall,const char *laddr);
void wcall_i_set_clients_for_conv(struct wcall *wcall, const char *json);
void wcall_i_destroy(struct calling_instance *inst);
void wcall_i_set_mute(int muted);
void wcall_i_request_video_streams(struct wcall *wcall,
				   int mode,
				   const char *json);
int wcall_i_set_epoch_info(struct wcall *wcall,
			   uint32_t epochid,
			   const char *clients_json,
			   uint8_t *key_data,
			   uint32_t key_size);

void wcall_i_process_notifications(struct calling_instance *inst,
				   bool processing);

void wcall_marshal_destroy(struct calling_instance *inst);
struct calling_instance *wcall_get_instance(void);
