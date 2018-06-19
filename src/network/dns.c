
#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <memory.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <re.h>
#include "avs_network.h"

#ifdef __APPLE__
#       include <TargetConditionals.h>
#endif


int dns_get_servers(char *domain, size_t dsize, struct sa *nsv, uint32_t *n)
{
#if TARGET_OS_IPHONE
	struct __res_state state;
	uint32_t i, cnt;
	int ret, err=0;
	union res_9_sockaddr_union *addrs;
	int k;

	memset(&state, 0, sizeof(state));
	ret = res_ninit(&state);
	if (0 != ret)
		return ENOENT;

	if (!state.nscount) {
		err = ENOENT;
		goto out;
	}

	cnt = min(*n, (uint32_t)state.nscount);

	addrs = mem_zalloc(cnt * sizeof(*addrs), NULL);
	if (!addrs) {
		err = ENOMEM;
		goto out;
	}

	cnt = res_getservers(&state, addrs, cnt);
	k = 0;
	for (i=0; i<cnt; i++) {
		switch (addrs[i].sin.sin_family) {
		case AF_INET:
			sa_set_in(&nsv[k],
				  addrs[i].sin.sin_addr.s_addr,
				  addrs[i].sin.sin_port);
			break;

		case AF_INET6:
			sa_set_in6(&nsv[k],
				   addrs[i].sin6.sin6_addr.s6_addr,
				   addrs[i].sin6.sin6_port);
			break;

		default:
			break;
		}		
		if (!sa_port(&nsv[k]))
			sa_set_port(&nsv[k], DNS_PORT);
		++k;
		    
	}
	mem_deref(addrs);

	*n = k;
 out:
	res_nclose(&state);

	return err;
#else
	return ENOSYS;
#endif
	
}
