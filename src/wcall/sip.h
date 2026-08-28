struct sip_instance {
	struct list ual;
};

struct sip_instance *wcall_get_sip_instance(struct calling_instance *inst);

int wcall_i_sip_init(struct calling_instance *inst, const char *conf_path);
int wcall_i_sip_close(struct calling_instance *inst);
int wcall_i_sip_create(struct calling_instance *inst, const char *aor);
int wcall_i_sip_destroy(struct calling_instance *inst, const char *aor);
