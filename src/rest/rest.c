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
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <re.h>
#include "avs_version.h"
#include "avs_jzon.h"
#include "avs_log.h"
#include "avs_store.h"
#include "avs_rest.h"


#define REST_MAGIC 0x0e5100a3


struct rest_cli {
	struct http_cli *http_cli;
	char *server_uri;
	struct login_token login_token;
	struct cookie_jar *jar;
	struct list openl;
	size_t maxopen;
	char *user_agent;
	struct list reql;
	bool shutdown;
};

struct rest_req {
	uint32_t magic;
	struct le le;
	struct rest_cli *rest_cli;
	struct rest_req **reqp;
	struct http_req *http_req;
	struct http_msg *msg;
	struct mbuf *mb_body;  /* size contains the expected content-length */
	char *method;
	char *path;
	char *uri;
	char *header;
	char *ctype;
	struct mbuf *req_body;
	bool json;
	bool raw;
	int prio;
	rest_resp_h *resph;
	void *arg;

	uint64_t ts_req;
	uint64_t ts_resp;
};


static void req_close(struct rest_req *req, int err,
		      const struct http_msg *msg, struct mbuf *mb,
		      struct json_object *jobj);


static void flush_requests(struct list *lst)
{
	struct le *le;

	if (list_count(lst) > 0) {
		info("rest: flushing pending requests (%d)\n",
		     list_count(lst));
	}

	le = list_head(lst);
	while (le) {
		struct rest_req *req = le->data;
		le = le->next;

		req_close(req, ECONNABORTED, NULL, NULL, NULL);
	}
}


/*** rest_client_alloc
 */

static void cli_destructor(void *arg)
{
	struct rest_cli *rest = arg;

	rest->shutdown = true;

	flush_requests(&rest->openl);
	flush_requests(&rest->reql);

	mem_deref(rest->jar);
	mem_deref(rest->http_cli);
	mem_deref(rest->server_uri);
	mem_deref(rest->user_agent);
}


int rest_client_alloc(struct rest_cli **restp, struct http_cli *http_cli,
		      const char *server_uri, struct store *store,
		      int maxopen, const char *user_agent)
{
	struct rest_cli *rest;
	int err;

	if (!restp || !http_cli || !server_uri || maxopen < 1)
		return EINVAL;

	rest = mem_zalloc(sizeof(*rest), cli_destructor);
	if (!rest)
		return ENOMEM;

	err = str_dup(&rest->server_uri, server_uri);
	if (err) {
		goto out;
	}
	rest->http_cli = mem_ref(http_cli);

	err = cookie_jar_alloc(&rest->jar, store);
	if (err) {
		warning("Cookie jar init failed: %m.\n", err);
		goto out;
	}

	rest->maxopen = maxopen;

	if (user_agent)
		err = str_dup(&rest->user_agent, user_agent);
	else
		err = str_dup(&rest->user_agent, avs_version_str());
	if (err)
		goto out;

	*restp = rest;

 out:
	if (err)
		mem_deref(rest);
	return err;
}


static void wake_request(struct rest_req *req);

static void trigger_queue(struct rest_cli *cli)
{
	struct le *le;
	struct rest_req *rq = NULL;

	debug("trigger_queue: reql %i, openl %i.\n",
	      (int) list_count(&cli->reql), (int) list_count(&cli->openl));

	if (list_isempty(&cli->reql)
	    || list_count(&cli->openl) >= cli->maxopen)
	{
		return;
	}

	LIST_FOREACH(&cli->reql, le) {
		struct rest_req *rrq = le->data;

		if (!rq || rrq->prio < rq->prio)
			rq = rrq;
	}
	list_unlink(&rq->le);

	wake_request(rq);
}	


static void req_destructor(void *arg)
{
	struct rest_req *req = arg;

	list_unlink(&req->le);
	mem_deref(req->http_req);
	mem_deref(req->method);
	mem_deref(req->path);
	mem_deref(req->uri);
	mem_deref(req->header);
	mem_deref(req->ctype);
	mem_deref(req->req_body);
	mem_deref(req->msg);
	mem_deref(req->mb_body);
}


