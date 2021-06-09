struct dns_lookup_entry {
	char *host;
	dns_lookup_h *lookuph;
	void *arg;

	struct le le;
	struct list lookupl;
	pthread_t tid;
	struct sa srv;
	int err;
};


int  dns_platform_init(void *arg);
void dns_platform_close(void);
int  dns_platform_lookup(struct dns_lookup_entry *lent, struct sa *srv);


