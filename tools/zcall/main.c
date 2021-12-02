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
/* libavs -- command line client
 *
 */

#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <getopt.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#ifdef HAVE_READLINE
#include <wordexp.h>
#endif
#include <pthread.h>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include "cli.h"
#include "options.h"

#include "view.h"

/* Defaults for arguments
 */
#define DEFAULT_STORE_DIR "~/.zcall"
#define DEFAULT_REQUEST_URL "https://prod-nginz-https.wire.com"
#define DEFAULT_NOTIFICATION_URL "https://prod-nginz-ssl.wire.com"
#define DEV_REQUEST_URL "https://staging-nginz-https.zinfra.io"
#define DEV_NOTIFICATION_URL "https://staging-nginz-ssl.zinfra.io"

/* Globals
 */

struct engine *zcall_engine = NULL;
struct store *zcall_store = NULL;
bool zcall_pending_nw_change = false;
bool zcall_audio_cbr = false;
bool zcall_force_audio = false;
bool zcall_auto_answer = false;
bool zcall_video = false;
bool zcall_av_test = false;
bool zcall_noise = false;
static uint64_t ts_start;
bool event_estab = false;
int g_trace = 0;
bool g_ice_privacy = false;
bool g_use_kase = true;
bool g_use_conference = false;
char *zcall_vfile = NULL;
struct list g_sftl = LIST_INIT;

#ifdef HAVE_CRYPTOBOX
struct cryptobox *g_cryptobox;
#endif

WUSER_HANDLE calling3_get_wuser(void);

static void usage(void)
{
	(void)re_fprintf(stderr,
			 "usage: zcall [-h] -e <email> -p <password>"
			 " [-r <url> -n <url>] [-E] [-t] [-t] [-d] [-d]"
			 " [-l <path>] [-s] [-q <path>]"
			 "\n");
	(void)re_fprintf(stderr, "\t-A             Force calls to be audio only\n");
	(void)re_fprintf(stderr, "\t-B             Enable Audio CBR mode\n");
	(void)re_fprintf(stderr, "\t-c <path>      config and cache "
				 		  "directory\n");
	(void)re_fprintf(stderr, "\t-e <email>     Email address\n");
	(void)re_fprintf(stderr, "\t-p <password>  Password\n");
	(void)re_fprintf(stderr, "\t-l <path>      Send debug log to file\n");
	(void)re_fprintf(stderr, "\t-n <url>       Backend notification URL"
				 " (optional)\n");
	(void)re_fprintf(stderr, "\t-r <url>       Backend request URL"
				 " (optional)\n");
	(void)re_fprintf(stderr, "\t-D             Use dev/staging"
				 " environment\n");
	(void)re_fprintf(stderr, "\t-d             Turn on debugging "
			                          "(twice for more)\n");
	(void)re_fprintf(stderr, "\t-t             Enable call tracing\n");
	(void)re_fprintf(stderr, "\t-T             Enable audio test sine-mode\n");
	(void)re_fprintf(stderr, "\t-N             Enable audio test noise-mode\n");
	(void)re_fprintf(stderr, "\t-f             Flush store for the "
						  "user.\n");
	(void)re_fprintf(stderr, "\t-C             Clear cookies for the "
			 "user.\n");
	(void)re_fprintf(stderr, "\t-m             Media system "
						  "(default: voe)\n");
	(void)re_fprintf(stderr, "\t-s             Trace call traffic to "
			                          "stdout\n");
	(void)re_fprintf(stderr, "\t-q <path>      Trace call traffic to "
			                          "<path>\n");
	(void)re_fprintf(stderr, "\t-P <port>      Enable REST on port\n");
	(void)re_fprintf(stderr, "\t-i             Enable ICE privacy mode\n");
	(void)re_fprintf(stderr, "\t-K             Disable KASE crypto\n");
	(void)re_fprintf(stderr, "\t-h             Show options\n");
	(void)re_fprintf(stderr, "\t-G             Force group calls to use "
			 "new conference protocol\n");
	(void)re_fprintf(stderr, "\t-S <sft list>  Set SFT URL "
			 "(in format http(s)://server:port, comma separated)\n");
	(void)re_fprintf(stderr, "\n");
	(void)re_fprintf(stderr, "Config/cache directory defaults to "
				 DEFAULT_STORE_DIR
				 "\n");
	(void)re_fprintf(stderr, "URLs default to regular backend.\n");
	(void)re_fprintf(stderr, "\t-W             Enable video when auto-answering\n");
	(void)re_fprintf(stderr, "\t-Z             Auto-answer incoming "
			 "calls\n");
}


