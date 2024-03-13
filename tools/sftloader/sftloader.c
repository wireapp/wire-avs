#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <re.h>
#include <avs_base.h>
#include <avs_wcall.h>
#include <avs_uuid.h>
#include <avs_jzon.h>

#define INFINITE (-1)

struct sftloader {
	bool running;
	
	char *convid;
	char *url;
	int ncalls;
	int nusers;
	int tmo;
	int duration;
	bool use_video;
	struct tmr tmr;
	struct list userl;
	struct dnsc *dnsc;	
};

struct sft_user {
	WUSER_HANDLE wuser;
	char *userid;
	char *clientid;
	char *convid;

	struct dnsc *dnsc;
	struct http_cli *httpc;

	struct {
		struct tmr t;
		struct tmr duration;
		struct tmr video;
	} tmr;

	int ncalls;
	int video_state;
	
	struct le le;
};

struct c3_req_ctx {
	WUSER_HANDLE wuser;
	void *arg;
	struct mbuf *mb_body;
	struct http_req *http_req;
};

static struct sftloader *sftloader = NULL;

static void ctx_destructor(void *arg)
{
	struct c3_req_ctx *ctx = arg;

	mem_deref(ctx->mb_body);
	mem_deref(ctx->http_req);
}


static void su_destructor(void *arg)
{
	struct sft_user *su = arg;

	tmr_cancel(&su->tmr.t);
	tmr_cancel(&su->tmr.duration);
	tmr_cancel(&su->tmr.video);
	
	mem_deref(su->convid);
	
	wcall_destroy(su->wuser);

	mem_deref(su->userid);
	mem_deref(su->clientid);
	mem_deref(su->dnsc);
	mem_deref(su->httpc);

}


static void ready_handler(int version, void *arg)
{
	struct sft_user *su = arg;
	
	re_printf("ready_handler: version=%d su=%p(%s / %s)\n",
	       version, su, su->userid, su->clientid);
}

static int send_handler(void *ctx, const char *convid,
			const char *userid_self, const char *clientid_self,
			const char *targets /*optional*/,
			const char *unused /*optional*/,
			const uint8_t *data, size_t len,
			int transient /*bool*/,
			int my_clients_only /*bool*/,
			void *arg)
{
	struct sft_user *su = arg;
	struct le *le;
	
	
	re_printf("su: %p send_handler: %s / %s\n", su, userid_self, clientid_self);

	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *uu = le->data;

		if (uu->wuser == su->wuser)
			continue;

		wcall_recv_msg(uu->wuser,
			       data, len,
			       0, 0,
			       convid,
			       userid_self, clientid_self,
			       WCALL_CONV_TYPE_CONFERENCE);
	}
	
	return 0;
}

static int dns_init(struct dnsc **dnscp)
{
	struct sa nsv[16];
	uint32_t nsn, i;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		re_printf("dns srv get: %m\n", err);
		goto out;
	}

	err = dnsc_alloc(dnscp, NULL, nsv, nsn);
	if (err) {
		re_printf("dnsc alloc: %m\n", err);
		goto out;
	}

	re_printf("engine: DNS Servers: (%u)\n", nsn);
	for (i=0; i<nsn; i++) {
		re_printf("    %J\n", &nsv[i]);
	}

 out:
	return err;
}


static int sft_data_handler(const uint8_t *buf, size_t size,
			    const struct http_msg *msg, void *arg)
{
	struct c3_req_ctx *c3ctx = arg;
	bool chunked;
	int err = 0;

	chunked = http_msg_hdr_has_value(msg, HTTP_HDR_TRANSFER_ENCODING,
					 "chunked");
	if (!c3ctx->mb_body) {
		c3ctx->mb_body = mbuf_alloc(1024);
		if (!c3ctx->mb_body) {
			err = ENOMEM;
			goto out;
		}
	}

	(void)chunked;

	/* append data to the body-buffer */
	err = mbuf_write_mem(c3ctx->mb_body, buf, size);
	if (err)
		return err;


 out:
	return err;
}