static void req_close(struct rest_req *req, int err,
		      const struct http_msg *msg, struct mbuf *mb,
		      struct json_object *jobj)
{
	struct rest_cli *cli = req->rest_cli;

	debug("rest: [%s %s] request closed\n", req->method, req->path);

	req->http_req  = mem_deref(req->http_req);

	if (req->reqp) {
		*req->reqp = NULL;
		req->reqp = NULL;
	}

	if (req->resph) {
		req->resph(err, msg, mb, jobj, req->arg);
		req->resph = NULL;
	}
	mem_deref(req);

	if (!cli->shutdown)
		trigger_queue(cli);
}


static void response(struct rest_req *req, const struct http_msg *msg,
		     struct mbuf *mb)
{
	struct json_object *jobj = NULL;
	size_t len;
	int err;

	len = mbuf_get_left(mb);

	/* Optional parsing of JSON body here */
	if (req->json) {

		if (len == 0) {
			warning("rest: json but no bytes to decode\n");
			goto out;
		}

		err = jzon_decode(&jobj, (char *)mbuf_buf(mb), len);
		if (err) {
			warning("rest: [%s %s] JSON parse error (%m) "
				" [%zu bytes]\n",
				req->method, req->path, err, len);
			goto out;
		}
	}

	req_close(req, 0, msg, mb, jobj);

 out:
	mem_deref(jobj);
}


/* NOTE: dont call response_complete() from here! */
static int http_data_handler(const uint8_t *buf, size_t size,
			     const struct http_msg *msg, void *arg)
{
	struct rest_req *req = arg;
	bool chunked;
	int err = 0;

	assert(REST_MAGIC == req->magic);

	chunked = http_msg_hdr_has_value(msg, HTTP_HDR_TRANSFER_ENCODING,
					 "chunked");
	
	if (!req->mb_body) {
		req->mb_body = mbuf_alloc(1024);
		if (!req->mb_body) {
			err = ENOMEM;
			goto out;
		}
	}

	/* append data to the body-buffer */
	err = mbuf_write_mem(req->mb_body, buf, size);
	if (err)
		return err;

	debug("rest: [%s %s] chunked=%d append %zu bytes, "
	      " total_length=%zu\n",
	      req->method, req->path,
	      chunked,
	      size, req->mb_body ? req->mb_body->end : 0);

 out:
	return err;
}


static void http_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	struct rest_req *req = arg;
	const struct http_hdr *hdr = 0;
	uint64_t ms_req;

	assert(REST_MAGIC == req->magic);

	if (err == ECONNABORTED)
		return;

	/* if msg==NULL then the request is complete */
	if (!err && msg == NULL) {

		if (req->mb_body)
			req->mb_body->pos = 0;
		response(req, req->msg, req->mb_body);

		return;
	}
	
	req->ts_resp = tmr_jiffies();
	ms_req = req->ts_resp - req->ts_req;

	if (err == 0) {
		hdr = http_msg_xhdr(msg, "Request-Id");
	}

	info("rest: response ReqID=%r %llums [%s %s] [%zu bytes] %d (%d)"
		  " \n",
		  hdr ? &hdr->val : NULL,
		  ms_req,
	      req->method, req->path, err ? 0 : mbuf_get_left(msg->mb),
	      err, err == 0 ? (int)msg->scode : -1
	      );

	if (err) {
		warning("rest: [%s %s] request failed: %m\n",
			req->method, req->path, err);
		goto out;
	}

	cookie_jar_handle_response(req->rest_cli->jar, req->uri,
				   msg);

	req->json =
		(0 == pl_strcasecmp(&msg->ctyp.type, "application")) &&
		(0 == pl_strcasecmp(&msg->ctyp.subtype, "json"));

#if 1
	if (msg && msg->scode >= 300) {

		info("rest: http error response: \n%b\n",
		     mbuf_buf(msg->mb), mbuf_get_left(msg->mb));
	}
#endif

	/* verify content-length, if we need to buffer */

	info("rest: response [json=%u]"
	     " -- clen %zu\n",
	     req->json, msg->clen);

	req->msg = mem_ref((struct http_msg *)msg);

	/* the raw data is handled in http_data_handler()
	 */

	if (req->mb_body)
		req->mb_body->pos = 0;
	response(req, req->msg, req->mb_body);

	return;

 out:
	if (err)
		req_close(req, err, msg, NULL, NULL);
}


