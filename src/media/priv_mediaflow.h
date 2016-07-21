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
	LAYER_DTLS_TRANSPORT = 15,  /* below DTLS */
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
 * DTLS
 */

int dtls_print_sha1_fingerprint(struct re_printf *pf, const struct tls *tls);
int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls);


/*
 * Packet
 */

enum packet {
	PACKET_UNKNOWN = 0,
	PACKET_RTP,
	PACKET_RTCP,
	PACKET_DTLS,
	PACKET_STUN,
};

bool packet_is_rtp_or_rtcp(const struct mbuf *mb);
bool packet_is_rtcp_packet(const struct mbuf *mb);
bool packet_is_dtls_packet(const struct mbuf *mb);
enum packet packet_classify_packet_type(const struct mbuf *mb);
const char *packet_classify_name(enum packet pkt);
