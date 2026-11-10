/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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
 * pscall -- publish/subscribe MLS conference call (Cloudflare Realtime)
 *
 * One sendonly ecall publishes our media, one recvonly ecall pulls the
 * other participants' media. Call signalling (CONFSTART, ringing) is
 * shared with ccall through confsig; membership comes from the catalog
 * (hello/welcome over Wire, updates over Cloudflare data channels).
 * Media keys come from the app's MLS epoch (set_media_key).
 */

#define PSCALL_BACKEND             "pscall"
#define PSCALL_DC_NAME             "catalog"

#define PSCALL_CONNECT_TIMEOUT     (   30000)
#define PSCALL_NOONE_JOINED_TIMEOUT (  300000)
#define PSCALL_EVERYONE_LEFT_TIMEOUT (  90000)
#define PSCALL_ROTATE_MLS_TIMEOUT  (    5000)
#define PSCALL_MLS_KEY_AGE         (   60000)
#define PSCALL_REQ_NEW_EPOCH_TIMEOUT (82800000)
#define PSCALL_SECRET_LEN          (      16)

struct pscall;

enum pscall_state {
	PSCALL_STATE_NONE = 0,
	PSCALL_STATE_IDLE,
	PSCALL_STATE_INCOMING,
	PSCALL_STATE_JOINING,
	PSCALL_STATE_ACTIVE,
	PSCALL_STATE_TERMINATING,
};

enum pscall_leg {
	PSCALL_LEG_PUB = 0,
	PSCALL_LEG_SUB = 1,
};

/* Perform a Cloudflare Realtime HTTP request for a leg; complete it with
 * pscall_http_response(). The path is relative to <api>/apps/<appId>;
 * the app adds base URL and authentication. body may be NULL.
 * One request per leg is in flight at a time.
 */
typedef int (pscall_http_h)(struct pscall *psc,
			    enum pscall_leg leg,
			    const char *method,
			    const char *path,
			    const char *body,
			    void *arg);

struct pscall_conf {
	pscall_http_h *httph;
	void *arg;
};

int  pscall_alloc(struct pscall **pscp,
		  const struct ecall_conf *econf,
		  const char *msys_name,
		  const char *convid,
		  const char *userid_self,
		  const char *clientid,
		  const struct pscall_conf *conf);

struct icall *pscall_get_icall(struct pscall *psc);

int  pscall_http_response(struct pscall *psc, enum pscall_leg leg,
			  int status, const char *body);

const char *pscall_state_name(enum pscall_state state);
enum pscall_state pscall_get_state(const struct pscall *psc);
