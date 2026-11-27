#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include <re.h>
#include <avs.h>
#include <avs_wcall.h>

#define INFINITE (-1)

struct sftloader {
	bool running;
	
	char *convid;
	char *sft_url;
	char *sfts_all;
	char *users_cfg;
	bool is_federating;
	uint32_t ncalls;
	uint32_t nusers;
	uint32_t clientno;
	uint32_t tmo;
	uint32_t duration;
	bool use_video;
	bool verify_video;
	bool video_octo;
	uint32_t max_vstreams; /* video streams requested per receiver */
	uint32_t rotate_ms;    /* rotate requested streams, 0: off */
	uint32_t nusers_created;
	uint32_t video_sync_ms; /* start all video at t0 + this, 0: off */
	uint64_t t0;
	struct tmr tmr;
	struct tmr tmr_vreport;
	struct list userl;
	struct dnsc *dnsc;

	FILE *logfp;
};

#define VP_MAX 32
#define VP_ID_LEN 128

struct sft_user {
	WUSER_HANDLE wuser;
	char *userid;
	char *clientid;
	char *convid;

	/* Per-user SFT config, sent in CONFIG updates.
	 * Defaults to the global -s/-S/-f values, can be
	 * overridden per user with -C <users.json>.
	 */
	char *sft_url;   /* "url[|username|credential]" */
	char *sfts_all;  /* comma separated list of urls */
	bool is_federating;
	int answer_ms;   /* <0: random 2-12s */

	/* Other participants that send video, sorted by user/client id,
	 * and the rotation offset into it for tile switching (-W/-R).
	 */
	struct {
		char userid[VP_MAX][VP_ID_LEN];
		char clientid[VP_MAX][VP_ID_LEN];
		uint32_t n;
		uint32_t rot;
	} vpeers;

	struct dnsc *dnsc;
	struct http_cli *httpc;

	struct {
		struct tmr t;
		struct tmr duration;
		struct tmr video;
		struct tmr rotate;
	} tmr;

	int ncalls;
	int video_state;
	bool done;       /* no more calls expected for this user */
	
	struct le le;
};

struct c3_req_ctx {
	//WUSER_HANDLE wuser;
	struct sft_user *su;
	void *arg;
	struct mbuf *mb_body;
	struct http_req *http_req;
};

void test_capturer_init(void);
void test_capturer_start_dynamic(uint32_t w, uint32_t h, uint32_t fps);
void test_capturer_start_head(uint32_t w, uint32_t h, uint32_t fps);
void test_capturer_stop(void);
void test_capturer_stamp_id(struct avs_vidframe *frame, uint16_t id);
int32_t test_capturer_read_id(const struct avs_vidframe *frame);

/*
 * Video verification (-V): every user stamps its index into the video it
 * sends; for every decoded frame the receiver checks that the stamp
 * matches the user/client AVS attributes the stream to.
 *
 * The tables are filled before the call starts and only read from the
 * media threads, so they are fixed size and not freed until the
 * capturer has stopped.
 */
#define VID_ID_LEN 128
#define VID_REPORT_MS 5000

struct vid_peer {
	char userid[VID_ID_LEN];
	char clientid[VID_ID_LEN];
};

struct vid_rx {
	uint32_t frames;     /* decoded frames */
	uint32_t ok;         /* stamp matches attributed sender */
	uint32_t wrong;      /* stamp is another user's */
	uint32_t unreadable; /* no valid stamp */
	int32_t  wrong_id;   /* last wrong stamp seen */
};

static struct {
	pthread_mutex_t lock;
	uint32_t n;
	struct vid_peer *peers;  /* [n] */
	struct vid_rx *rx;       /* [n * n], rx[recv * n + send] */
	uint32_t *unknown;       /* [n], frames from unknown senders */
} g_vid = {PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL, NULL};

static struct sftloader *sftloader = NULL;
#if 0
const static char fake_userid[] = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
const static char fake_clientid[] = "aaaaaaaaaaaaaaaa";
const static char fake_secret[] = "ZG9udGxvb2tkb250bG9vaw==";
#endif

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
	tmr_cancel(&su->tmr.rotate);
	
	mem_deref(su->userid);
	mem_deref(su->clientid);
	mem_deref(su->convid);
	mem_deref(su->sft_url);
	mem_deref(su->sfts_all);

	wcall_destroy(su->wuser);

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
	
	
	re_printf("su: %p send_handler: %s / %s\n"
		  "JSON=%s\n", su, userid_self, clientid_self, data);

	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *uu = le->data;

		if (uu->wuser == su->wuser)
			continue;

		wcall_recv_msg(uu->wuser,
			       data, len,
			       0, 0,
			       convid,
			       userid_self, clientid_self,
			       WCALL_CONV_TYPE_CONFERENCE_MLS,
			       0);
	}
	
	return 0;
}

static int sl_dns_init(struct dnsc **dnscp)
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
	struct sft_user *su = c3ctx->su;
	const uint8_t *buf = NULL;
	int sz = 0;

