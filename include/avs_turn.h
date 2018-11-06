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


/*
 * TURN Connection
 */

struct turn_conn;

typedef void (turnconn_estab_h)(struct turn_conn *conn,
				const struct sa *relay_addr,
				const struct sa *mapped_addr,
				const struct stun_msg *msg, void *arg);
typedef void (turnconn_data_h)(struct turn_conn *conn, const struct sa *src,
			       struct mbuf *mb, void *arg);
typedef void (turnconn_error_h)(int err, void *arg);

/* Defines one TURN-connection via UDP/TCP to one TURN-Server */
struct turn_conn {
	struct le le;

	struct turnc *turnc;
	struct tcp_conn *tc;
	struct sa turn_srv;
	struct tls_conn *tlsc;
	struct tls *tls;
	struct mbuf *mb;
	struct udp_helper *uh_app;  /* for outgoing UDP->TCP redirect */
	struct udp_sock *us_app;    // todo: remove?
	struct udp_sock *us_turn;
	struct stun_keepalive *ska;
	struct tmr tmr_delay;
	char *username;
	char *password;
	int af;
	int proto;
	bool secure;
	bool turn_allocated;
	bool failed;
	int layer_stun;
	int layer_turn;
	uint32_t delay;
	turnconn_estab_h *estabh;
	turnconn_data_h *datah;
	turnconn_error_h *errorh;
	void *arg;

	uint64_t ts_turn_resp;
	uint64_t ts_turn_req;

	unsigned n_permh;

	struct sa relay_addr;
	struct sa mapped_addr;
};


int turnconn_alloc(struct turn_conn **connp, struct list *connl,
		   const struct sa *turn_srv, int proto, bool secure,
		   const char *username, const char *password,
		   int af, struct udp_sock *sock,
		   int layer_stun, int layer_turn,
		   turnconn_estab_h *estabh, turnconn_data_h *datah,
		   turnconn_error_h *errorh, void *arg
		   );
int turnconn_add_permission(struct turn_conn *conn, const struct sa *peer);
int turnconn_add_channel(struct turn_conn *conn, const struct sa *peer);
struct turn_conn *turnconn_find_allocated(const struct list *turnconnl,
					  int proto);
const char *turnconn_proto_name(const struct turn_conn *conn);
bool turnconn_is_one_allocated(const struct list *turnconnl);
bool turnconn_are_all_allocated(const struct list *turnconnl);
bool turnconn_are_all_failed(const struct list *turnconnl);
int turnconn_debug(struct re_printf *pf, const struct turn_conn *conn);


/*
 * STUN uri
 */

enum stun_scheme {
	STUN_SCHEME_STUN,
	STUN_SCHEME_TURN,
};


struct stun_uri {
	enum stun_scheme scheme;
	struct sa addr;	
	char host[512];
	int port;
	int proto;
	bool secure;
};


int stun_uri_encode(struct re_printf *pf, const struct stun_uri *uri);
int stun_uri_decode(struct stun_uri *stun_uri, const char *str);
