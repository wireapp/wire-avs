#include <stdlib.h>
#include <unistd.h>
#include <re.h>
#include <avs_wcall.h>

//#define SIP_AOR "sip:wire@192.168.2.240:5061;regint=0"
//#define SIP_AOR "sip:wire@172.20.10.8:5061;regint=0"
//#define SIP_AOR "sip:wire@192.168.20.157:5061;regint=0"
#define SIP_AOR "sip:wire@127.0.0.1:5061;regint=0"

#define CONVID "siptest"

struct client {
	WUSER_HANDLE wuser;

	char *userid;
	char *clientid;

	bool ready;
};

static struct {
	bool running;
	char *config_path;
	uint64_t answer_timeout;
	uint64_t call_timeout;

	/* timers */
	struct tmr tmr_call;
	struct tmr tmr_answer;

	char *convid;
	
	struct {
		struct client pstn;
		struct client wire;
	} clients;	
} g_st = {
	.running = false,
	.config_path = NULL,
	.answer_timeout = 1000,
	.call_timeout = 100,
	.convid = CONVID,
	.clients = {
		.pstn = {
			.wuser = WUSER_INVALID_HANDLE,
			.userid = "pstn",
			.clientid = "0000",
			.ready = false,
		},
		.wire = {
			.wuser = WUSER_INVALID_HANDLE,
			.userid = "wire",
			.clientid = "0000",
			.ready = false,
		},
	}
};

static void timeout_call_handler(void *arg)
{
	struct client *cli = &g_st.clients.wire;
	(void)arg;

	wcall_start(cli->wuser, g_st.convid,
		    WCALL_CALL_TYPE_NORMAL,
		    WCALL_CONV_TYPE_ONEONONE,
		    1,
		    0);
}

static void ready_handler(int version, void *arg)
{
	struct client *cli = arg;
	
	printf("ready_handler: client: %p is ready\n", cli);

	cli->ready = true;

	if (g_st.clients.pstn.ready && g_st.clients.wire.ready) {
		tmr_start(&g_st.tmr_call, g_st.call_timeout,
			  timeout_call_handler, NULL);
	}
}


static int config_req_handler(WUSER_HANDLE wuser, void *arg)
{
	struct client *cli = arg;

	char *empty_cfg = "{}";

	printf("config_req_handler: client=%p\n", cli);
	
	wcall_config_update(wuser, 0, empty_cfg);

	return 0;
}

static int send_handler(void *ctx, const char *convid,
			const char *userid_self, const char *clientid_self,
			const char *userid_dest, const char *clientid_dest,
			const uint8_t *data, size_t len, int transient,
			int my_clients_only,
			void *arg)
{
	struct client *cli = arg;
	struct client *other = NULL;
	
	if (cli == &g_st.clients.pstn) {
		other = &g_st.clients.wire;
	}
	else if (cli == &g_st.clients.wire) {
		other = &g_st.clients.pstn;
	}
	
	wcall_recv_msg(other->wuser, data, len,
		       0,
		       0,
		       convid,
		       userid_self,
		       clientid_self,
		       WCALL_CONV_TYPE_ONEONONE,
		       false);

	/* Reply with success */
	wcall_resp(cli->wuser, 200, "", ctx);

	return 0;
}

static void timeout_answer_handler(void *arg)
{
	struct client *cli = arg;
	
	wcall_answer(cli->wuser, g_st.convid, WCALL_CALL_TYPE_NORMAL, 0);
	
}

static void incoming_handler(const char *convid, uint32_t msg_time,
			     const char *userid, const char *clientid,
			     int video_call /*bool*/,
			     int should_ring /*bool*/,
			     int conv_type, /*WCALL_CONV_TYPE...*/
			     void *arg)
{
	struct client *cli = arg;

	printf("incoming_handler: client=%p userid=%s\n", cli, cli->userid);

	if (cli != &g_st.clients.pstn) {
		printf("incoming_handler: ignoring on non-SIP side\n");
		return;
	}

	tmr_start(&g_st.tmr_answer,
		  g_st.answer_timeout,
		  timeout_answer_handler,
		  cli);
}


static void estab_handler(const char *convid,
			  const char *userid, const char *clientid, void *arg)
{
	struct client *cli = arg;

	(void)userid;
	(void)clientid;
	
	printf("estab_handler: client=%p convid=%s\n", cli, convid);
}


static void close_handler(int reason, const char *convid, uint32_t msg_time,
			  const char *userid, const char *clientid, void *arg)
{
	struct client *cli = arg;
	struct client *other = NULL;

	if (cli == &g_st.clients.pstn)
		other = &g_st.clients.wire;
	else if (cli == &g_st.clients.wire)
		other = &g_st.clients.pstn;

	if (!other) {
		fprintf(stderr, "close_handler: invalid client: %p\n", cli);
		return;
	}

	wcall_end(other->wuser, g_st.convid);
}


int main(int argc, char **argv)
{
	WUSER_HANDLE wuser;
	
	for (;;) {
		const int c = getopt(argc, argv, "a:f:Tt:");
		if (c < 0)
			break;

		switch (c) {
		case 'a':
			g_st.answer_timeout = atoi(optarg) * 1000;		
			break;

		case 'f':
			str_dup(&g_st.config_path, optarg);
			break;
			
		case 'T':
			// Set AVS into test mode
			break;

		case 't':
			g_st.call_timeout = atoi(optarg) * 1000;
			break;
			
		}

	}

	tmr_init(&g_st.tmr_answer);
	
	if (!g_st.config_path) {
		fprintf(stderr, "%s: missing config path\n", argv[0]);
		return 2;
	}
	
	wcall_run();

	g_st.running = true;
	
	/* PSTN client */
	wuser = wcall_create_ex(g_st.clients.pstn.userid,
				g_st.clients.pstn.clientid,
				0,
				"pstn",
				ready_handler,
				send_handler,
				NULL,
				incoming_handler,
				NULL,
				NULL,
				estab_handler,
				close_handler,
				NULL,
				config_req_handler,
				NULL,
				NULL,
				&g_st.clients.pstn);
	
	wcall_sip_init(wuser, g_st.config_path);
	wcall_sip_create(wuser, CONVID, SIP_AOR);

	g_st.clients.pstn.wuser = wuser;

	/* Wire client */
	wuser = wcall_create_ex(g_st.clients.wire.userid,
				g_st.clients.wire.clientid,
				0,
				"voe",
				ready_handler,
				send_handler,
				NULL,
				incoming_handler,
				NULL,
				NULL,
				estab_handler,
				close_handler,
				NULL,
				config_req_handler,
				NULL,
				NULL,
				&g_st.clients.wire);
	g_st.clients.wire.wuser = wuser;
	
	while(g_st.running) {
		usleep(100 * 1000);
	}

	wcall_sip_destroy(g_st.clients.pstn.wuser, CONVID, SIP_AOR);
	wcall_sip_close(g_st.clients.pstn.wuser);

	sleep(1);

	wcall_destroy(g_st.clients.pstn.wuser);
	
	return 0;
}