#if 1
	debug("sft_resp: su=%p err=%d %d bytes\n",
	      su, err, c3ctx->mb_body ? (int)c3ctx->mb_body->end : 0);
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
		re_printf("sft_resp: msg %s\n", buf);
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

				warning("sft_resp_handler: su=%p HTML error code %d\n", su, errcode);
			}
		}
	}

	wcall_sft_resp(su->wuser, err,
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

	c3ctx = mem_zalloc(sizeof(*c3ctx), ctx_destructor);
	if (!c3ctx)
		return ENOMEM;
	c3ctx->arg = ctx;
	c3ctx->su = su;

	if (!su->httpc) {
		err = http_client_alloc(&su->httpc,
					sftloader->dnsc);
		if (err) {
			re_printf("http_client_alloc failed: %m.\n", err);
			goto out;
		}
	}

#if 1
	debug("sft_req: su=%p %d bytes\n", su, len);
#endif
	
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
	int call_type = sftloader->use_video ? WCALL_CALL_TYPE_VIDEO
	                                     : WCALL_CALL_TYPE_NORMAL;
	
	wcall_answer(su->wuser, su->convid, call_type, true);
	tmr_start(&su->tmr.duration, sftloader->duration, end_timeout, su);	
}

static void answer_call(struct sft_user *su)
{	
	int tmo;

	if (su->answer_ms >= 0)
		tmo = su->answer_ms;
	else
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
		
		le = le->next;
		has_calls = wcall_get_state(su->wuser, su->convid) != WCALL_STATE_NONE;
	}

	return has_calls;
}

static bool all_users_done(void)
{
	struct le *le;

	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *su = le->data;

		if (!su->done)
			return false;
	}

	return true;
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

	tmr_cancel(&su->tmr.rotate);
	su->vpeers.n = 0;

	if (!sftloader->running) {
		if (!has_calls()) {
			re_cancel();
		}
		return;
	}
	
	if (su->ncalls > 0)
		--su->ncalls;
	
	if (su->ncalls == INFINITE || su->ncalls > 0) {
		re_printf("close_handler(%p): answering\n", su);
		answer_call(su);
	}
	else {
		/* Leaving a conference that others are still in gives
		 * STILL_ONGOING, not NORMAL, so accept any reason here
		 * and stop once every user is done.
		 */
		su->done = true;
		if (all_users_done()) {
			re_printf("close_handler(%p): all users done, "
				  "ending\n", su);
			re_cancel();
		}
	}
}

/* Append one SFT entry to jarr. spec is "url[|username|credential]" */
static int add_sft_entry(struct json_object *jarr, const char *spec)
{
	struct json_object *jsft;
	struct json_object *jurls;
	char *url = NULL;
	char *user = NULL;
	char *cred = NULL;
	int err;

	err = str_dup(&url, spec);
	if (err)
		return err;

	user = strchr(url, '|');
	if (user) {
		*user++ = 0;
		cred = strchr(user, '|');
		if (cred)
			*cred++ = 0;
	}

	jsft = jzon_alloc_object();
	jurls = json_object_new_array();
	if (!jsft || !jurls) {
		mem_deref(jsft);
		mem_deref(jurls);
		err = ENOMEM;
		goto out;
	}

	json_object_array_add(jurls, json_object_new_string(url));
	json_object_object_add(jsft, "urls", jurls);
	if (user && *user)
		json_object_object_add(jsft, "username",
				       json_object_new_string(user));
	if (cred && *cred)
		json_object_object_add(jsft, "credential",
				       json_object_new_string(cred));

	json_object_array_add(jarr, jsft);

 out:
	mem_deref(url);
	return err;
}

static void cfg_timeout(void *arg)
{
	struct sft_user *su = arg;
	struct json_object *jobj = NULL;
	struct json_object *jsfts;
	struct json_object *jsfts_all;
	char *json_str = NULL;
	char *sfts_all = NULL;
	char *surl;
	char *at;
	int err = 0;

	re_printf("cfg_handler: su: %p(%s) URL=%s ALL=%s federating=%d\n",
		  su, su->userid, su->sft_url, su->sfts_all,
		  su->is_federating);

	/* NOTE: jzon copies a child object when it is added to its
	 * container (and releases the original), so the arrays must
	 * be complete before they are added to jobj.
	 */
	jobj = jzon_alloc_object();
	jsfts = json_object_new_array();
	jsfts_all = json_object_new_array();
	if (!jobj || !jsfts || !jsfts_all) {
		err = ENOMEM;
		goto out;
	}

	if (su->sft_url) {
		err = add_sft_entry(jsfts, su->sft_url);
		if (err)
			goto out;
	}

	/* If no explicit list of all SFTs, use the SFT url */
	if (su->sfts_all) {
		err = str_dup(&sfts_all, su->sfts_all);
	}
	else if (su->sft_url) {
		err = str_dup(&sfts_all, su->sft_url);
		if (!err) {
			char *p = strchr(sfts_all, '|');
			if (p)
				*p = 0;
		}
	}
	if (err)
		goto out;

	if (sfts_all) {
		surl = strtok_r(sfts_all, ",", &at);
		while (surl) {
			err = add_sft_entry(jsfts_all, surl);
			if (err)
				goto out;
			surl = strtok_r(NULL, ",", &at);
		}
	}

	json_object_object_add(jobj, "sft_servers", jsfts);
	json_object_object_add(jobj, "sft_servers_all", jsfts_all);
	jsfts = jsfts_all = NULL;
	json_object_object_add(jobj, "is_federating",
			       json_object_new_boolean(su->is_federating));

	err = jzon_encode(&json_str, jobj);

 out:
	if (err)
		warning("cfg_timeout: su=%p failed to build config: %m\n",
			su, err);

	mem_deref(jsfts);
	mem_deref(jsfts_all);

	re_printf("config_update json_str=%s\n", json_str);
	wcall_config_update(su->wuser, err, json_str);

	mem_deref(sfts_all);
	mem_deref(jobj);
	mem_deref(json_str);
}

