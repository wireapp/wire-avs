#ifdef __cplusplus
extern "C" {
#endif


struct peerflow;
struct avs_vidframe;

int peerflow_set_funcs(void);

int peerflow_init(void);

typedef void (peerflow_acbr_h)(bool enabled, bool offer, void *arg);
typedef void (peerflow_norelay_h)(bool local, void *arg);

void peerflow_start_log(void);

int peerflow_alloc(struct iflow		**flowp,
		   const char		*convid,
		   const char		*userid_self,
		   const char		*clientid_self,
		   enum icall_conv_type	conv_type,
		   enum icall_call_type	call_type,
		   enum icall_vstate	vstate,
		   void			*extarg);

void capture_source_handle_frame(struct avs_vidframe *frame);

#ifdef __cplusplus
}
#endif
	