/* Just a command to test whether commands actually sorta work.
 */
static void echo_command_handler(int argc, char *argv[])
{
	int i;

	for (i = 0; i < argc; ++i) {
		output(argv[i]);
		output("\n");
	}
}

struct command echo_command = {
	.command = "test",
	.h = echo_command_handler,
	.help = "echos back your command"
};


/*** 't' ... Show login token
 */

static bool show_token_handler(int ch)
{
	(void) ch;

	output("Login token: %s\n", engine_get_login_token(zcall_engine));
	return true;
}

struct key_stroke token_stroke = {
	.ch = 't',
	.h = show_token_handler,
	.help = "show login token"
};


/*** 'x' ... Network change
 */

static bool nwchange_handler(int ch)
{
	(void) ch;

	output("NETWORK CHANGED!\n");

	zcall_pending_nw_change = true;
	engine_restart(zcall_engine);

	return true;
}


struct key_stroke nwchange_stroke = {
	.ch = 'x',
	.h = nwchange_handler,
	.help = "Trigger network change"
};


#ifdef HAVE_CRYPTOBOX

/*** 'y' ... Cryptobox dump
 */

static bool cryptobox_handler(int ch)
{
	char clientid[64];

	(void) ch;

	if (0 == client_id_load(clientid, sizeof(clientid))) {
		output("my clientid:  %s\n", clientid);
	}
	else {
		output("my clientid:  (Not registered)\n");
	}

	cryptobox_dump(g_cryptobox);

	return true;
}

static struct key_stroke cryptobox_stroke = {
	.ch = 'y',
	.h = cryptobox_handler,
	.help = "Cryptobox dump"
};

#endif


/*** sync command
 */

static void sync_command_handler(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	engine_start_sync(zcall_engine);
}

struct command sync_command = {
	.command = "sync",
	.h = sync_command_handler,
	.help = "start a sync"
};


static void event_estab_handler(bool estab, void *arg)
{
	(void)arg;

	event_estab = estab;

	if (!estab) {
		warning("zcall: event channel down!\n");
		return;
	}

	output("Websocket event channel established.\n");
	if (zcall_pending_nw_change) {

		wcall_network_changed(calling3_get_wuser());

		zcall_pending_nw_change = false;
	}
}


static void sync_done_handler(void *arg)
{
	(void)arg;

	output("*** Sync done (%.3f seconds) ***\n",
	       (double)(tmr_jiffies() - ts_start) / 1000.0);
}

struct engine_lsnr event_lsnr = {
	.estabh = event_estab_handler,
	.syncdoneh = sync_done_handler,
};


static void ready_handler(void *arg)
{
	int err;

	(void) arg;

	info("zcall: engine ready with AVS=" AVS_VERSION "\n");
	
	err = avs_start(engine_get_login_token(zcall_engine));
	if (err) {
		warning("zcall: avs_start failed (%m)\n", err);
	}

	output(">>> Welcome to zcall " AVS_VERSION "."
	       " Try 'h' or '?' for help.\n");

	conv_ready();
	calling3_init();
}


static void error_handler(int err, void *arg)
{
	error("Fatal error: %m. Shutting down now.\n", err);
}


static void shutdown_handler(void *arg)
{
	(void) arg;

	info("Shutting down now.\n");
	re_cancel();
}


static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		input_close();
		warning("Aborted.\n");
		exit(0);
	}

	term = true;

	warning("Terminating ...\n");

	if (zcall_engine)
		engine_shutdown(zcall_engine);
	else
		re_cancel();
	input_shutdown();
}