static void sft_resp_handler(int err, const struct http_msg *msg,
			     void *arg)
{
	struct c3_req_ctx *c3ctx = arg;
	const uint8_t *buf = NULL;
	int sz = 0;

#if 0
	re_printf("sft_resp: done err %d, %d bytes to send\n",
		  err, c3ctx->mb_body ? (int)c3ctx->mb_body->end : 0);
#endif
	
	if (err == ECONNABORTED)
		goto error;

	if (c3ctx->mb_body) {
		mbuf_write_u8(c3ctx->mb_body, 0);
		c3ctx->mb_body->pos = 0;

		buf = mbuf_buf(c3ctx->mb_body);
		sz = mbuf_get_left(c3ctx->mb_body);
	}

	if (buf) {
		//re_printf("sft_resp: msg %s\n", buf);
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

				re_printf("sft_resp_handler: HTML error code %d\n", errcode);
			}
		}
	}

	wcall_sft_resp(c3ctx->wuser, err,
		       buf, sz,
		       c3ctx->arg);
 error:
	mem_deref(c3ctx);
}


static int sft_handler(void *ctx, const char *url,
		       const uint8_t *data, size_t len,
		       void *arg)
{
	struct sft_user *su = arg;
	struct c3_req_ctx *c3ctx;
	int err = 0;

	re_printf("sft_handler: su: %p url: %s\n", su, url);
	c3ctx = mem_zalloc(sizeof(*c3ctx), ctx_destructor);
	if (!c3ctx)
		return ENOMEM;
	c3ctx->arg = ctx;
	c3ctx->wuser = su->wuser;

	if (!su->httpc) {
		err = http_client_alloc(&su->httpc,
					sftloader->dnsc);
		if (err) {
			re_printf("http_client_alloc failed: %m.\n", err);
			goto out;
		}
	}

	err = http_request(&c3ctx->http_req,
			   su->httpc,
			   "POST", url,
			   sft_resp_handler, sft_data_handler, c3ctx, 
			   "Accept: application/json\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: %zu\r\n"
			   "User-Agent: sftloader\r\n"
			   "\r\n"
			   "%b",
			   len, data, len);
	if (err) {
		re_printf("su(%p): sft_handler failed to send request: %m\n",
		       su, err);
	}

out:
	if (err) {
		mem_deref(c3ctx);
	}

	return err;
	
}

static void end_timeout(void *arg);



static void answer_timeout(void *arg)
{
	struct sft_user *su = arg;

	wcall_answer(su->wuser, su->convid, WCALL_CALL_TYPE_NORMAL, true);
	tmr_start(&su->tmr.duration, sftloader->duration, end_timeout, su);	
}

static void answer_call(struct sft_user *su)
{	
	int tmo;

	tmo = (rand_u32() % 10000) + 2000;

	tmr_start(&su->tmr.t, tmo, answer_timeout, su);
}

static void incoming_handler(const char *convid, uint32_t msg_time,
			     const char *userid, const char *clientid,
			     int video_call /*bool*/,
			     int should_ring /*bool*/,
			     int conv_type, /*WCALL_CONV_TYPE...*/
			     void *arg)
{
	struct sft_user *su = arg;
	
	re_printf("incoming_handler: su=%p convid=%s\n", su, convid);

	su->convid = mem_deref(su->convid);
	str_dup(&su->convid, convid);

	answer_call(su);
}


static bool has_calls(void)
{
	bool has_calls = false;
	struct le *le = sftloader->userl.head;
	
	while(le && !has_calls) {
		struct sft_user *su = le->data;

		has_calls = wcall_get_state(su->wuser, su->convid) != WCALL_STATE_NONE;
	}

	return has_calls;
}

static void close_handler(int reason,
			  const char *convid,
			  uint32_t msg_time,
			  const char *userid,
			  const char *clientid,
			  void *arg)
{
	struct sft_user *su = arg;

	re_printf("close_handler(%p) closed reason=%d\n", su, reason);

	if (!sftloader->running) {
		if (!has_calls()) {
			re_cancel();
		}
		return;
	}
	
	if (su->ncalls > 0)
		--su->ncalls;
	
	if (su->ncalls == INFINITE || su->ncalls > 0) {
		answer_call(su);
	}	
}

