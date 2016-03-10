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


/* Protocol stack layers (order is important) */
enum {
	/*LAYER_RTP  =  40,*/
	LAYER_DTLS =  20,       /* must be above zero */
	LAYER_SRTP =  10,       /* must be below RTP */
	LAYER_ICE  = -10,
	LAYER_TURN = -20,       /* must be below ICE */
	LAYER_STUN = -30,       /* must be below TURN */
};


extern const char *avs_software;


/*
 * ICE-lite
 */

typedef void (ice_estab_h)(struct ice_cand_attr *rcand, void *arg);
typedef void (ice_close_h)(int err, void *arg);

struct ice_lite;

struct lite_cand {
	struct le le;
	struct ice_cand_attr cand;
};

int icelite_alloc(struct ice_lite **icep, struct udp_sock *us,
		  const char *ufrag, const char *pwd,
		  ice_estab_h *estabh, ice_close_h *closeh, void *arg);
int icelite_cand_add(struct ice_lite *ice, const struct ice_cand_attr *cand);
struct ice_cand_attr *icelite_cand_find(const struct ice_lite *ice,
					const struct sa *addr);
struct list *icelite_rcandl(struct ice_lite *ice);
int icelite_debug(struct re_printf *pf, const struct ice_lite *ice);


/*
 * Consent Freshness
 */

typedef void (consent_close_h)(int err, void *arg);

struct consent;

int consent_start(struct consent **conp, struct stun *stun,
		  struct udp_sock *us, bool controlling,
		  const char *lufrag,
		  const char *rufrag, const char *rpwd,
		  size_t presz, const struct sa *raddr,
		  consent_close_h *closeh, void *arg);


/*
 * TURN Connection
 */

struct turn_conn;

typedef void (turnconn_estab_h)(struct turn_conn *conn,
				const struct sa *relay_addr,
				const struct sa *mapped_addr, void *arg);
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
	char *username;
	char *password;
	int proto;
	bool secure;
	bool turn_allocated;
	turnconn_estab_h *estabh;
	turnconn_error_h *errorh;
	void *arg;

	uint64_t ts_turn_resp;
	uint64_t ts_turn_req;
};


int turnconn_alloc(struct turn_conn **connp, struct list *connl,
		   const struct sa *turn_srv, int proto, bool secure,
		   const char *username, const char *password,
		   struct udp_sock *sock,
		   turnconn_estab_h *estabh,
		   turnconn_error_h *errorh, void *arg
		   );
int turnconn_add_permission(struct turn_conn *conn, const struct sa *peer);
struct turn_conn *turnconn_find_allocated(const struct list *turnconnl,
					  int proto);
const char *turnconn_proto_name(const struct turn_conn *conn);
int turnconn_debug(struct re_printf *pf, const struct turn_conn *conn);


/*
 * DTLS
 */

int dtls_print_sha1_fingerprint(struct re_printf *pf, const struct tls *tls);
int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls);