static int cfg_handler(WUSER_HANDLE wuser, void *arg)
{
	struct sft_user *su = arg;
	
	tmr_start(&su->tmr.t, 1, cfg_timeout, su);

	return 0;
}

#if 0
static void create_confstart(char *buf, size_t *blen)
{
	memset(buf, 0, *blen);

	if (sftloader->start_url)
		snprintf(buf, *blen, "{\"version\":\"3.0\",\"type\":\"CONFSTART\",\"sessid\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"src_userid\":\"%s\",\"src_clientid\":\"%s\",\"resp\":false,\"sft_url\":\"%s\",\"secret\":\"%s\",\"timestamp\":\"1\",\"seqno\":\"1\",\"sfts\":[\"%s\",\"%s\"],\"props\":{\"videosend\":\"%s\"}}",
			 fake_userid,
			 fake_clientid,
			 sftloader->start_url,
			 fake_secret,
			 sftloader->start_url,
			 sftloader->sft_url,
			 sftloader->use_video ? "true" : "false");
	else
		snprintf(buf, *blen, "{\"version\":\"3.0\",\"type\":\"CONFSTART\",\"sessid\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"src_userid\":\"%s\",\"src_clientid\":\"%s\",\"resp\":false,\"sft_url\":\"%s\",\"secret\":\"%s\",\"timestamp\":\"1\",\"seqno\":\"1\",\"sfts\":[\"%s\"],\"props\":{\"videosend\":\"%s\"}}",
			 fake_userid,
			 fake_clientid,
			 sftloader->sft_url,
			 fake_secret,
			 sftloader->sft_url,
			 sftloader->use_video ? "true" : "false");
	*blen = strlen(buf);
	re_printf("confstart=%s\n", buf);
}
#endif

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
		uint32_t c;

		for (c = 0; c < 10; c++) {
			char cid[ECONN_ID_LEN];

			snprintf(cid, ECONN_ID_LEN, "%04u", c);
			jcli = jzon_alloc_object();

			jzon_add_str(jcli, "userid", "%s", uu->userid);
			jzon_add_str(jcli, "clientid", "%s", cid);
			jzon_add_bool(jcli, "in_subconv", true);

			json_object_array_add(jclients, jcli);
		}
	}

	jobj = jzon_alloc_object();
	json_object_object_add(jobj, "clients", jclients);
	jzon_encode(&json, jobj);

	wcall_set_epoch_info(su->wuser,
			     convid,
			     1,
			     json,
			     "c3VwZXJzZWNyZXRrZXkwMA==");

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
	if (sftloader->video_sync_ms) {
		/* Same start for every sender, so their encoders' frame
		 * ids (which start at 0) stay close to each other.
		 */
		uint64_t at = sftloader->t0 + sftloader->video_sync_ms;
		uint64_t now = tmr_jiffies();

		if (now >= at) {
			re_printf("video_timeout: user %s.%s joined after "
				  "the synced video start\n",
				  su->userid, su->clientid);
			tmo = 0;
		}
		else {
			tmo = at - now;
		}
	}
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

