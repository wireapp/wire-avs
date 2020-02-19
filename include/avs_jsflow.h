#ifdef __cplusplus
extern "C" {
#endif


struct jsflow;
	
typedef void (jsflow_acbr_h)(bool enabled, bool offer, void *arg);
void jsflow_start_log(void);

int jsflow_alloc(struct iflow		**flowp,
		 const char		*convid,
		 enum icall_conv_type	conv_type,
		 enum icall_call_type	call_type,
		 enum icall_vstate	vstate,
		 void			*extarg);

#ifdef __cplusplus
}
#endif
	
