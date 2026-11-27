#ifdef __cplusplus
extern "C" {
#endif


struct peerflow;
struct avs_vidframe;

int peerflow_set_funcs(void);

void peerflow_set_adm(void *adm);
int peerflow_init(void);

typedef void (peerflow_acbr_h)(bool enabled, bool offer, void *arg);
typedef void (peerflow_norelay_h)(bool local, void *arg);
typedef void (peerflow_tool_h)(const char *tool, void *arg);

void peerflow_start_log(void);

/* See wcall_set_stats_log */
void peerflow_set_stats_log(bool enable);

int peerflow_alloc(struct iflow		**flowp,
		   const char		*convid,
		   const char		*userid_self,
		   const char		*clientid_self,
		   enum icall_conv_type	conv_type,
		   enum icall_call_type	call_type,
		   enum icall_vstate	vstate,
		   const char           *rec_path,
		   const char           *play_path,
		   void			*extarg);

void capture_source_handle_frame(struct avs_vidframe *frame);

/*
 * Test hooks, used by tools (e.g. sftloader) to verify video routing
 * end to end. When no handlers are set the media path is unchanged.
 */

/* Called for every outgoing frame, once per local user/client sending
 * video. The frame buffer is a private copy and may be modified.
 * userid/clientid may be NULL for sinks without a peerflow.
 */
typedef void (capture_stamp_h)(struct avs_vidframe *frame,
			       const char *userid_self,
			       const char *clientid_self,
			       void *arg);

void capture_source_set_stamp_handler(capture_stamp_h *stamph, void *arg);
bool capture_source_stamp_enabled(void);

/* Called for every decoded remote video frame, with both the receiving
 * (local) and the sending (remote) user/client.
 */
typedef void (peerflow_render_tap_h)(const struct avs_vidframe *frame,
				     const char *userid_self,
				     const char *clientid_self,
				     const char *userid_remote,
				     const char *clientid_remote,
				     void *arg);

void peerflow_set_render_tap(peerflow_render_tap_h *taph, void *arg);

/* In conference calls the flows only know the hashed self ids. The call
 * layer registers the real ids for them so the test hooks above can report
 * real ids. No-op when no test hook is set.
 */
void peerflow_test_register_self(const char *userid_hash,
				 const char *clientid_hash,
				 const char *userid_real,
				 const char *clientid_real);
/* Resolve hashed self ids into the real ones (or copy them if unknown) */
void peerflow_test_resolve_self(const char *userid, const char *clientid,
				char *userid_out, size_t userid_sz,
				char *clientid_out, size_t clientid_sz);
void peerflow_render_tap(struct peerflow *pf,
			 const struct avs_vidframe *frame,
			 const char *userid_remote,
			 const char *clientid_remote);

int peerflow_get_userid_for_ssrc(struct peerflow* pf,
				 uint32_t csrc,
				 bool video,
				 char **userid_real,
				 char **clientid_real,
				 char **userid_hash);

int peerflow_inc_frame_count(struct peerflow* pf,
			     uint32_t csrc,
			     bool video,
			     uint32_t frames);

#ifdef __cplusplus
}
#endif
	