void rest_client_set_token(struct rest_cli *rest,
			   const struct login_token *token)
{
	if (!rest || !token)
		return;

	rest->login_token = *token;
}


static int null_print(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	return 0;
}

static int auth_print(struct re_printf *pf, void *arg)
{
	struct login_token *token = arg;

	if (str_isset(token->access_token)) {

		return re_hprintf(pf, "Authorization: %s %s\r\n",
				  token->token_type, token->access_token);
	}

	return 0;
}


static int cookie_print(struct re_printf *pf, void *arg)
{
	struct rest_req *rr = arg;

	return cookie_jar_print_to_request(rr->rest_cli->jar, pf, rr->uri);
}


int rest_req_alloc(struct rest_req **rrp,
		   rest_resp_h *resph, void *arg, const char *method,
		   const char *path, ...)
{
	va_list ap;
	int err = 0;

	va_start(ap, path);
	err = rest_req_valloc(rrp, resph, arg, method, path, ap);
	va_end(ap);
	return err;
}


int rest_req_valloc(struct rest_req **rrp,
		    rest_resp_h *resph, void *arg, const char *method,
		    const char *path, va_list ap)
{
	struct rest_req *rr;
	int err = 0;
	if (!rrp || !method || !path)
		return EINVAL;

	rr = mem_zalloc(sizeof(*rr), req_destructor);
	if (!rr)
		return ENOMEM;

	rr->magic = REST_MAGIC;
	rr->resph = resph;
	rr->arg = arg;

	err = str_dup(&rr->method, method);
	if (err)
		goto out;

	err = re_vsdprintf(&rr->path, path, ap);
	if (err)
		goto out;

	*rrp = rr;

 out:
	if (err)
		mem_deref(rr);
	return err;
}


int rest_req_set_raw(struct rest_req *rr, bool raw)
{
	if (!rr)
		return EINVAL;
	
	rr->raw = raw;
	return 0;
}


