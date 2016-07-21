

/* RFC 4571 */


enum { SHIM_HDR_SIZE = 2 };

struct shim;

typedef bool (shim_frame_h)(struct mbuf *mb, void *arg);


int shim_insert(struct shim **shimp, struct tcp_conn *tc, int layer,
		shim_frame_h *frameh, void *arg);
int shim_debug(struct re_printf *pf, const struct shim *shim);
