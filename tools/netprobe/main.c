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
/* libavs -- Network Prober
 */

#include <ctype.h>
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <re.h>
#include <avs.h>


/* Defaults for arguments
 */
#define DEFAULT_REQUEST_URL "https://prod-nginz-https.wire.com"
#define DEFAULT_NOTIFICATION_URL "https://prod-nginz-ssl.wire.com"
#define DEV_REQUEST_URL "https://staging-nginz-https.zinfra.io"
#define DEV_NOTIFICATION_URL "https://staging-nginz-ssl.zinfra.io"


/* Globals
 */

static struct engine *engine = NULL;
static struct config *g_config = NULL;
static struct probe_entry {
	struct netprobe *np;
	struct sa turn_srv;
	bool secure;
	int proto;
} netprobev[32];
static size_t netprobec;
static size_t netprobec_ok;
static const char *turn_uri;

struct lookup_entry {
	struct zapi_ice_server turn;
	char *host;
	int port;
	int proto;
	bool secure;
	uint64_t ts;
	
	struct le le;
};


static int start_netprobe(const struct sa *turn_srv,
			  int proto, bool secure,
			  const char *username, const char *password);

static void dns_handler(int dns_err, const struct sa *srv, void *arg)
{
	struct lookup_entry *lent = arg;
	struct sa turn_srv;

	re_printf("DNS lookup for: %s err=%d\n", lent->host, dns_err);
	if (dns_err)
		goto out;

	sa_cpy(&turn_srv, srv);
	sa_set_port(&turn_srv, lent->port);

	re_printf("DNS lookup success: %s -> %J(proto=%s secure=%d)\n",
		  lent->host, &turn_srv, net_proto2name(lent->proto),
		  lent->secure);
	
	start_netprobe(&turn_srv, lent->proto, lent->secure,
		       lent->turn.username, lent->turn.credential);
 out:
	mem_deref(lent);
}


static void lent_destructor(void *arg)
{
	struct lookup_entry *lent = arg;

	mem_deref(lent->host);
}


static int turn_dns_lookup(struct zapi_ice_server *turn,
			   struct stun_uri *uri)
{
	struct lookup_entry *lent;
	int err = 0;

	lent = mem_zalloc(sizeof(*lent), lent_destructor);
	if (!lent)
		return ENOMEM;

	lent->turn = *turn;
	lent->ts = tmr_jiffies();
	lent->proto = uri->proto;
	lent->secure = uri->secure;
	lent->port = uri->port;
	err = str_dup(&lent->host, uri->host);
	if (err)
		goto out;

	re_printf("dns_lookup for: %s:%d\n", lent->host, lent->port);
	
	err = dns_lookup(lent->host, dns_handler, lent);
	if (err) {
		warning("dns_lookup: failed: %m\n", err);
		goto out;
	}
 out:
	if (err)
		mem_deref(lent);

	return err;
}


static void cfg_resp_handler(int err, const struct http_msg *msg,
			     struct mbuf *mb, struct json_object *jobj,
			     void *arg)
{
	char *json_str = NULL;
	struct zapi_ice_server *servers;
	size_t nservers = 0;
	size_t i;

	re_printf("config ready! err=%d\n");
	if (err == ECONNABORTED)
		goto out;

	if (!err && jobj) {
		err = jzon_encode(&json_str, jobj);
		if (err)
			goto out;
	}

	re_printf("CONFIG: %s\n", json_str);

	config_update(g_config, err, json_str, str_len(json_str));
	mem_deref(json_str);
	
	servers = config_get_iceservers(g_config, &nservers);
	re_printf("Probing: %d servers\n", nservers);

	for (i = 0; i < nservers; ++i) {
		struct zapi_ice_server *turn = &servers[i];
		struct stun_uri uri;

		err = stun_uri_decode(&uri, turn->url);
		if (err)
			err = turn_dns_lookup(turn, &uri);
		else {
			start_netprobe(&uri.addr, uri.proto, uri.secure,
				       turn->username, turn->credential);
		}
	}

	
 out:
	if (err)
		error("config request failed: %m\n", err);
}

static int config_req_handler(void *arg)
{
	printf("requesing config\n");
	return rest_request(NULL, engine_get_restcli(engine), 0,
			    "GET", cfg_resp_handler, NULL,
			    "/calls/config/v2", NULL);
	
}




static void ready_handler(void *arg)
{
	struct stun_uri uri;
	int err;

	dns_init(NULL);
	
	(void)uri;
	(void)err;
	
	re_printf("engine ready.\n");

	config_alloc(&g_config, config_req_handler, NULL, NULL);
	config_start(g_config);

#if 0
	err = stun_uri_decode(&uri, turn_uri);
	if (err) {
		warning("netprobe: ready_handler: failed to parse URI: %s\n",
			turn_uri);
		return;
	}

	start_netprobe(&uri.addr, uri.proto, uri.secure, "", "");
#endif
}


static void error_handler(int err, void *arg)
{
	error("Engine just broken: %m.\n", err);
}


static void engine_shutdown_handler(void *arg)
{
	(void) arg;

	re_printf("The engine shutted down.\n");
	re_cancel();
}


static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		warning("Aborted.\n");
		exit(0);
	}

	term = true;

	warning("Terminating ...\n");

	engine_shutdown(engine);
}