int rest_req_add_header(struct rest_req *rr, const char *fmt, ...)
{
	va_list ap;
	int err;

	if (!rr || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = rest_req_add_header_v(rr, fmt, ap);
	va_end(ap);
	return err;
}


int rest_req_add_header_v(struct rest_req *rr, const char *fmt, va_list ap)
{
	if (!rr || !fmt)
		return EINVAL;

	return re_vsdprintf(&rr->header, fmt, ap);
}


int rest_req_add_body(struct rest_req *rr, const char *ctype,
		      const char *fmt, ...)
{
	va_list ap;
	int err;

	if (!rr || !ctype || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = rest_req_add_body_v(rr, ctype, fmt, ap);
	va_end(ap);
	return err;
}


static int add_body(struct rest_req *rr, const char *ctype)
{
	int err;
	
	err = str_dup(&rr->ctype, ctype);
	if (err)
		goto out;

	mem_deref(rr->req_body);
	rr->req_body = mbuf_alloc(1024);

 out:
	return err;
}


int rest_req_add_body_v(struct rest_req *rr, const char *ctype,
		        const char *fmt, va_list ap)
{
	int err = 0;

	if (!rr || !ctype || !fmt)
		return EINVAL;

	err = add_body(rr, ctype);
	if (err)
		goto out;
	
	err = mbuf_vprintf(rr->req_body, fmt, ap);
	if (err)
		goto out;

 out:
	return err;
}


int rest_req_add_body_raw(struct rest_req *rr, const char *ctype,
			  uint8_t *data, size_t len)
{
	int err;
	
	if (!rr || !ctype || !data)
		return EINVAL;

	err = add_body(rr, ctype);
	if (err)
		goto out;

	err = mbuf_write_mem(rr->req_body, data, len);
	if (err)
		goto out;

 out:
	return err;
}


int rest_req_add_json(struct rest_req *rr, const char *format, ...)
{
	va_list ap;
	int err;

	if (!rr || !format)
		return EINVAL;

	va_start(ap, format);
	err = rest_req_add_json_v(rr, format, ap);
	va_end(ap);

	return err;
}


int rest_req_add_json_v(struct rest_req *rr, const char *format, va_list ap)
{
	struct json_object *jobj;
	int err;

	err = jzon_vcreatf(&jobj, format, ap);
	if (err)
		return err;

	err = rest_req_add_body(rr, "application/json", "%H",
				jzon_print, jobj);
	if (err)
		goto out;

 out:
	mem_deref(jobj);
	return err;
}


int rest_req_start(struct rest_req **rrp, struct rest_req *rr,
		   struct rest_cli *rest_cli, int prio)
{
	int err = 0;

	if (!rr)
		return EINVAL;

	if (rr->raw) {
		str_dup(&rr->uri, rr->path);
	}
	else {
		err = re_sdprintf(&rr->uri, "%s%s",
				  rest_cli->server_uri, rr->path);
		if (err)
			goto out;
	}

	/* Assign this early, cookie_print needs it.  */
	rr->rest_cli = rest_cli;

	rr->prio = prio;

	if (rr->raw) {
		debug("rest_req_start: %s\n\t%s\n",
		      rr->uri, rr->header ? rr->header : "");
	}
	else {
		debug("rest_req_start: %s\n\t%H\n\t%H%s\n",
		      rr->uri, auth_print, &rest_cli->login_token,
		      cookie_print, rr, rr->header ? rr->header : "");
	}

	list_append(&rest_cli->reql, &rr->le, rr);

	if (rrp) {
		rr->reqp = rrp;
		*rrp = rr;
	}

	trigger_queue(rest_cli);

 out:
	return err;
}


static void wake_request(struct rest_req *rr)
{
	int err;

	rr->ts_req = tmr_jiffies();

	if (rr->req_body) {
		err = http_request(&rr->http_req, rr->rest_cli->http_cli,
				   rr->method, rr->uri, http_resp_handler,
				   http_data_handler, rr,
				   "%H"
				   "Accept: application/json\r\n"
				   "%s"
				   "%H"
				   "Content-Type: %s\r\n"
				   "Content-Length: %zu\r\n"
				   "User-Agent: %s\r\n"
				   "\r\n"
				   "%b"
				   ,
				   rr->raw ? null_print : auth_print,
				   rr->raw ? NULL : &rr->rest_cli->login_token,
				   rr->header ? rr->header : "",
				   cookie_print, rr,
				   rr->ctype, rr->req_body->end,
				   rr->rest_cli->user_agent,
				   rr->req_body->buf, rr->req_body->end);
	}
	else {
		err = http_request(&rr->http_req, rr->rest_cli->http_cli,
				   rr->method, rr->uri, http_resp_handler,
				   http_data_handler, rr,
				   "%H"
				   "Accept: application/json\r\n"
				   "%s"
				   "%H"
				   "Content-Length: 0\r\n"
				   "User-Agent: %s\r\n"
				   "\r\n"
				   ,
				   rr->raw ? null_print : auth_print,
				   rr->raw ? NULL : &rr->rest_cli->login_token,
				   rr->header ? rr->header : "",
				   cookie_print, rr,
				   rr->rest_cli->user_agent);
	}
	if (err) {
		debug("rest: request [%s %s] [%zu bytes] failed: %m\n",
		      rr->method, rr->path,
		      rr->req_body ? rr->req_body->end : 0,
		      err);
		goto out;
	}
	else {
		debug("rest: request [%s %s] [%zu bytes]\n",
		      rr->method, rr->path,
		      rr->req_body ? rr->req_body->end : 0);
	}

	list_append(&rr->rest_cli->openl, &rr->le, rr);

 out:
	if (err)
		req_close(rr, err, NULL, NULL, NULL);
}



/*** Convenience interface
 */

int rest_get(struct rest_req **rrp, struct rest_cli *rest_cli, int prio,
	     rest_resp_h *resph, void *arg, const char *path, ...)
{
	struct rest_req *rr;
	va_list ap;
	int err = 0;

	if (!rest_cli || !path)
		return EINVAL;

	va_start(ap, path);
	err = rest_req_valloc(&rr, resph, arg, "GET", path, ap);
	va_end(ap);
	if (err)
		return err;

	err = rest_req_start(rrp, rr, rest_cli, prio);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(rr);
	return err;
}


/**
 * @param body HTTP body content (optional)
 */
int rest_request(struct rest_req **rrp, struct rest_cli *rest_cli, int prio,
		 const char *method, rest_resp_h *resph, void *arg,
		 const char *path, const char *body, ...)
{
	struct rest_req *rr;
	va_list ap;
	int err = 0;

	if (!rest_cli || !method || !path)
		return EINVAL;

	err = rest_req_alloc(&rr, resph, arg, method, "%s", path);
	if (err)
		return err;

	if (body) {
		va_start(ap, body);
		err = rest_req_add_body_v(rr, "application/json", body, ap);
		va_end(ap);
		if (err)
			goto out;
	}

	err = rest_req_start(rrp, rr, rest_cli, prio);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(rr);
	return err;
}


int rest_request_json(struct rest_req **rrp, struct rest_cli *rest_cli,
		      int prio, const char *method,
		      rest_resp_h *resph, void *arg,
		      const char *path, uint32_t objc, ...)
{
	struct json_object *json;
	va_list ap;
	uint32_t i;
	int err = 0;

	json = json_object_new_object();
	if (!json)
		return ENOMEM;

	va_start(ap, objc);
	for (i=0; i<objc; i++) {

		char *key = va_arg(ap, char *);
		char *str = va_arg(ap, char *);

		if (!key || !str)
			break;

		json_object_object_add(json, key, json_object_new_string(str));
	}
	va_end(ap);

	err = rest_request(rrp, rest_cli, prio, method, resph, arg, path,
			   "%H", jzon_print, json);
	if (err)
		goto out;

 out:
	mem_deref(json);

	return err;
}


int rest_request_jobj(struct rest_req **rrp, struct rest_cli *rest_cli,
		      int prio, const char *method,
		      rest_resp_h *resph, void *arg,
		      struct json_object *jobj, const char *path, ...)
{
	struct rest_req *rr;
	va_list ap;
	int err = 0;

	if (!rest_cli || !method || !jobj || !path)
		return EINVAL;

	va_start(ap, path);
	err = rest_req_valloc(&rr, resph, arg, method, path, ap);
	va_end(ap);
	if (err)
		return err;

	err = rest_req_add_body(rr, "application/json", "%H",
				jzon_print, jobj);
	if (err)
		goto out;

	err = rest_req_start(rrp, rr, rest_cli, prio);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(rr);
	return err;
}


int rest_client_debug(struct re_printf *pf, const struct rest_cli *cli)
{
	struct le *le;
	int err = 0;

	err |= re_hprintf(pf, "rest client:\n");
	err |= re_hprintf(pf, "server_uri = %s\n", cli->server_uri);
	err |= re_hprintf(pf, "pending HTTP requests: (%u)\n",
			  list_count(&cli->reql));

	for (le = cli->reql.head; le; le = le->next) {

		struct rest_req *rr = le->data;

		err |= re_hprintf(pf, "  [%s %s] json=%d\n",
				  rr->method, rr->path, rr->json);
	}

	return err;
}


/*** rest_urlencode
 */

int rest_urlencode(struct re_printf *pf, const char *s)
{
	static char hex[] = "0123456789ABCDEF";
	int err; 

	/* XXX This is the most lazy implementation I could come up with -- m.
	 */
	while(*s) {
		if (*s == ' ')
			err = pf->vph("+", 1, pf->arg);
		else if (!isalnum(*s) && *s != '-' && *s != '_' &&
			 *s != '.' && *s != '~')
		{
			err = pf->vph("%", 1, pf->arg)
			    | pf->vph(hex + (*s >> 4), 1, pf->arg)
			    | pf->vph(hex + (*s & 0xF), 1, pf->arg);
		}
		else 
			err = pf->vph(s, 1, pf->arg);

		if (err)
			return err;

		++s;
	}

	return 0;
}


/*** rest_err
 */

int rest_err(int err, const struct http_msg *msg)
{
	if (err)
		return err;
	else if (!msg)
		return 0;
	else {
		switch (msg->scode) {

		case 200:
		case 201:
			return 0;
		case 404:
			return ENOENT;
		case 403:
			return EPERM;
		/* XXX Add more.  */
		default:
			return EPIPE;
		}
	}
}