static int vpeer_cmp(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* Request video from at most max_vstreams of the participants that send
 * video, starting at the rotation offset. With fewer streams than video
 * senders, changing the offset makes the SFT hand a stream (tile) to a
 * different source.
 */
static void request_vstreams(struct sft_user *su)
{
	struct json_object *robj = NULL;
	struct json_object *rclients;
	char *json_str = NULL;
	uint32_t i, n;
	char sel[512] = "";
	size_t pos = 0;

	if (su->vpeers.n == 0)
		return;

	n = min(su->vpeers.n, sftloader->max_vstreams);

	robj = jzon_alloc_object();
	rclients = json_object_new_array();
	for (i = 0; i < n; ++i) {
		uint32_t k = (su->vpeers.rot + i) % su->vpeers.n;
		struct json_object *rcli = jzon_alloc_object();

		jzon_add_str(rcli, "userid", "%s", su->vpeers.userid[k]);
		jzon_add_str(rcli, "clientid", "%s", su->vpeers.clientid[k]);
		json_object_array_add(rclients, rcli);

		if (pos < sizeof(sel))
			pos += re_snprintf(sel + pos, sizeof(sel) - pos,
					   "%s%s", i ? "," : "",
					   su->vpeers.userid[k]);
	}
	jzon_add_str(robj, "convid", "%s", su->convid);
	json_object_object_add(robj, "clients", rclients);

	jzon_encode(&json_str, robj);
	if (json_str) {
		if (sftloader->rotate_ms) {
			re_printf("TILES user=%s.%s streams=[%s]\n",
				  su->userid, su->clientid, sel);
		}
		wcall_request_video_streams(su->wuser, su->convid, 0,
					    json_str);
	}

	mem_deref(robj);
	mem_deref(json_str);
}

static void rotate_timeout(void *arg)
{
	struct sft_user *su = arg;

	su->vpeers.rot++;
	request_vstreams(su);

	tmr_start(&su->tmr.rotate, sftloader->rotate_ms, rotate_timeout, su);
}

static void participant_changed_handler(const char *convid,
					const char *mjson,
					void *arg)
{
	struct sft_user *su = arg;
	struct json_object *jobj = NULL;
	struct json_object *jclients = NULL;
	size_t i, nclients = 0;
	uint32_t n = 0;
	int err = 0;

	size_t jlen = strlen(mjson);
	err = jzon_decode(&jobj, mjson, jlen);
	if (err)
		goto out;

	err = jzon_array(&jclients, jobj, "members");
	if (err)
		goto out;

	if (!jzon_is_array(jclients))
		goto out;

	nclients = json_object_array_length(jclients);

	for (i = 0; i < nclients && n < VP_MAX; ++i) {
		const char *uid, *cid;
		struct json_object *jcli;
		int32_t vstate;

		jcli = json_object_array_get_idx(jclients, i);
		if (!jcli)
			goto out;

		err = jzon_int(&vstate, jcli, "vrecv");
		if (err)
			goto out;

		uid = jzon_str(jcli, "userid");
		cid = jzon_str(jcli, "clientid");
		if (!uid || !cid)
			continue;
		if (vstate != WCALL_VIDEO_STATE_STARTED)
			continue;
		if (streq(su->userid, uid) && streq(su->clientid, cid))
			continue;

		re_snprintf(su->vpeers.userid[n], VP_ID_LEN, "%s", uid);
		re_snprintf(su->vpeers.clientid[n], VP_ID_LEN, "%s", cid);
		++n;
	}
	su->vpeers.n = n;

	if (n == 0)
		goto out;

	/* Stable order, so rotation walks every sender in turn */
	if (n > 1) {
		char keys[VP_MAX][2 * VP_ID_LEN];
		char uids[VP_MAX][VP_ID_LEN];
		char cids[VP_MAX][VP_ID_LEN];
		uint32_t k;

		for (k = 0; k < n; ++k) {
			re_snprintf(keys[k], sizeof(keys[k]), "%s.%s.%u",
				    su->vpeers.userid[k],
				    su->vpeers.clientid[k], k);
		}
		qsort(keys, n, sizeof(keys[0]), vpeer_cmp);
		memcpy(uids, su->vpeers.userid, sizeof(uids));
		memcpy(cids, su->vpeers.clientid, sizeof(cids));
		for (k = 0; k < n; ++k) {
			const char *dot = strrchr(keys[k], '.');
			uint32_t ix = (uint32_t)atoi(dot + 1);

			str_ncpy(su->vpeers.userid[k], uids[ix], VP_ID_LEN);
			str_ncpy(su->vpeers.clientid[k], cids[ix], VP_ID_LEN);
		}
	}

	request_vstreams(su);

	if (sftloader->rotate_ms && !tmr_isrunning(&su->tmr.rotate)) {
		tmr_start(&su->tmr.rotate, sftloader->rotate_ms,
			  rotate_timeout, su);
	}
out:
	mem_deref(jobj);
}

static void quality_handler(const char *convid,
			    const char *userid,
			    const char *clientid,
			    const char* quality_info,
			    void *arg)
{
	struct sft_user *su = arg;

	(void)su;
	
	re_printf("quality_handler: %s[%s.%s] quality: %s\n",
		  convid, su->userid, su->clientid, quality_info);
}


static int vid_peer_index(const char *userid, const char *clientid)
{
	uint32_t i;

	if (!userid || !clientid)
		return -1;

	for (i = 0; i < g_vid.n; ++i) {
		if (0 == str_casecmp(g_vid.peers[i].userid, userid) &&
		    streq(g_vid.peers[i].clientid, clientid))
			return (int)i;
	}

	return -1;
}

/* Called from the capture thread, once per sending user */
static void vid_stamp_handler(struct avs_vidframe *frame,
			      const char *userid_self,
			      const char *clientid_self,
			      void *arg)
{
	int idx = vid_peer_index(userid_self, clientid_self);

	(void)arg;

	if (idx >= 0)
		test_capturer_stamp_id(frame, (uint16_t)(idx + 1));
}

/* Called from the render thread for every decoded frame */
static void vid_render_tap(const struct avs_vidframe *frame,
			   const char *userid_self,
			   const char *clientid_self,
			   const char *userid_remote,
			   const char *clientid_remote,
			   void *arg)
{
	int ridx = vid_peer_index(userid_self, clientid_self);
	int sidx = vid_peer_index(userid_remote, clientid_remote);
	int32_t id;
	struct vid_rx *rx;

	(void)arg;

	if (ridx < 0)
		return;

	id = test_capturer_read_id(frame);

	pthread_mutex_lock(&g_vid.lock);
	if (sidx < 0) {
		g_vid.unknown[ridx]++;
	}
	else {
		rx = &g_vid.rx[ridx * g_vid.n + sidx];
		rx->frames++;
		if (id == sidx + 1) {
			rx->ok++;
		}
		else if (id > 0) {
			rx->wrong++;
			rx->wrong_id = id;
		}
		else {
			rx->unreadable++;
		}
	}
	pthread_mutex_unlock(&g_vid.lock);
}

/* Prints per-pair results, returns the number of failed pairs.
 * A pair fails if no correctly stamped frame was received,
 * or if any frame carried another sender's stamp.
 */
static uint32_t vid_report(const char *tag)
{
	uint32_t r, s;
	uint32_t nfailed = 0;

	if (!g_vid.n)
		return 0;

	pthread_mutex_lock(&g_vid.lock);
	for (r = 0; r < g_vid.n; ++r) {
		for (s = 0; s < g_vid.n; ++s) {
			struct vid_rx *rx = &g_vid.rx[r * g_vid.n + s];
			const char *wrong_from = "-";

			if (r == s)
				continue;

			if (rx->wrong_id > 0 &&
			    (uint32_t)rx->wrong_id <= g_vid.n)
				wrong_from = g_vid.peers[rx->wrong_id - 1].userid;

			if (rx->ok == 0 || rx->wrong > 0)
				++nfailed;

			re_printf("VIDEO_RX[%s] recv=%s.%s from=%s.%s "
				  "frames=%u ok=%u wrong=%u unreadable=%u "
				  "wrong_from=%s\n",
				  tag,
				  g_vid.peers[r].userid, g_vid.peers[r].clientid,
				  g_vid.peers[s].userid, g_vid.peers[s].clientid,
				  rx->frames, rx->ok, rx->wrong,
				  rx->unreadable, wrong_from);
		}
		if (g_vid.unknown[r]) {
			re_printf("VIDEO_RX[%s] recv=%s.%s from=unknown "
				  "frames=%u\n",
				  tag,
				  g_vid.peers[r].userid, g_vid.peers[r].clientid,
				  g_vid.unknown[r]);
		}
	}
	pthread_mutex_unlock(&g_vid.lock);

	return nfailed;
}

static void vid_report_timeout(void *arg)
{
	(void)arg;

	vid_report("progress");
	tmr_start(&sftloader->tmr_vreport, VID_REPORT_MS,
		  vid_report_timeout, NULL);
}

static int vid_verify_init(void)
{
	struct le *le;
	uint32_t i = 0;

	g_vid.n = list_count(&sftloader->userl);
	if (g_vid.n == 0 || g_vid.n > 0xFFFE)
		return EINVAL;

	g_vid.peers = calloc(g_vid.n, sizeof(*g_vid.peers));
	g_vid.rx = calloc((size_t)g_vid.n * g_vid.n, sizeof(*g_vid.rx));
	g_vid.unknown = calloc(g_vid.n, sizeof(*g_vid.unknown));
	if (!g_vid.peers || !g_vid.rx || !g_vid.unknown)
		return ENOMEM;

	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *su = le->data;

		str_ncpy(g_vid.peers[i].userid, su->userid, VID_ID_LEN);
		str_ncpy(g_vid.peers[i].clientid, su->clientid, VID_ID_LEN);
		re_printf("vid_verify: user %s.%s stamps id %u\n",
			  su->userid, su->clientid, i + 1);
		++i;
	}

	capture_source_set_stamp_handler(vid_stamp_handler, NULL);
	peerflow_set_render_tap(vid_render_tap, NULL);

	tmr_start(&sftloader->tmr_vreport, VID_REPORT_MS,
		  vid_report_timeout, NULL);

	return 0;
}

