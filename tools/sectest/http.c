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

#include <re.h>
#include <avs.h>
#include "http.h"

static struct http_cli *http_cli = NULL;
static struct dnsc *dnsc = NULL;

static int local_dns_init(struct dnsc **dnscp)
{
	struct sa nsv[16];
	uint32_t nsn, i;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_init(NULL);
	if (err) {
		goto out;
	}
	
	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		printf("dns srv get: %d\n", err);
		goto out;
	}

	err = dnsc_alloc(dnscp, NULL, nsv, nsn);
	if (err) {
		printf("dnsc alloc: %d\n", err);
		goto out;
	}

	//printf("engine: DNS Servers: (%u)\n", nsn);
	for (i=0; i<nsn; i++) {
		//printf("    %J\n", &nsv[i]);
	}

 out:
	return err;
}

struct http_req_ctx {
	struct icall *icall;
	void *arg;
	struct mbuf *mb_body;
	struct http_req *http_req;
	http_response_h *responseh;
};

static void ctx_destructor(void *arg)
{
	struct http_req_ctx *ctx = (struct http_req_ctx*)arg;

	mem_deref(ctx->mb_body);
	mem_deref(ctx->http_req);
}

static int http_data_handler(const uint8_t *buf, size_t size,
			     const struct http_msg *msg, void *arg)
{
	struct http_req_ctx *ctx = (struct http_req_ctx*)arg;
	bool chunked;
	int err = 0;

	chunked = http_msg_hdr_has_value(msg, HTTP_HDR_TRANSFER_ENCODING,
					 "chunked");
	if (!ctx->mb_body) {
		ctx->mb_body = mbuf_alloc(1024);
		if (!ctx->mb_body) {
			err = ENOMEM;
			goto out;
		}
	}

        (void)chunked;

	/* append data to the body-buffer */
	err = mbuf_write_mem(ctx->mb_body, buf, size);
	if (err)
		return err;


 out:
	return err;
}

static void http_resp_handler(int err, const struct http_msg *msg,
			      void *arg)
{
	struct http_req_ctx *ctx = (struct http_req_ctx*)arg;
	struct econn_message *econn_msg = NULL;
	const uint8_t *buf = NULL;
	int sz = 0;

	//printf("XXXX %s\n", __FUNCTION__);
	//printf("sft_resp: done err %d, %d bytes to send\n",
	//     err, ctx->mb_body ? (int)ctx->mb_body->end : 0);
	if (err == ECONNABORTED)
		goto out;

	if (ctx->mb_body) {
		mbuf_write_u8(ctx->mb_body, 0);
		ctx->mb_body->pos = 0;

		buf = mbuf_buf(ctx->mb_body);
		sz = mbuf_get_left(ctx->mb_body);
	}

	if (buf) {
		//info("sft_resp: msg %s\n", buf);
		if (buf[0] == '<') {
			uint8_t *c = (uint8_t*)strstr((char*)buf, "<title>");
			int errcode = 0;

			if (c) {
				c += 7;
				while (c - buf < sz && *c >= '0' && *c <= '9') {
					errcode *= 10;
					errcode += *c - '0';
					c++;
				}

				warning("sft_resp_handler: HTML error code %d\n", errcode);
			}
		}
		err = econn_message_decode(&econn_msg, 0, 0,
					   (const char*)buf, sz);
		if (err == EPROTONOSUPPORT) {
			printf("wcall: recv_msg: uknown message type, ask user to update client\n");
			goto out;
		}
		else if (err) {
			warning("wcall: recv_msg: failed to decode\n");
			goto out;
		}

		//printf("XXXX %s %s message received\n", __FUNCTION__, econn_msg_name(econn_msg->msg_type));
	}

	if(ctx->responseh) {
		err = ctx->responseh(ctx->icall, err, econn_msg, ctx->arg);
		if (err)
			goto out;
	}
/*
	err = ICALL_CALLE(ctx->icall, sft_msg_recv, err, econn_msg);
	if (err)
		goto out;
*/
	//printf("XXXX %s\n", buf);
	//wcall_sft_resp(ctx->wuser, err,
	//	       buf, sz,
	//	       ctx->arg);
 out:
	if (err)
		printf("XXXX %s err=%d\n", __FUNCTION__, err);
	mem_deref(ctx);
}

int http_send_message(struct icall *icall,
		      const char *url,
		      struct econn_message *msg,
		      http_response_h *responseh,
		      void *arg)
{
	struct http_req_ctx *ctx = NULL;
	char *jstr = NULL;
	size_t len;
	int err = 0;

	info("wcall_sft_handler: url: %s\n", url);
	ctx = (struct http_req_ctx*) mem_zalloc(sizeof(*ctx), ctx_destructor);
	if (!ctx) {
		err = ENOMEM;
		goto out;
	}
	ctx->arg = arg;
	ctx->icall = icall;
	ctx->responseh = responseh;

	if (!dnsc) {
		err = local_dns_init(&dnsc);
		if (err) {
			warning("DNS client init failed: %m.\n", err);
			goto out;
		}
	}

	if (!http_cli) {
		err = http_client_alloc(&http_cli, dnsc);
		if (err) {
			warning("HTTP client init failed: %m.\n", err);
			goto out;
		}
	}

	err = econn_message_encode(&jstr, msg);
	if (err)
		goto out;

	len = strlen(jstr);

	err = http_request(&ctx->http_req, http_cli,
			   "POST", url, http_resp_handler, http_data_handler, ctx, 
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: %zu\r\n"
			   "User-Agent: zcall\r\n"
			   "\r\n"
			   "%b",
			   len, jstr, len);
	if (err) {
		warning("wcall(%p): sft_handler failed to send request: %m\n", ctx, err);
	}
out:
	if (err) {
		mem_deref(ctx);
	}

	return err;
}