static void cfg_timeout(void *arg)
{
	struct sft_user *su = arg;

	char *json_str = NULL;

	struct json_object *jobj;
	struct json_object *jsfts;
	struct json_object *jsfts_all;
	struct json_object *jurls;
	struct json_object *jsft;
	struct json_object *jurl;
	struct json_object *juser;
	struct json_object *jcred;

	char *url;
	char *user = NULL;
	char *cred = NULL;

	int err = 0;

	re_printf("cfg_handler: su: %p URL=%s\n", su, sftloader->url);

	jobj = json_object_new_object();
	
	jsfts = json_object_new_array();
	if (!jsfts) {
		err = ENOMEM;
		goto out;
	}

	jsfts_all = json_object_new_array();
	if (!jsfts_all) {
		err = ENOMEM;
		goto out;
	}

	str_dup(&url, sftloader->url);
	jurls = json_object_new_array();
	jsft = jzon_alloc_object();
	user = strchr(url, '|');
	if (user) {
		*user = 0;
		user++;
		cred = strchr(user, '|');
		if (cred) {
			*cred = 0;
			cred++;
		}
	}
				
	jurl = json_object_new_string(url);
	if (!jurls || !jsft || !jurl) {
		err = ENOMEM;
		goto out;
	}

	json_object_array_add(jurls, jurl);
	json_object_object_add(jsft, "urls", jurls);
	if (user) {
		juser = json_object_new_string(user);
		json_object_object_add(jsft, "username", juser);
	}
	if (cred) {
		jcred = json_object_new_string(cred);
		json_object_object_add(jsft, "credential", jcred);
	}
	json_object_array_add(jsfts, jsft);

	jurls = json_object_new_array();
	jsft = jzon_alloc_object();
	jurl = json_object_new_string(url);
	if (!jurls || !jsft || !jurl) {
		err = ENOMEM;
		goto out;
	}
	json_object_array_add(jurls, jurl);
	json_object_object_add(jsft, "urls", jurls);
	if (user) {
		juser = json_object_new_string(user);
		json_object_object_add(jsft, "username", juser);
	}
	if (cred) {
		jcred = json_object_new_string(cred);
		json_object_object_add(jsft, "credential", jcred);
	}
	json_object_array_add(jsfts_all, jsft);
	
	mem_deref(url);

	json_object_object_add(jobj, "sft_servers", jsfts);
	json_object_object_add(jobj, "sft_servers_all", jsfts_all);
	json_object_object_add(jobj, "is_federating", json_object_new_boolean(false));

	if (!err && jobj) {
		err = jzon_encode(&json_str, jobj);
		if (err)
			goto out;
	}
 out:
	re_printf("config_update json_str=%s\n", json_str);
	wcall_config_update(su->wuser, 0, json_str);

	mem_deref(jobj);
	mem_deref(json_str);

}

static int cfg_handler(WUSER_HANDLE wuser, void *arg)
{
	struct sft_user *su = arg;
	
	tmr_start(&su->tmr.t, 1, cfg_timeout, su);

	return 0;
}

static void req_clients_handler(WUSER_HANDLE wuser,
				const char *convid, void *arg)
{
	struct sft_user *su = arg;
	struct le *le;
	
	struct json_object *jobj;
	struct json_object *jclients;
	char *json;

	jclients = json_object_new_array();
	
	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *uu = le->data;
		struct json_object *jcli;

		jcli = jzon_alloc_object();

		jzon_add_str(jcli, "userid", "%s", uu->userid);
		jzon_add_str(jcli, "clientid", "%s", uu->clientid);
		jzon_add_bool(jcli, "in_subconv", true);

		json_object_array_add(jclients, jcli);
	}

	jobj = jzon_alloc_object();
	json_object_object_add(jobj, "clients", jclients);
	jzon_encode(&json, jobj);

	wcall_set_clients_for_conv(su->wuser,
				   convid,
				   json);


	mem_deref(jobj);
	mem_deref(json);
}

static void video_timeout(void *arg)
{
	struct sft_user *su = arg;
	uint64_t tmo;

	(void)tmo;

	re_printf("video_timeout: su=%p use_video=%d\n", su, sftloader->use_video);
	
	if (!sftloader->use_video)
		return;
	
	wcall_set_video_send_state(su->wuser, su->convid, su->video_state);
	if (su->video_state == WCALL_VIDEO_STATE_STARTED) {
		su->video_state = WCALL_VIDEO_STATE_STOPPED;
		/* If only enabling video once keep the return, otherwise remove */
		return;
	}
	else {
		su->video_state = WCALL_VIDEO_STATE_STARTED;
	}
#if 1
	tmo = rand_u32() % 5000;
	tmo += 2000;
	tmr_start(&su->tmr.video, tmo, video_timeout, su);
#endif
}

static void estab_handler(const char *convid,
			  const char *userid,
			  const char *clientid,
			  void *arg)
{
	struct sft_user *su = arg;

	re_printf("estab_handler: su=%p\n", su);

	video_timeout(su);
}