/* Returns true if every pair received correctly stamped video */
static bool vid_verify_stop(void)
{
	uint32_t npairs = g_vid.n ? g_vid.n * (g_vid.n - 1) : 0;
	uint32_t nfailed;

	capture_source_set_stamp_handler(NULL, NULL);
	peerflow_set_render_tap(NULL, NULL);

	nfailed = vid_report("final");

	re_printf("VIDEO_VERIFY: %s pairs=%u failed=%u\n",
		  nfailed ? "FAIL" : "PASS", npairs, nfailed);

	return nfailed == 0;
}

/* Only call once the media threads are gone (after wcall_close) */
static void vid_verify_free(void)
{
	g_vid.n = 0;
	free(g_vid.peers);
	free(g_vid.rx);
	free(g_vid.unknown);
	g_vid.peers = NULL;
	g_vid.rx = NULL;
	g_vid.unknown = NULL;
}

static int create_user(struct sft_user **sup,
		       const char *userid, const char *clientid)
{
	struct sft_user *su;
	
	su = mem_zalloc(sizeof(*su), su_destructor);
	if (!su)
		return ENOMEM;

	str_dup(&su->userid, userid);
	str_dup(&su->clientid, clientid);
	str_dup(&su->convid, sftloader->convid);
	if (sftloader->sft_url)
		str_dup(&su->sft_url, sftloader->sft_url);
	if (sftloader->sfts_all)
		str_dup(&su->sfts_all, sftloader->sfts_all);
	su->is_federating = sftloader->is_federating;
	su->answer_ms = -1;
	su->ncalls = sftloader->ncalls;
	su->video_state = WCALL_VIDEO_STATE_STOPPED;

	tmr_init(&su->tmr.t);
	tmr_init(&su->tmr.duration);
	tmr_init(&su->tmr.video);
	tmr_init(&su->tmr.rotate);
	/* Start every receiver at a different tile */
	su->vpeers.rot = sftloader->nusers_created++;

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
	wcall_set_participant_changed_handler(su->wuser, participant_changed_handler, su);
	wcall_set_network_quality_handler(su->wuser, quality_handler, 10, su);

	re_printf("create_user: su: %p wuser=0x%08x %s.%s\n",
		  su, su->wuser, su->userid, su->clientid);

	if (sup)
		*sup = su;

	return 0;
}

