/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/


#ifdef __cplusplus
extern "C" {
#endif


struct msgctx {
	struct stun_unknown_attr ua;
	uint8_t *key;
	uint32_t keylen;
	bool fp;
	struct turnd *turnd;
};

struct trafstat {
	uint64_t pktc_tx;
	uint64_t pktc_rx;
	uint64_t bytc_tx;
	uint64_t bytc_rx;
};


typedef void (turn_recv_h)(bool secure, void *arg);

struct turnd {
	struct sa rel_addr;
	struct sa rel_addr6;
	struct hash *ht_alloc;
	uint32_t lifetime_max;

	/* TCP-transport */
	struct list lstnrl;
	struct list tcl;
	turn_recv_h *recvh;
	void *arg;

	uint16_t sim_error;
};

struct chanlist;

struct allocation {
	struct le he;
	struct tmr tmr;
	uint8_t tid[STUN_TID_SIZE];
	struct sa cli_addr;
	struct sa srv_addr;
	struct sa rel_addr;
	struct sa rsv_addr;
	void *cli_sock;
	struct udp_sock *rel_us;
	struct udp_sock *rsv_us;
	char *username;
	struct hash *perms;
	struct chanlist *chans;
	uint64_t dropc_tx;
	uint64_t dropc_rx;
	int proto;
};

extern const char *turn_software;

void allocate_request(struct turnd *turnd, struct allocation *alx,
		      struct msgctx *ctx, int proto, void *sock,
		      const struct sa *src, const struct sa *dst,
		      const struct stun_msg *msg);
void refresh_request(struct turnd *turnd, struct allocation *al,
		     struct msgctx *ctx,
		     int proto, void *sock, const struct sa *src,
		     const struct stun_msg *msg);
void createperm_request(struct allocation *al, struct msgctx *ctx,
			int proto, void *sock, const struct sa *src,
			const struct stun_msg *msg);
void chanbind_request(struct allocation *al, struct msgctx *ctx,
		      int proto, void *sock, const struct sa *src,
		      const struct stun_msg *msg);


struct perm;

struct perm *perm_find(const struct hash *ht, const struct sa *addr);
struct perm *perm_create(struct hash *ht, const struct sa *peer,
			 const struct allocation *al);
void perm_refresh(struct perm *perm);
void perm_tx_stat(struct perm *perm, size_t bytc);
void perm_rx_stat(struct perm *perm, size_t bytc);
int  perm_hash_alloc(struct hash **ht, uint32_t bsize);
void perm_status(struct hash *ht, struct mbuf *mb);


struct chan;

struct chan *chan_numb_find(const struct chanlist *cl, uint16_t numb);
struct chan *chan_peer_find(const struct chanlist *cl, const struct sa *peer);
uint16_t chan_numb(const struct chan *chan);
const struct sa *chan_peer(const struct chan *chan);
int  chanlist_alloc(struct chanlist **clp, uint32_t bsize);
void chan_status(const struct chanlist *cl, struct mbuf *mb);


bool turn_request_handler(struct msgctx *ctx, int proto, void *sock,
			  const struct sa *src, const struct sa *dst,
			  const struct stun_msg *msg);
bool turn_indication_handler(struct msgctx *ctx, int proto,
			     void *sock, const struct sa *src,
			     const struct sa *dst,
			     const struct stun_msg *msg);
bool turn_raw_handler(struct turnd *turnd, int proto, const struct sa *src,
		      const struct sa *dst, struct mbuf *mb);


/* stun */

void process_msg(struct turnd *turnd, int proto, void *sock,
		 const struct sa *src, const struct sa *dst,
		 struct mbuf *mb);


/* tcp */

int  restund_tcp_init(struct turnd *turnd, const char *cert);
void restund_tcp_close(struct turnd *turnd);
struct sa *restund_tcp_laddr(struct turnd *turnd, bool secure);


#ifdef __cplusplus
}
#endif