static int create_user(void)
{
	struct sft_user *su;
	
	su = mem_zalloc(sizeof(*su), su_destructor);
	if (!su)
		return ENOMEM;

	uuid_v4(&su->userid);
	uuid_v4(&su->clientid);
	str_dup(&su->convid, sftloader->convid);
	su->ncalls = sftloader->ncalls;
	su->video_state = WCALL_VIDEO_STATE_STOPPED;

	tmr_init(&su->tmr.t);
	tmr_init(&su->tmr.duration);
	tmr_init(&su->tmr.video);

	su->wuser = wcall_create(su->userid,
				 su->clientid,
				 ready_handler,
				 send_handler,
				 sft_handler,
				 incoming_handler,
				 NULL,
				 NULL,
				 estab_handler,
				 close_handler,
				 NULL,
				 cfg_handler,
				 NULL,
				 NULL,
				 su);

	list_append(&sftloader->userl, &su->le, su);

	wcall_set_req_clients_handler(su->wuser, req_clients_handler);

	re_printf("create_user: su: %p wuser=0x%08x\n", su, su->wuser);
	
	
	return 0;
}

static void call_timeout(void *arg);


static void end_timeout(void *arg)
{
	struct sft_user *su = arg;

	re_printf("end_timeout(%p): wuser=%08x\n", su, su->wuser);
	
	wcall_end(su->wuser, su->convid);
}

static void call_timeout(void *arg)
{
	struct sft_user *su = arg;
	uint32_t rmo;

	rmo = rand_u32() % 120000;
	if (rmo < 30000)
		rmo += 30000;

	wcall_start(su->wuser, sftloader->convid,
		    WCALL_CALL_TYPE_NORMAL,
		    WCALL_CONV_TYPE_CONFERENCE,
		    true);

	//tmr_start(&su->tmr_duration, sftloader->duration, end_timeout, su);
}

static void start_timeout(void *arg)
{
	struct le *le = sftloader->userl.head;
	struct sft_user *su = le ? le->data : NULL;

	if (su) {
	    tmr_start(&su->tmr.t, 500, call_timeout, su);
	}
}


static void log_handler(int level,
			const char *msg,
			void *arg /*any*/)
{
	re_printf("%s", msg);
}

static void shutdown_timeout(void *arg)
{
	struct le *le = NULL;

	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *su = le ? le->data : NULL;
		if (su) {
			wcall_end(su->wuser, su->convid);
		}
	}

	list_flush(&sftloader->userl);
	re_cancel();
}

static void signal_handler(int sig)
{
	re_printf("sftloader: signal: %d received\n", sig);

	if (!sftloader->running) {
		re_cancel();
	}
	
	sftloader->running = false;

	tmr_start(&sftloader->tmr, 1, shutdown_timeout, NULL);
}


static void sl_destructor(void *arg)
{
	struct sftloader *sl = arg;

	mem_deref(sl->convid);
	mem_deref(sl->url);
	mem_deref(sl->dnsc);
	
}

int main(int argc, char **argv)
{
	int i;

	sftloader = mem_zalloc(sizeof(*sftloader), sl_destructor);
	
	sftloader->ncalls = INFINITE;
	
	for (;;) {
		const int c = getopt(argc, argv, "d:n:s:t:u:v");

		if (c < 0)
			break;

		switch (c) {
		case 'd':
			sftloader->duration = atoi(optarg) * 1000;
			break;

		case 'n':
			sftloader->ncalls = atoi(optarg);
			break;

		case 's':
			re_printf("copying URL=%s\n", optarg);
			str_dup(&sftloader->url, optarg);
			break;
			
		case 't':
			sftloader->tmo = atoi(optarg);
			break;

		case 'u':
			sftloader->nusers = atoi(optarg);
			break;

		case 'v':
			sftloader->use_video = true;
			break;

		default:
			break;
		}
	}

	sftloader->running = true;
	wcall_set_log_handler(log_handler, NULL);
	wcall_init(0);
	wcall_setup_ex(AVS_FLAG_AUDIO_TEST);
	dns_init(&sftloader->dnsc);
	uuid_v4(&sftloader->convid);

	for (i = 0; i < sftloader->nusers; ++i) {
		create_user();
	}
	tmr_init(&sftloader->tmr);
	tmr_start(&sftloader->tmr, 1, start_timeout, NULL);
	
	re_main(signal_handler);

	tmr_cancel(&sftloader->tmr);

	mem_deref(sftloader);

	wcall_close();
	
	//tmr_debug();
	//mem_debug();
	
	return 0;
}