static int create_numbered_user(uint32_t uidno, uint32_t cidno)
{
	char userid[ECONN_ID_LEN];
	char clientid[ECONN_ID_LEN];

	snprintf(userid, sizeof(userid), "usr%05u", uidno);
	snprintf(clientid, sizeof(clientid), "%04u", cidno);

	return create_user(NULL, userid, clientid);
}

/* Join an optional "sfts_all" value (array of strings, or a comma
 * separated string) into a comma separated string.
 */
static int sfts_all_from_json(char **dstp, struct json_object *jusr)
{
	struct json_object *jall;
	struct mbuf *mb = NULL;
	int i, n;
	int err = 0;

	if (!json_object_object_get_ex(jusr, "sfts_all", &jall) || !jall)
		return 0;

	if (!jzon_is_array(jall))
		return str_dup(dstp, json_object_get_string(jall));

	n = json_object_array_length(jall);
	if (n == 0)
		return 0;

	mb = mbuf_alloc(256);
	if (!mb)
		return ENOMEM;

	for (i = 0; i < n; ++i) {
		struct json_object *jurl = json_object_array_get_idx(jall, i);
		const char *url = jurl ? json_object_get_string(jurl) : NULL;

		if (!url)
			continue;
		err = mbuf_printf(mb, "%s%s", mb->end ? "," : "", url);
		if (err)
			goto out;
	}

	mb->pos = 0;
	err = mbuf_strdup(mb, dstp, mb->end);

 out:
	mem_deref(mb);
	return err;
}

/* Load per-user config from a JSON file:
 * [
 *   {"userid": "usrA", "clientid": "0000",
 *    "sft_url": "https://sft1[|user|cred]",
 *    "sfts_all": ["https://sft1", ...] | "https://sft1,https://sft2",
 *    "is_federating": true,
 *    "answer_ms": 1000},
 *   ...
 * ]
 * Only "userid" is mandatory, other fields default to the global
 * -c/-s/-S/-f values. The first entry starts the call.
 */
static int load_users_cfg(const char *path)
{
	struct json_object *jarr = NULL;
	struct mbuf *mb = NULL;
	FILE *fp;
	char buf[4096];
	size_t n;
	int i, nusers;
	int err = 0;

	fp = fopen(path, "r");
	if (!fp) {
		err = errno;
		re_fprintf(stderr, "sftloader: cannot open %s: %m\n",
			   path, err);
		return err;
	}

	mb = mbuf_alloc(sizeof(buf));
	if (!mb) {
		err = ENOMEM;
		goto out;
	}
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		err = mbuf_write_mem(mb, (uint8_t *)buf, n);
		if (err)
			goto out;
	}

	err = jzon_decode(&jarr, (char *)mb->buf, mb->end);
	if (err || !jzon_is_array(jarr)) {
		re_fprintf(stderr, "sftloader: %s: expected a JSON array\n",
			   path);
		err = err ? err : EINVAL;
		goto out;
	}

	nusers = json_object_array_length(jarr);
	for (i = 0; i < nusers; ++i) {
		struct json_object *jusr = json_object_array_get_idx(jarr, i);
		struct sft_user *su = NULL;
		const char *userid;
		char clientid[ECONN_ID_LEN];
		char *str = NULL;
		int answer_ms;

		userid = jusr ? jzon_str(jusr, "userid") : NULL;
		if (!userid) {
			re_fprintf(stderr, "sftloader: %s: entry %d has "
				   "no userid\n", path, i);
			err = EINVAL;
			goto out;
		}
		if (jzon_str(jusr, "clientid"))
			str_ncpy(clientid, jzon_str(jusr, "clientid"),
				 sizeof(clientid));
		else
			snprintf(clientid, sizeof(clientid), "%04u",
				 sftloader->clientno);

		err = create_user(&su, userid, clientid);
		if (err)
			goto out;

		if (0 == jzon_strdup(&str, jusr, "sft_url")) {
			mem_deref(su->sft_url);
			su->sft_url = str;
			str = NULL;
		}

		err = sfts_all_from_json(&str, jusr);
		if (err)
			goto out;
		if (str) {
			mem_deref(su->sfts_all);
			su->sfts_all = str;
		}

		su->is_federating = jzon_bool_opt(jusr, "is_federating",
						  su->is_federating);
		if (0 == jzon_int(&answer_ms, jusr, "answer_ms"))
			su->answer_ms = answer_ms;

		re_printf("user[%d]: %s.%s sft=%s all=%s fed=%d answer=%d\n",
			  i, su->userid, su->clientid,
			  su->sft_url, su->sfts_all,
			  su->is_federating, su->answer_ms);
	}
	sftloader->nusers = nusers;

 out:
	fclose(fp);
	mem_deref(mb);
	mem_deref(jarr);

	return err;
}

