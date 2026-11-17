
#define WU_MAGIC 0x57550000 /* WU */

struct wcall_marshal;
struct calling_instance;

struct str_le {
	struct le le;
	char *str;
};

struct duration_entry {
        char *convid;
        int duration;
        struct le le;
};

struct calling_instance *wcall_inst_get(WUSER_HANDLE wuser);
struct calling_instance *wcall_inst_ref(struct calling_instance *inst);
void *wcall_inst_put(struct calling_instance *inst);

bool wcall_is_ready(struct calling_instance *inst,
		    int conv_type);

void wcall_invoke_ready(struct calling_instance *inst);

int wcall_marshal_alloc(struct wcall_marshal **wmp); 
void wcall_marshal_free(struct wcall_marshal *wm);
void wcall_marshal_flush_pending(struct calling_instance *inst);
struct wcall_marshal *wcall_get_marshal(struct calling_instance *inst);

struct wcall *wcall_lookup(struct calling_instance *inst, const char *convid);
int  wcall_add(struct calling_instance *inst,
	       struct wcall **wcallp, const char *convid,
	       int conv_type, bool meeting);
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
		      int conv_type,
		      bool meeting);
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
		   int audio_cbr,
		   bool meeting);
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

void wcall_i_set_duration(struct wcall *wcall, int duration);
void wcall_i_mute_changed(struct calling_instance *inst, bool muted);
void wcall_i_activate(struct calling_instance *inst, bool active);

enum wcall_setter {
	WCALL_SET_MEDIA_ESTAB_H,
	WCALL_SET_REQ_NEW_EPOCH_H,
	WCALL_SET_MEDIA_STOPPED_H,
	WCALL_SET_DATA_CHAN_ESTAB_H,
	WCALL_SET_STATE_H,
	WCALL_SET_GROUP_CHANGED_H,
	WCALL_SET_PARTICIPANT_CHANGED_H,
	WCALL_SET_REQ_CLIENTS_H,
	WCALL_SET_ACTIVE_SPEAKER_H,
	WCALL_SET_MUTE_H,
	WCALL_SET_NETWORK_QUALITY_H,
	WCALL_SET_TRACE,
	WCALL_SET_BACKGROUND,
	WCALL_SET_PRIVACY,
};

/* A public setter call, applied to the instance on the wcall thread */
struct wcall_set {
	enum wcall_setter what;
	void (*h)(void); /* cast back to the handler type */
	void *arg;
	int val;
};

void wcall_i_set(struct calling_instance *inst, const struct wcall_set *set);
int  wcall_marshal_set(struct calling_instance *inst,
		       const struct wcall_set *set);

#define WCALL_DESTROY_WAIT_MS 5000

/* Waits up to wait_ms for the DESTROY to be handled, 0 to not wait */
int  wcall_marshal_destroy(struct calling_instance *inst, int wait_ms);
bool wcall_inst_closing(struct calling_instance *inst);
int  wcall_marshal_mute_changed(struct calling_instance *inst, bool muted);
int  wcall_marshal_activate(struct calling_instance *inst, bool active);
void wcall_thread_enter(void);
void wcall_thread_leave(void);
bool wcall_thread_foreign(void);
bool wcall_thread_current(void);
int wcall_duration_add(struct calling_instance *inst,
		       const char *convid,
		       int duration);
struct duration_entry *wcall_duration_lookup(struct calling_instance *inst,
					     const char *convid);
WUSER_HANDLE wcall_create_wuser(uint32_t *idx);
/* Returns a referenced instance, caller must mem_deref it */
struct calling_instance *wcall_get_instance(void);