static int create_store(struct store **stp, const char *store_dir,
			const char *email)
{
#ifdef HAVE_CRYPTOBOX
	char cboxpath[512];
#endif

	int err=0;

#ifdef HAVE_READLINE
	wordexp_t we;

	err = wordexp(store_dir, &we, 0);
	if (err)
		return err;

	store_dir = we.we_wordv[0];
#endif

	err = store_alloc(stp, store_dir);
	if (err)
		goto out;

#ifdef HAVE_CRYPTOBOX
	re_snprintf(cboxpath, sizeof(cboxpath), "%s/%s",
		    store_dir, email);

	err = store_mkdirf(0700, cboxpath);
	if (err)
		goto out;

	/* do it after STORE is created */
	err = cryptobox_alloc(&g_cryptobox, cboxpath);
	if (err) {
		re_fprintf(stderr, "Failed to init cryptobox: %m\n", err);
		goto out;
	}
#endif

 out:
#ifdef HAVE_READLINE
	wordfree(&we);
#endif
	return err;
}

static void parse_sfts(char *sftstr)
{
	char *tok;

	while ((tok = strtok_r(sftstr, ",", &sftstr))) {
		econn_stringlist_append(&g_sftl, tok);
	}

}

int main(int argc, char *argv[])
{
	const char *email = NULL;
	const char *password = NULL;
	const char *log_file = NULL;
	const char *trace_file = NULL;
	bool trace_stdout = false;
	const char *request_uri = DEFAULT_REQUEST_URL;
	const char *notification_uri = DEFAULT_NOTIFICATION_URL;
	const char *store_dir = DEFAULT_STORE_DIR;
	enum log_level level = LOG_LEVEL_INFO;
	bool flush = false;
	char msys[64] = "voe";
	char log_file_name[100];
	int err = 0;
	bool clear_cookies = false;
	uint16_t rest_lport = 0;
	bool rest_lport_set = false;
	bool do_login = false;

#ifndef HAVE_READLINE
	char store_dir_buf[512];
	char path[256];

	err = fs_gethome(path, sizeof(path));
	if (err) {
		re_fprintf(stderr, "could not get path to $HOME\n");
		return 2;
	}
	re_snprintf(store_dir_buf, sizeof(store_dir_buf),
		    "%s/.zcall", path);
	store_dir = store_dir_buf;
#endif

	for (;;) {
		const int c = getopt(argc, argv,
				     "ABc:CdDe:fGhiIKl:m:n:Np:P:q:r:sS:tTV:WZ");
		if (c < 0)
			break;

		switch (c) {

		case 'A':
			zcall_force_audio = true;
			break;

		case 'B':
			zcall_audio_cbr = true;
			break;
                
		case 'c':
			store_dir = optarg;
			break;

		case 'C':
			re_printf("clearing cookie.\n");
			clear_cookies = true;
			break;

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

		case 'f':
			flush = true;
			break;

		case 'G':
			g_use_conference = true;
			break;

		case 'i':
			re_printf("enable ICE privacy\n");
			g_ice_privacy = true;
			break;

		case 'K':
			g_use_kase = false;
			break;

		case 'l':
			log_file = optarg;
			break;

		case 'm':
			str_ncpy(msys, optarg, sizeof(msys));
			break;

		case 'n':
			notification_uri = optarg;
			break;

		case 'p':
			password = optarg;
			break;

		case 'P':
			rest_lport = atoi(optarg);
			rest_lport_set = true;
			break;

		case 'q':
			trace_file = optarg;
			break;

		case 'r':
			request_uri = optarg;
			break;

		case 's':
			trace_stdout = true;
			break;

		case 'S':
			parse_sfts(optarg);
			break;

		case 't':
			++g_trace;
			break;

		case 'N':
			zcall_av_test = true;
			zcall_noise = true;
			break;

		case 'T':
			zcall_av_test = true;
			break;
                
		case 'V':
			str_dup(&zcall_vfile, optarg);
			break;


		case 'W':
			zcall_video = true;
			break;

		case 'Z':
			zcall_auto_answer = true;
			break;

		case '?':
			err = EINVAL;
			/* fall through */
		case 'h':
			usage();
			return err;
		}
	}

	if (log_file == NULL) {
		char  buf[256];
		time_t     now = time(0);
		struct tm  tstruct;

		tstruct = *localtime(&now);
		strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
		buf[13] = '-';
		buf[16] = '-';

		re_snprintf(log_file_name, sizeof(log_file_name),
			    "avs_log_%s.log", buf);

		log_file = log_file_name;
	}

	if (streq(log_file, "disabled")) {
		log_file = NULL;
	}

	if (level == LOG_LEVEL_INFO)
		output("Log level set to INFO.\n");
	else if (level == LOG_LEVEL_DEBUG)
		output("Log level set to DEBUG.\n");
	log_set_min_level(level);

	info("libavs version %s\n", wcall_library_version());

	do_login = email != NULL;
	if (email == NULL) {
		(void)re_fprintf(stderr, "Missing email.\n");
		//err = EINVAL;
		//goto out;
	}

	/* XXX We should probably treat the password more carefully.
	 */
	if (do_login && password == NULL) {
		//password = getpass("Password: ");

		static char buf[128];

		printf("Password: ");
		fgets(buf, sizeof(buf), stdin);
		buf[strlen(buf) - 1] = 0;

		password = buf;

		if (!str_isset(password)) {
			(void)re_fprintf(stderr, "Missing password.\n");
			err = EINVAL;
			goto out;
		}
	}

	if (do_login) {
		info("Connecting to %s.\n", request_uri);
	}

	err = libre_init();
	if (err) {
		(void)re_fprintf(stderr, "libre init failed: %m\n", err);
		goto out;
	}

	info("libre inited (%s)\n", sys_libre_version_get());
    
	err = avs_init(AVS_FLAG_EXPERIMENTAL
		       | (zcall_av_test ? AVS_FLAG_AUDIO_TEST : 0));
	if (err) {
		(void)re_fprintf(stderr, "avs init failed: %m\n", err);
		goto out;
	}

	sys_coredump_set(true);

	struct sa laddr;
	int r;

	r = net_default_source_addr_get(AF_INET, &laddr);
	info("ipv4: %J (%m)\n", &laddr, r);
	r = net_default_source_addr_get(AF_INET6, &laddr);
	info("ipv6: %J (%m)\n", &laddr, r);

	ts_start = tmr_jiffies();

	err = engine_init(msys);
	if (err) {
		(void)re_fprintf(stderr, "Engine init failed: %m\n", err);
		goto out;
	}

	err = msystem_enable_datachannel(flowmgr_msystem(), true);
	if (err) {
		warning("could not enable data-channel (%m)\n", err);
		goto out;
	}

	if (do_login) {
		err = create_store(&zcall_store, store_dir, email);
		if (err) {
			(void)re_fprintf(stderr, "Failed to init store: %m\n",
					 err);
			goto out;
		}
	}

	err = input_init();
	if (err)
		goto out;

	err = options_init();
	if (err)
		goto out;

	err = clilog_init(level, log_file);
	if (err) {
		(void)re_fprintf(stderr, "Log init failed: %m\n", err);
		goto out;
	}

	if (do_login) {
		err = engine_alloc(&zcall_engine, request_uri, notification_uri,
				   email, password, zcall_store, flush,
				   clear_cookies,
				   "zcall/" AVS_VERSION,
				   ready_handler, error_handler,
				   shutdown_handler,
				   NULL);
		if (err) {
			(void)re_fprintf(stderr, "Engine alloc failed: %m\n",
					 err);
			goto out;
		}
	}

	if (trace_stdout || trace_file)
		engine_set_trace(zcall_engine, trace_file, trace_stdout);

	engine_lsnr_register(zcall_engine, &event_lsnr);

	err = user_init()
	    | conv_init()
	    | client_init();
	if (err) {
		goto out;
	}

	//err = pairing_init();
	//if (err)
	//	goto out;

	if (rest_lport_set) {
		err = restsrv_init(rest_lport);
		if (err) {
			warning("failed to init REST server (%m)\n", err);
			goto out;
		}
	}

	register_command(&echo_command);
	register_command(&sync_command);
	register_key_stroke(&token_stroke);
	register_key_stroke(&nwchange_stroke);

#ifdef HAVE_CRYPTOBOX
	register_key_stroke(&cryptobox_stroke);
#endif

	wcall_set_video_handlers(render_handler, size_handler, NULL);

	view_init(zcall_av_test);
	runloop_start();

	re_main(signal_handler);

 out:
	runloop_stop();
	view_close();
	//pairing_close();
	client_close();
	conv_close();
	user_close();
	mem_deref(zcall_engine);
	clilog_close();
	options_close();
	input_close();
#ifdef HAVE_CRYPTOBOX
	g_cryptobox = mem_deref(g_cryptobox);
#endif
	mem_deref(zcall_store);

	engine_close();
	restsrv_close();

	avs_close();
	libre_close();

	mem_deref(zcall_vfile);
	list_flush(&g_sftl);
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