//static void call_timeout(void *arg);


static void end_timeout(void *arg)
{
	struct sft_user *su = arg;

	re_printf("end_timeout(%p): wuser=%08x\n", su, su->wuser);
	
	wcall_end(su->wuser, su->convid);
}


#if 0
static void call_timeout(void *arg)
{
	char cs_buf[2048];
	size_t cs_len = sizeof(cs_buf);
	struct le *le = NULL;

	uint32_t rmo;

	rmo = rand_u32() % 120000;
	if (rmo < 30000)
		rmo += 30000;

	create_confstart(cs_buf, &cs_len);

	LIST_FOREACH(&sftloader->userl, le) {
		struct sft_user *uu = le->data;

		wcall_recv_msg(uu->wuser,
			       (uint8_t*)cs_buf, cs_len,
			       0, 0,
			       sftloader->convid,
			       fake_userid, fake_clientid,
			       WCALL_CONV_TYPE_CONFERENCE_MLS);
	}

	//tmr_start(&su->tmr_duration, sftloader->duration, end_timeout, su);
}
#endif

static void start_call(struct sft_user *su)
{
	int call_type = sftloader->use_video ? WCALL_CALL_TYPE_VIDEO
	                                     : WCALL_CALL_TYPE_NORMAL;

        wcall_start(su->wuser, sftloader->convid, call_type,
		    WCALL_CONV_TYPE_CONFERENCE_MLS, 1, 0);

	/* The caller must leave too, or the process never ends */
	if (sftloader->duration)
		tmr_start(&su->tmr.duration, sftloader->duration,
			  end_timeout, su);
}

static void start_timeout(void *arg)
{
	struct le *le = sftloader->userl.head;
	struct sft_user *su = le ? le->data : NULL;


	if (su)
	  start_call(su);
#if 0
	if (su) {
	    tmr_start(&su->tmr.t, 500, call_timeout, su);
	}
#endif
}

static const char *level_prefix(enum log_level level)
{
	switch (level) {

	case LOG_LEVEL_DEBUG: return "DEBUG  : ";
	case LOG_LEVEL_INFO:  return "INFO   : ";
	case LOG_LEVEL_WARN:  return "WARNING: ";
	case LOG_LEVEL_ERROR: return "ERROR  : ";
	default:              return "       : ";
	}
}


static void log_handler(int level,
			const char *msg,
			void *arg /*any*/)
{
	static struct lock *log_lock = NULL;
	struct timeval tv;
	const pthread_t tid = pthread_self();

	if (!log_lock) {
		lock_alloc(&log_lock);
	}

	lock_write_get(log_lock);

	if (gettimeofday(&tv, NULL) != 0) {
		re_fprintf(sftloader->logfp, "T(0x%08x) %s",
			   (void *)tid,
			   level_prefix(level), msg);
	}
	else {
		struct tm  tstruct;
		uint32_t tms;
		char timebuf[64];

		memset(timebuf, 0, 64);
		tstruct = *localtime(&tv.tv_sec);
		tms = tv.tv_usec / 1000;
		strftime(timebuf, sizeof(timebuf), "%m-%d %X", &tstruct);
		re_fprintf(sftloader->logfp, "%s.%03u T(0x%08x) %s%s",
			   timebuf, tms,
			   (void *)tid,
			   level_prefix(level), msg);
	}

	lock_rel(log_lock);
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
	mem_deref(sl->sft_url);
	mem_deref(sl->sfts_all);
	mem_deref(sl->users_cfg);
	mem_deref(sl->dnsc);
	
}

