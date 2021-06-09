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
 * forward declarations
 */

struct rest_cli;

struct store;        /* see avs_store.h */
struct json_object;  /* see, er, elsewhere  */

typedef void (rest_resp_h)(int err, const struct http_msg *msg,
			   struct mbuf *mb, struct json_object *jobj,
			   void *arg);


/*
 * HTTP chunked encoding
 */

struct chunk_decoder;

int  chunk_decoder_alloc(struct chunk_decoder **decp);
int  chunk_decoder_append_data(struct chunk_decoder *dec,
			       const uint8_t *data, size_t len);
bool chunk_decoder_is_final(const struct chunk_decoder *dec);
int  chunk_decoder_unchunk(struct chunk_decoder *dec, struct mbuf *mb);
size_t chunk_decoder_count_chunks(const struct chunk_decoder *dec);
size_t chunk_decoder_length(const struct chunk_decoder *dec);


int chunk_encode(struct mbuf *mb, const uint8_t *p, size_t len);
int chunk_decode(uint8_t **bufp, size_t *lenp, struct mbuf *mb_in);


/*
 * Cookie jar
 */

struct cookie_jar;

int cookie_jar_alloc(struct cookie_jar **jarp, struct store *store);
int cookie_jar_print_to_request(struct cookie_jar *jar, struct re_printf *pf,
				const char *uri);
int cookie_jar_handle_response(struct cookie_jar *jar, const char *uri,
			       const struct http_msg *msg);
const struct list *cookie_jar_list(const struct cookie_jar *jar);


/*
 * Login
 */


struct login_token {
	uint32_t expires_in; /* seconds */
	char access_token[256];
	char token_type[32];
};

typedef void (login_h)(int err, const struct login_token *token, void *arg);

struct login;

int login_request(struct login **loginp, struct rest_cli *rest_cli,
		  const char *server_uri,
		  const char *email, const char *password,
		  login_h *loginh, void *arg);
struct login_token *login_get_token(const struct login *login);


/*
 * REST Request
 */

struct rest_cli;
struct rest_req;


int  rest_client_alloc(struct rest_cli **restp, struct http_cli *http,
		       const char *server_uri, struct store *store,
		       int maxopen, const char *user_agent);
void rest_client_set_token(struct rest_cli *rest,
			   const struct login_token *token);
int  rest_client_debug(struct re_printf *pf, const struct rest_cli *cli);

int rest_req_alloc(struct rest_req **rrp,
		   rest_resp_h *resph, void *arg, const char *method,
		   const char *path, ...);
int rest_req_valloc(struct rest_req **rrp,
		    rest_resp_h *resph, void *arg, const char *method,
		    const char *path, va_list ap);
int rest_req_set_raw(struct rest_req *rr, bool raw);
int rest_req_add_header(struct rest_req *rr, const char *fmt, ...);
int rest_req_add_header_v(struct rest_req *rr, const char *fmt, va_list ap);
int rest_req_add_body(struct rest_req *rr, const char *ctype,
		      const char *fmt, ...);
int rest_req_add_body_v(struct rest_req *rr, const char *ctype,
		        const char *fmt, va_list ap);
int rest_req_add_body_raw(struct rest_req *rr, const char *ctype,
			  uint8_t *data, size_t len);
int rest_req_add_json(struct rest_req *rr, const char *format, ...);
int rest_req_add_json_v(struct rest_req *rr, const char *format, va_list ap);
int rest_req_start(struct rest_req **rrp, struct rest_req *rr,
		   struct rest_cli *rest_cli, int prio);

int rest_get(struct rest_req **rrp, struct rest_cli *rest_cli, int prio,
	     rest_resp_h *resph, void *arg, const char *path, ...);
int rest_request(struct rest_req **rrp, struct rest_cli *rest_cli, int prio,
		 const char *method, rest_resp_h *resph, void *arg,
		 const char *path, const char *body, ...);
int rest_request_json(struct rest_req **rrp, struct rest_cli *rest_cli,
		      int prio, const char *method,
		      rest_resp_h *resph, void *arg,
		      const char *path, uint32_t objc, ...);
int rest_request_jobj(struct rest_req **rrp, struct rest_cli *rest_cli,
		      int prio, const char *method,
		      rest_resp_h *resph, void *arg,
		      struct json_object *jobj, const char *path, ...);

/* Utility functions
 */

int rest_urlencode(struct re_printf *pf, const char *s);

int rest_err(int err, const struct http_msg *msg);
