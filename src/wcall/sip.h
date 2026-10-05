int wcall_i_sip_init(struct calling_instance *inst, const char *conf_path);
int wcall_i_sip_close(struct calling_instance *inst);
int wcall_i_sip_create(struct calling_instance *inst,
		       const char *aor,
		       wcall_sip_ready_h *readyh,	       
		       wcall_sip_incoming_h *incomingh,
		       wcall_sip_close_h *closeh,
		       wcall_sip_err_h *errh,
		       void *arg);
int  wcall_i_sip_destroy(struct calling_instance *inst,
			 const char *aor);
int  wcall_i_sip_answer(struct calling_instance *inst,
			struct wsip_call *wsip, const char *convid);
void wcall_i_sip_hangup(struct calling_instance *inst,
			struct wsip_call *wsip, int code, const char *status);