static void netprobe_handler(int err, const struct netprobe_result *result,
			     void *arg)
{
	struct probe_entry *np;
	size_t ix = (size_t)arg;

	if (err) {
		warning("netprobe failed (%m)\n", err);
		goto out;
	}

	np = &netprobev[ix];

	re_printf("Network Probe results for %s TURN%s-server at %J\n",
		  net_proto2name(np->proto),
		  np->secure ? "S" : "", &np->turn_srv);
	re_printf("    Average RTT:   %.1f milliseconds\n",
		  result->rtt_avg / 1000.0);
	re_printf("    transmitted:   %u packets\n", result->n_pkt_sent);
	re_printf("    received:      %u packets\n", result->n_pkt_recv);
	re_printf("\n");

 out:
	netprobec_ok++;

	if (netprobec_ok >= netprobec) {
		info("netprobing done. shutting down..\n");
		engine_shutdown(engine);
	}
}


static int start_netprobe(const struct sa *turn_srv,
			  int proto, bool secure,
			  const char *username, const char *password)
{
	int err;

	re_printf("starting netprobe with TURN%s-server %J"
		  " (proto=%s) ..\n",
		  secure ? "S" : "", turn_srv,
		  net_proto2name(proto));

#define PACKET_COUNT 50
#define PACKET_INTERVAL 20

	if (netprobec >= ARRAY_SIZE(netprobev)) {
		warning("reached maximum %zu netprobes\n", netprobec);
		return 0;
	}

	netprobev[netprobec].turn_srv = *turn_srv;
	netprobev[netprobec].secure = secure;
	netprobev[netprobec].proto = proto;
	

	err = netprobe_alloc(&netprobev[netprobec].np,
			     turn_srv, proto, secure,
			     username, password,
			     PACKET_COUNT, PACKET_INTERVAL,
			     netprobe_handler, (void *)netprobec);
	if (err) {
		warning("could not create netprobe (%m)\n", err);
		goto out;
	}

	netprobec++;

 out:
	return err;
}


static void usage(void)
{
	(void)re_fprintf(stderr,
			 "usage: netprobe [-dh] -e <email> -p <password>"
			 " [-r <url> -n <url>] [-t] [-t] [-d] [-d]"
			 " [-l <path>]"
			 "\n");
	(void)re_fprintf(stderr, "\t-c <path>      config and cache "
				 		  "directory\n");
	(void)re_fprintf(stderr, "\t-d             Turn on debugging "
			                          "(twice for more)\n");
	(void)re_fprintf(stderr, "\t-e <email>     Email address\n");
	(void)re_fprintf(stderr, "\t-p <password>  Password\n");
	(void)re_fprintf(stderr, "\t-l <path>      Send debug log to file\n");
	(void)re_fprintf(stderr, "\t-n <url>       Backend notification URL"
				 " (optional)\n");
	(void)re_fprintf(stderr, "\t-r <url>       Backend request URL"
				 " (optional)\n");
	(void)re_fprintf(stderr, "\t-D             Use dev environment\n");
	(void)re_fprintf(stderr, "\t-u <TURN>      Force a TURN uri\n");

	(void)re_fprintf(stderr, "\t-h             Show options\n");
	(void)re_fprintf(stderr, "\n");
	(void)re_fprintf(stderr, "URLs default to regular backend.\n");
}


int main(int argc, char *argv[])
{
	const char *email = NULL;
	const char *password = NULL;
	const char *request_uri = DEFAULT_REQUEST_URL;
	const char *notification_uri = DEFAULT_NOTIFICATION_URL;
	enum log_level level = LOG_LEVEL_WARN;
	size_t i;

	int err = 0;

	for (;;) {
		const int c = getopt(argc, argv, "de:l:n:p:r:tDu:");
		if (c < 0)
			break;

		switch (c) {

		case 'd':
			if (level == LOG_LEVEL_INFO)
				level = LOG_LEVEL_DEBUG;
			else
				level = LOG_LEVEL_INFO;
			break;

		case 'D':
			request_uri = DEV_REQUEST_URL;
			notification_uri = DEV_NOTIFICATION_URL;
			break;

		case 'e':
			email = optarg;
			break;

		case 'n':
			notification_uri = optarg;
			break;

		case 'p':
			password = optarg;
			break;

		case 'r':
			request_uri = optarg;
			break;

		case 'u':
			turn_uri = optarg;
			break;

		case '?':
			err = EINVAL;
			/* fall through */
		case 'h':
			usage();
			return err;
		}
	}

	log_set_min_level(level);

	if (email == NULL) {
		(void)re_fprintf(stderr, "Missing email.\n");
		err = EINVAL;
		goto out;
	}
	if (password == NULL) {
		(void)re_fprintf(stderr, "Missing password.\n");
		err = EINVAL;
		goto out;
	}

	err = libre_init();
	if (err) {
		(void)re_fprintf(stderr, "libre init failed: %m\n", err);
		goto out;
	}

	err = avs_init(AVS_FLAG_EXPERIMENTAL);
	if (err) {
		(void)re_fprintf(stderr, "avs init failed: %m\n", err);
		goto out;
	}

	sys_coredump_set(true);

	err = engine_init("audummy");
	if (err) {
		(void)re_fprintf(stderr, "engine init failed: %m\n", err);
		goto out;
	}

	err = engine_alloc(&engine, request_uri, notification_uri, email,
			   password, NULL, false, false,
			   "netprobe/" AVS_VERSION,
			   ready_handler, error_handler,
			   engine_shutdown_handler, 0);
	if (err) {
		(void)re_fprintf(stderr, "Engine init failed: %m\n", err);
		goto out;
	}

	err = re_main(signal_handler);

 out:
	for (i=0; i<netprobec; i++)
		mem_deref(netprobev[i].np);

	mem_deref(g_config);
	mem_deref(engine);

	engine_close();
	dns_close();

	libre_close();

	/* check for memory leaks */
	mem_debug();
	tmr_debug();

	if (err) {
		return 1;
	}
	else {
		return 0;
	}
}