static void usage(void)
{
	re_fprintf(stderr,
		   "usage: sftloader [options]\n"
		   "(see tools/sftloader/README.md for examples)\n"
		   "\t-h            show this help\n"
		   "\t-u <n>        number of users (usr00000, usr00001, ...)\n"
		   "\t-c <n>        clientid number (default 0 -> \"0000\")\n"
		   "\t-s <url>      SFT url (url[|user|cred])\n"
		   "\t-S <urls>     comma separated list of all SFTs\n"
		   "\t-f            federating\n"
		   "\t-C <file>     per-user config JSON, overrides -u\n"
		   "\t-i <convid>   conversation id (default: random uuid)\n"
		   "\t-n <n>        number of calls per user (default: endless)\n"
		   "\t-d <sec>      call duration (default: until killed)\n"
		   "\t-t <ms>       timeout (currently unused)\n"
		   "\t-v            send video\n"
		   "\t-V            stamp and verify received video (implies -v),\n"
		   "\t              exit 1 if any pair got no or wrong video\n"
		   "\t-P <pattern>  video pattern: head (default) or octo\n"
		   "\t-W <n>        request at most n video streams (default 9)\n"
		   "\t-R <ms>       tile switching: rotate the requested video\n"
		   "\t              streams every ms (use with -W < senders)\n"
		   "\t-Y <ms>       start every user's video at the same time,\n"
		   "\t              ms after start (instead of random 2-7s\n"
		   "\t              after joining)\n"
		   "\t-l <file>     log file\n");
}

int main(int argc, char **argv)
{
	uint32_t i;
	int ret = 0;

	sftloader = mem_zalloc(sizeof(*sftloader), sl_destructor);
	
	sftloader->ncalls = INFINITE;
	sftloader->clientno = 0;
	sftloader->logfp = stdout;
	sftloader->max_vstreams = 9;

	for (;;) {
		const int c = getopt(argc, argv, "C:c:d:fhi:l:n:P:R:s:S:t:u:vVW:Y:");

		if (c < 0)
			break;

		switch (c) {
		case 'C':
			str_dup(&sftloader->users_cfg, optarg);
			break;

		case 'h':
			usage();
			return 0;

		case 'c':
			sftloader->clientno = atoi(optarg);
			break;

		case 'd':
			sftloader->duration = atoi(optarg) * 1000;
			break;

		case 'f':
			sftloader->is_federating = true;
			break;

		case 'i':
			str_dup(&sftloader->convid, optarg);
			break;

		case 'l':
			sftloader->logfp = fopen(optarg, "w");
			break;

		case 'n':
			sftloader->ncalls = atoi(optarg);
			break;

		case 'P':
			if (streq(optarg, "octo")) {
				sftloader->video_octo = true;
			}
			else if (streq(optarg, "head")) {
				sftloader->video_octo = false;
			}
			else {
				re_fprintf(stderr, "sftloader: unknown video "
					   "pattern: %s\n", optarg);
				usage();
				return 2;
			}
			break;

		case 'Y':
			sftloader->video_sync_ms = atoi(optarg);
			break;

		case 'R':
			sftloader->rotate_ms = atoi(optarg);
			break;

		case 'W':
			sftloader->max_vstreams = atoi(optarg);
			if (sftloader->max_vstreams < 1) {
				re_fprintf(stderr, "sftloader: -W must be "
					   ">= 1\n");
				return 2;
			}
			break;

		case 's':
			str_dup(&sftloader->sft_url, optarg);
			break;

		case 'S':
		        str_dup(&sftloader->sfts_all, optarg);
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

		case 'V':
			sftloader->use_video = true;
			sftloader->verify_video = true;
			break;

		default:
			break;
		}
	}

	sftloader->running = true;
	wcall_set_log_handler(log_handler, NULL);
	fd_setsize(0);	
	fd_setsize(1048576);	
	wcall_init(0);
	wcall_set_mode(WCALL_MODE_DIRECT);
	wcall_setup_ex(AVS_FLAG_AUDIO_TEST);
	sl_dns_init(&sftloader->dnsc);
	if (!sftloader->convid)
		uuid_v4(&sftloader->convid);

	if (sftloader->use_video) {
		test_capturer_init();
		if (sftloader->video_octo)
			test_capturer_start_dynamic(640,480,15);
		else
			test_capturer_start_head(640,480,15);
	}
	if (sftloader->users_cfg) {
		if (load_users_cfg(sftloader->users_cfg)) {
			list_flush(&sftloader->userl);
			ret = 2;
			goto out;
		}
	}
	else {
		for (i = 0; i < sftloader->nusers; ++i) {
			create_numbered_user(i, sftloader->clientno);
		}
	}
	tmr_init(&sftloader->tmr);
	tmr_init(&sftloader->tmr_vreport);
	if (sftloader->verify_video) {
		if (vid_verify_init()) {
			re_fprintf(stderr, "sftloader: video verify init "
				   "failed\n");
			list_flush(&sftloader->userl);
			ret = 2;
			goto out;
		}
	}
	sftloader->t0 = tmr_jiffies();
	tmr_start(&sftloader->tmr, 500, start_timeout, NULL);
	
	re_main(signal_handler);

 out:
	tmr_cancel(&sftloader->tmr);
	tmr_cancel(&sftloader->tmr_vreport);

	if (sftloader->logfp != stdout)
		fclose(sftloader->logfp);

	if (sftloader->use_video) {
		test_capturer_stop();
	}

	if (sftloader->verify_video) {
		/* Keep an earlier setup error (2) if there was one */
		if (!vid_verify_stop() && ret == 0)
			ret = 1;
	}
	
	wcall_close();

	vid_verify_free();

	mem_deref(sftloader);
	
	
	//tmr_debug();
	//mem_debug();
	
	return ret;
}
