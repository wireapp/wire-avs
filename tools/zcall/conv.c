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
/* zclient-call -- command line version
 *
 * Conversation handling
 */

#include <strings.h>
#include <re.h>
#include <avs.h>
#include <avs_wcall.h>
#include "cli.h"
#include "view.h"
#include "options.h"

// Batch retries, 5: 1+2+3+4+5 = 15 seconds max
#define MAX_BATCH_RETRIES 5

/* We are stuffing a struct le into the user arg of the conversation
 * and thus link our conversation list.
 */

WUSER_HANDLE calling3_get_wuser(void);

struct batch_call {
	unsigned call_num;

	bool started;
	bool estab;
	bool ended;

	uint64_t ts_start;
	uint64_t ts_estab;
	uint64_t ts_ended;

	//struct mediaflow_stats mf_stats;
};


static struct {
	struct list convl;
	struct engine_conv *curr;
	struct tmr tmr_autoanswer;
	bool auto_answer;
	bool archived;
	int video_state;

	struct tmr tmr_batch;
	struct batch_call *batchv;
	size_t batchc;
	size_t batchi;
	unsigned batch_dur;
	uint32_t batch_retry;

} conv_data = {
	.convl = LIST_INIT,
	.curr = NULL,
	.archived = false
};


/* prototypes */
static bool accept_key_handler(int ch);
static void vstart_cmd_handler(int argc, char *argv[]);
static void vstop_cmd_handler (int argc, char *argv[]);
static void vpause_cmd_handler(int argc, char *argv[]);
void test_view_capture_next_frame(void);


static void conv_le_destructor(void *arg)
{
	struct le *le = arg;

	list_unlink(le);
}


static void conv_added_handler(struct engine_conv *conv, void *arg)
{
	struct le *le;

	if (conv->arg) {
		info("Conversation %s already added. (%H)\n",
			conv->id, engine_print_conv_name, conv);
		return;
	}

	le = mem_zalloc(sizeof(*le), conv_le_destructor);
	if (!le)
		return;

	conv->arg = le;

	list_append(&conv_data.convl, le, conv);
	if (!conv_data.curr && (!conv->archived || conv_data.archived))
		conv_data.curr = conv;

	debug("Conversation '%H' added.\n", engine_print_conv_name, conv);
}


void handle_calling_event(struct engine_conv *conv,
			  struct engine_user *from_user,
			  const char *from_clientid,
			  const struct ztime *timestamp,
			  const char *data)
{

	debug("Calling3 event: %p(%d)\n", data, strlen(data));

	calling3_recv_msg(conv->id, from_user->id, from_clientid,
			  timestamp, data);
}


#ifdef HAVE_CRYPTOBOX
static void otr_add_message_handler(struct engine_conv *conv,
				    struct engine_user *from,
				    const struct ztime *timestamp,
				    const uint8_t *cipher, size_t cipher_len,
				    const char *sender, const char *recipient,
				    void *arg)
{
	struct session *sess;
	char clientid[64];
	uint8_t *plain = NULL;
	size_t plain_len = 8192;
	int err;

	err = client_id_load(clientid, sizeof(clientid));
	if (err) {
		debug("my clientid not set -- dropping incoming OTR msg\n");
		return;
	}

	info("[%H] OTR from \"%s\":\n",
	       engine_print_conv_name, conv, from->display_name);

	/* check local clientid, disregard if no match */
	if (0 != str_casecmp(recipient, clientid)) {
		debug("conv: discard otr message to other recipient (%s)\n",
		       recipient);
		return;
	}

	/* forward the encrypted OTR-message to "cryptobox" */
	plain = mem_alloc(plain_len, NULL);

	sess = cryptobox_session_find(g_cryptobox, from->id, sender, recipient);
	if (sess) {

		err = cryptobox_session_decrypt(g_cryptobox, sess,
						plain, &plain_len,
						cipher, cipher_len);
		if (err) {
			warning("decrypt error (%m)\n", err);
			goto out;
		}
	}
	else {
		output("no session found for '%s.%s' -- create new\n",
		       from->id, sender);

		err = cryptobox_session_add_recv(g_cryptobox, from->id, sender, recipient,
						 plain, &plain_len,
						 cipher, cipher_len);
		if (err) {
			warning("session_add error (%m)\n", err);
			goto out;
		}
	}

	if (plain && plain_len) {
#ifdef HAVE_PROTOBUF

		struct protobuf_msg *msg = NULL;
		GenericMessage *gm;
		Text *text;
		Calling *calling;

		err = protobuf_decode(&msg, plain, plain_len);
		if (err) {
			warning("failed to decode protobuf (%zu bytes)\n",
				plain_len);
		}
		info("plain=%p(%zu)\n", plain, plain_len);

		gm = msg->gm;

		switch (gm->content_case) {

		case GENERIC_MESSAGE__CONTENT_TEXT:
			text = gm->text;

			output("\033[1;32m"
			       "%s >>>    \"%s\"\n"
			       "\x1b[;m",
			       from->display_name,
			       text->content);
			break;

		case GENERIC_MESSAGE__CONTENT_IMAGE:
			output("receive image\n");
			break;

		case GENERIC_MESSAGE__CONTENT_KNOCK:
			output("\033[10;1000]\033[11;1000]\a"
			       "\xE2\x98\x8E "
			       "\x1b[36mPing!\x1b[;m"
			       "\xE2\x8F\xB0"
			       "\n");
			break;

		case GENERIC_MESSAGE__CONTENT_LAST_READ:
			break;

		case GENERIC_MESSAGE__CONTENT_CALLING:
			calling = gm->calling;
			handle_calling_event(conv, from, sender,
					     timestamp, calling->content);
			break;

		default:
			info("generic-message: content type %d not handled\n",
			     gm->content_case);
			break;
		}

		mem_deref(msg);

#else
		warning("I am compiled without HAVE_PROTOBUF\n");
#endif

	}

 out:
	mem_deref(plain);
}
#endif


static void tmr_autoanswer_handler(void *arg)
{
	accept_key_handler(0);
}


/*** 'r' ... open/close archive
 */

static bool archive_key_handler(int ch)
{
	if (conv_data.archived) {
		conv_data.archived = false;
		output("Hiding archive.\n");
	}
	else {
		conv_data.archived = true;
		output("Showing archive.\n");
	}

	return true;
}


static struct key_stroke archive_stroke = {
	.ch = 'r',
	.h = archive_key_handler,
	.help = "open/close archive"
};


/*** 'l' ... List conversations
 */

static void print_conv_list_entry(struct engine_conv *conv)
{
	if (conv == conv_data.curr)
		output("-> ");
	else
		output("   ");

	switch (conv->type) {
	case ENGINE_CONV_SELF:
		output("SL");
		break;
	case ENGINE_CONV_CONNECT:
		output("RQ");
		break;
	default:
		output("  ");
	}
	if (conv->unread)
		output("o ");
	else
		output("  ");

	output(" '%H'\n", engine_print_conv_name, conv);
}


static bool list_key_handler(int ch)
{
	struct le *le;

	(void) ch;

	output("Conversation list");
	if (conv_data.archived)
		output(" with archive");
	output(" (%u entries)", list_count(&conv_data.convl));
	output(":\n");
	LIST_FOREACH(&conv_data.convl, le) {
		struct engine_conv *conv = le->data;

		if (conv->archived && !conv_data.archived)
			continue;

		print_conv_list_entry(conv);
	}
	output("EOL\n");

	return true;
}


static struct key_stroke list_stroke = {
	.ch = 'l',
	.h = list_key_handler,
	.help = "list conversations"
};


/*** 'i' ... Conversation info
 */

static bool info_key_handler(int ch)
{
	struct le *le;
	struct wcall_members *members = NULL;
	size_t p;

	(void) ch;

	if (!conv_data.curr)
		return true;

	output("%H\n", engine_print_conv_name, conv_data.curr);
	output("ID:   %s\n", conv_data.curr->id);
	output("Type: ");
	switch (conv_data.curr->type) {
	case ENGINE_CONV_REGULAR:
		output("group conversation\n");
		break;
	case ENGINE_CONV_SELF:
		output("selfconversation\n");
		break;
	case ENGINE_CONV_ONE:
		output("one-on-one conversation\n");
		break;
	case ENGINE_CONV_CONNECT:
		output("connect request pending\n");
		break;
	default:
		output("unknown\n");
	}
	output("Members:\n");
	LIST_FOREACH(&conv_data.curr->memberl, le) {
		struct engine_conv_member *mbr = le->data;

		output("  %s %s", mbr->user->id,
			mbr->user->display_name);
		if (!mbr->active)
			output(" [left]");
		if (conv_data.curr->type == ENGINE_CONV_REGULAR) {
			members = wcall_get_members(calling3_get_wuser(), conv_data.curr->id);
			if (members) {
				for (p = 0; p < members->membc; p++) {
					if (strcmp(members->membv[p].userid, mbr->user->id) == 0) {
						output(" [in call]");
						break;
					}
				}
				wcall_free_members(members);
			}
		}

		output("\n");
	}
	if (!conv_data.curr->active)
		output("You have left the conversation.\n");
	if (conv_data.curr->archived)
		output("You have archived the conversation.\n");
	if (conv_data.curr->muted)
		output("You have muted the conversation.\n");
	output("Call state: ");

	output("%s\n", wcall_state_name(wcall_get_state(calling3_get_wuser(), conv_data.curr->id)));

	output("Last event: %s\n", conv_data.curr->last_event);
	output("Last read:  %s\n", conv_data.curr->last_read);

	return true;
}


static struct key_stroke info_stroke = {
	.ch = 'i',
	.h = info_key_handler,
	.help = "conversation info"
};


/*** 'j' ... Switch to next conversation in list
 */

void set_curr_conv(struct engine_conv *conv)
{
	struct sobject *so;
	int err;

	if (!conv)
		goto out;

	conv_data.curr = conv;

	if (!zcall_store)
		goto out;

	err = store_user_open(&so, zcall_store, "zcall", "conv", "wb");
	if (err)
		goto out;

	sobject_write_lenstr(so, conv->id);
	sobject_close(so);
	mem_deref(so);

 out:
	output("Current conversation: %H\n",
		engine_print_conv_name, conv_data.curr);
}

static struct engine_conv *get_next_conv(struct engine_conv *conv,
					 bool unread)
{
	struct le *le;

	if (!conv)
		conv = conv_data.curr;

	le = conv->arg;
	if (!le->next)
		return NULL;

	conv = le->next->data;
	if ((conv->archived && !conv_data.archived)
	    || (!conv->unread && unread))
	{
		return get_next_conv(conv, unread);
	}
	return conv;
}


static bool next_conv_key_handler(int ch)
{
	struct engine_conv *next;

	(void) ch;

	if (!conv_data.curr) {
		output("No conversations available yet.\n");
		return true;
	}

	next = get_next_conv(conv_data.curr, false);
	set_curr_conv(next);

	return true;
}


static struct key_stroke next_conv_stroke = {
	.ch = 'j',
	.h = next_conv_key_handler,
	.help = "next conversation"
};


/*** 'k' ... Switch to previous conversation in list
 */

static struct engine_conv *get_prev_conv(struct engine_conv *conv,
					 bool unread)
{
	struct le *le;

	if (!conv)
		conv = conv_data.curr;

	le = conv->arg;
	if (!le->prev)
		return NULL;

	conv = le->prev->data;
	if ((conv->archived && !conv_data.archived)
	    || (!conv->unread && unread))
	{
		return get_prev_conv(conv, unread);
	}
	return conv;
}


static bool prev_conv_key_handler(int ch)
{
	(void) ch;

	if (!conv_data.curr) {
		output("No conversations available yet.\n");
		return true;
	}

	set_curr_conv(get_prev_conv(conv_data.curr, false));

	return true;
}


static struct key_stroke prev_conv_stroke = {
	.ch = 'k',
	.h = prev_conv_key_handler,
	.help = "prev conversation"
};


/*** 'J' ... Switch to next unread conversation in list
 */

static bool next_unread_key_handler(int ch)
{
	(void) ch;

	if (!conv_data.curr) {
		output("No conversations available yet.\n");
		return true;
	}

	set_curr_conv(get_next_conv(conv_data.curr, true));

	return true;
}


static struct key_stroke next_unread_stroke = {
	.ch = 'J',
	.h = next_unread_key_handler,
	.help = "next unread conversation"
};


/*** 'K' ... Switch to previous unread conversation in list
 */

static bool prev_unread_key_handler(int ch)
{
	(void) ch;

	if (!conv_data.curr) {
		output("No conversations available yet.\n");
		return true;
	}

	set_curr_conv(get_prev_conv(conv_data.curr, true));

	return true;
}


static struct key_stroke prev_unread_stroke = {
	.ch = 'K',
	.h = prev_unread_key_handler,
	.help = "previous unread conversation"
};


/*** 'sw' and 'switch' ... Switch to conversation given by name.
 */

static struct engine_conv *find_conv(const char *name)
{
	size_t len;
	struct le *le;
	char *cname;
	int err;

	len = strlen(name);

	LIST_FOREACH(&conv_data.convl, le) {
		struct engine_conv *conv = le->data;

		err = re_sdprintf(&cname, "%H", engine_print_conv_name,
				  conv);
		if (err)
			continue;
		int name_diff = strncasecmp(cname, name, len);
		int id_diff = strncasecmp(conv->id, name, len);
		mem_deref(cname);
		if (name_diff != 0 && id_diff != 0)
			continue;
		if (conv->archived && !conv_data.archived)
			continue;
		return conv;
	}
	return NULL;
}


static void switch_cmd_help(void)
{
	output("Usage: switch <conversation name>\n\n"
	       "Switches to the first converstation whose name starts with\n"
	       "<conversation name.\n"
	       "<conversation name> is one argument, so quote names with\n"
	       "white space. \n");
}


static void switch_cmd_handler(int argc, char *argv[])
{
	struct engine_conv *conv;

	if (argc != 2) {
		switch_cmd_help();
		return;
	}
	conv = find_conv(argv[1]);
	if (!conv)
		output("No such conversation.\n");
	else {
		set_curr_conv(conv);
	}
}


static struct command switch_command = {
	.command = "switch",
	.h = switch_cmd_handler,
	.helph = switch_cmd_help,
	.help = "switch to conversation in first argument"
};


static void log_cmd_help(void)
{
	output("Usage: log <string>\n\n");
}
	       

static void log_cmd_handler(int argc, char *argv[])
{
	if (argc != 2) {
		log_cmd_help();
		return;
	}
	
	info("%s\n", argv[1]);
}


static struct command log_command = {
	.command = "log",
	.h = log_cmd_handler,
	.helph = log_cmd_help,
	.help = "Log info from string in first argument"
};

#if USE_AVSLIB

/*** start_play. Starts playing a file as microphone signal.
 */

static void start_play_file_cmd_handler(int argc, char *argv[])
{
	const char *name;
	int fs;

	if (argc != 3) {
		output("Usage: start_play fs filename>\n");
		return;
	}

	fs = atoi(argv[1]);
	name = argv[2];

	/* Start File Playout */
	msystem_start_mic_file_playout(name, fs);
}


static struct command start_play_file_command = {
	.command = "start_play",
	.h = start_play_file_cmd_handler,
	.help = "Start playing file as microphone signal"
};


/*** stop_play. Stop playing a file as microphone signal.
 */
static void stop_play_file_cmd_handler(int argc, char *argv[])
{
	/* Stop File Playout */
	msystem_stop_mic_file_playout();
}


static struct command stop_play_file_command = {
	.command = "stop_play",
	.h = stop_play_file_cmd_handler,
	.help = "Stop playing file as microphone signal"
};

/*** "bitrate"/"br" option
 */

static void set_bitrate_handler(int valc, char *valv[], void *arg)
{
	int rate;
	(void) arg;

	if (valc != 2) {
		output("Usage: set bitrate <rate in bps>\n");
		output("   or: set br <rate in bps>\n");
		return;
	}

	rate = atoi(valv[1]);

	msystem_set_bitrate(rate);
}


static void bitrate_help_handler(void *arg)
{
	(void) arg;

	output("Usage: set bitrate <rate>\n"
	       "       set br <rate>\n\n"
	       "Set the audio bitrate to <rate> in bits per second.\n");
}


static struct opt bitrate_option = {
	.key = "bitrate",
	.seth = set_bitrate_handler,
	.helph = bitrate_help_handler,
	.help = "set audio bitrate"
};


static struct opt br_option = {
	.key = "br",
	.seth = set_bitrate_handler,
	.helph = bitrate_help_handler,
	.help = "set audio bitrate"
};


/*** "packetsize"/"ps" option
 */

static void set_packetsize_handler(int valc, char *valv[], void *arg)
{
	int size;

	(void) arg;

	if (valc != 2) {
		output("Usage: set packetsize <size in ms>\n");
		output("   or: set ps <size in ms>\n");
		return;
	}

	size = atoi(valv[1]);

	msystem_set_packet_size(size);
}


static void packetsize_help_handler(void *arg)
{
	(void) arg;

	output("Usage: set packetsize <size>\n"
	       "       set ps <size>\n\n"
	       "Set the sending packet size to <size> in milliseconds.\n");
}


static struct opt packetsize_option = {
	.key = "packetsize",
	.seth = set_packetsize_handler,
	.helph = packetsize_help_handler,
	.help = "set packet size"
};


static struct opt ps_option = {
	.key = "ps",
	.seth = set_packetsize_handler,
	.helph = packetsize_help_handler,
	.help = "set packet size"
};
#endif

/*** "autoanswer" option
 */

static void set_autoanswer_handler(int valc, char *valv[], void *arg)
{
	int val;

	(void) arg;

	if (valc != 2) {
		output("Usage: set autoanswer <bool>\n");
		return;
	}

	val = option_value_to_bool(valv[1]);
	if (val == -1) {
		output("Usage: set autoanswer <bool>\n");
		return;
	}

	conv_data.auto_answer = val;
	output("Auto-answer %s\n", val ? "Enabled" : "Disabled");
}


static void autoanswer_help_handler(void *arg)
{
	(void) arg;

	output("Usage: set autoanswer <bool>\n\n"
	       "Enable or disable auto answer.\n");
}


static struct opt autoanswer_option = {
	.key = "autoanswer",
	.seth = set_autoanswer_handler,
	.helph = autoanswer_help_handler,
	.help = "enable auto answer"
};


/*** 'c' ... Call in current conversation
 */

static bool call_key_handler(int ch)
{
	(void) ch;

	if (!event_estab) {
		warning("Websocket event channel not established"
			" -- cannot make call\n");
		return true;
	}

	if (zcall_video)
		vstart_cmd_handler(0, NULL);
	calling3_start(conv_data.curr);

	return true;
}

struct key_stroke call_stroke = {
	.ch = 'c',
	.h = call_key_handler,
	.help = "start call in current conversation"
};


/*** 'V' ... Toggle video in call
 */

static bool video_key_handler(int ch)
{
	(void) ch;
	int new_state = conv_data.video_state;

	if (ch == 'V') {
		switch (conv_data.video_state) {
		case WCALL_VIDEO_STATE_STOPPED:
			new_state = WCALL_VIDEO_STATE_STARTED;
			break;
		default:
			new_state = WCALL_VIDEO_STATE_STOPPED;
			break;
		}
	}
	else if (ch == 'P') {
		switch (conv_data.video_state) {
		case WCALL_VIDEO_STATE_STARTED:
			new_state = WCALL_VIDEO_STATE_PAUSED;
			break;
		case WCALL_VIDEO_STATE_PAUSED:
			new_state = WCALL_VIDEO_STATE_STARTED;
			break;
		default:
			break;
		}
	}

	if (new_state != conv_data.video_state) {
		switch (new_state) {
		case WCALL_VIDEO_STATE_STARTED:
			vstart_cmd_handler(0, NULL);
			break;
		case WCALL_VIDEO_STATE_PAUSED:
			vpause_cmd_handler(0, NULL);
			break;
		case WCALL_VIDEO_STATE_STOPPED:
			vstop_cmd_handler(0, NULL);
			break;
		default:
			output("Unknown video state, doing nothing\n");
			break;
		}
	}

	return true;
}

struct key_stroke video_stroke = {
	.ch = 'V',
	.h = video_key_handler,
	.help = "toggle video sending during call"
};

struct key_stroke videop_stroke = {
	.ch = 'P',
	.h = video_key_handler,
	.help = "toggle video send paused during call"
};

static bool interrupt_key_handler(int ch)
{
	(void) ch;

	output("TODO: implement me.\n");

	return true;
}


struct key_stroke interrupt_stroke = {
	.ch = 'I',
	.h = interrupt_key_handler,
	.help = "interrupt media"
};


/*** 'a' ... Accept first pending call
 */

static bool find_pending3_handler(struct le *le, void *arg)
{
	struct engine_conv *conv = le->data;

	(void) arg;

	return wcall_get_state(calling3_get_wuser(), conv->id) == WCALL_STATE_INCOMING;
}


static bool accept_key_handler(int ch)
{
	struct engine_conv *conv;
	struct le *le = NULL;
	(void) ch;

	le = list_apply(&conv_data.convl, true,
			find_pending3_handler, NULL);		
	if (!le) {
		output("No call to accept.\n");
		return true;
	}

	conv = le->data;
	set_curr_conv(conv);

	if (zcall_video) {
		vstart_cmd_handler(0, NULL);
	}
	calling3_answer(conv);
	
	return true;
}


struct key_stroke accept_stroke = {
	.ch = 'a',
	.h = accept_key_handler,
	.help = "accept first pending call"
};


static bool reject_key_handler(int ch)
{
	struct engine_conv *conv;
	struct le *le = NULL;
	(void) ch;

	le = list_apply(&conv_data.convl, true,
			find_pending3_handler, NULL);		
	if (!le) {
		output("No call to reject.\n");
		return true;
	}

	conv = le->data;
	set_curr_conv(conv);

	calling3_reject(conv);
	
	return true;
}


struct key_stroke reject_stroke = {
	.ch = 'R',
	.h = reject_key_handler,
	.help = "reject first pending call"
};


/*** 'e' ... End call in current conversation
 */

static bool end_call_key_handler(int ch)
{
	(void) ch;

	if (!conv_data.curr)
		return true;

	calling3_end(conv_data.curr);

#if USE_AVSLIB
	/* Stop File playout if enabled */
	msystem_stop_mic_file_playout();
#endif

	return true;
}

struct key_stroke end_stroke = {
	.ch = 'e',
	.h = end_call_key_handler,
	.help = "end call in current conversation"
};


/*** 'm' ... Mute/unmute
 */

static bool mute_key_handler(int ch)
{
	bool muted;

	(void) ch;

	muted = wcall_get_mute(calling3_get_wuser());

	wcall_set_mute(calling3_get_wuser(), muted ? 0 : 1);

	if (muted)
		output("Microphone unmuted.\n");
	else
		output("Microphone muted.\n");
	return true;
}

struct key_stroke mute_stroke = {
	.ch = 'm',
	.h = mute_key_handler,
	.help = "mute/unmute microphone"
};


static bool calling3_key_handler(int ch)
{
	(void) ch;

	calling3_dump();

	return true;
}


static bool calling3_stats_handler(int ch)
{
	(void) ch;

	calling3_stats();

	return true;
}


static struct key_stroke calling3_stroke = {
	.ch = 'f',
	.h = calling3_key_handler,
	.help = "Calling 3 debug"
};

static struct key_stroke calling3_stats_stroke = {
	.ch = 'z',
	.h = calling3_stats_handler,
	.help = "Calling stats"
};


static bool propsync_key_handler(int ch)
{
	(void) ch;

	output("sending PROPSYNC request..\n");

	calling3_propsync(conv_data.curr);

	return true;
}


static struct key_stroke propsync_stroke = {
	.ch = 'p',
	.h = propsync_key_handler,
	.help = "propsync"
};


/*** say. Send a E2EE secure text message
 */


#if defined (HAVE_CRYPTOBOX)
static void otr_resp_handler(int err, void *arg)
{
	output("OTR Response: %m\n", err);
}
#endif


static void say_cmd_handler(int argc, char *argv[])
{
	struct engine_conv *conv = conv_data.curr;
	const char *text_content = argv[1];
	uint8_t *pbuf = NULL;
	size_t pbuf_len = 8192;
	int err;

	(void) argc;

	if (!conv_data.curr) {
		output("No conversation selected ...\n");
		return;
	}

	if (conv_data.curr->memberl.head == NULL) {
		output("No user members in this conversation\n");
		return;
	}

	pbuf = mem_alloc(pbuf_len, NULL);

#if defined (HAVE_PROTOBUF)
	err = protobuf_encode_text(pbuf, &pbuf_len, text_content);
#else
	output("no protobuf compiled in\n");
	(void)text_content;
	err = ENOSYS;
#endif
	if (err) {
		warning("protobuf_encode failed (%m)\n", err);
		goto out;
	}

#if defined (HAVE_CRYPTOBOX)
	char lclientid[64];

	err = client_id_load(lclientid, sizeof(lclientid));
	if (err) {
		debug("my clientid not set -- cannot send OTR\n");
		goto out;
	}

	err = engine_send_otr_message(zcall_engine, g_cryptobox, conv, NULL, 0,
		 lclientid, pbuf, pbuf_len, false, false,
		 otr_resp_handler, NULL);
	if (err)
		output("Send OTR message Failed: %m.\n", err);
	else
		output("OK\n");
#else
	output("no cryptobox compiled in\n");
	(void)conv;
#endif

 out:
	mem_deref(pbuf);
}


static struct command say_command = {
	.command = "say",
	.h = say_cmd_handler,
	.help = "Send a secure E2EE text message.",
	.verbatim = true
};


#ifdef HAVE_CRYPTOBOX

/*** get_prekeys. Get a prekey for each client of a user.
 */

static void prekey_handler(const char *userid,
			   const uint8_t *key, size_t key_len,
			   uint16_t id, const char *clientid,
			   bool last, void *arg)
{
	struct session *sess;
	char lclientid[64];
	int err;

	output("prekey_handler: %zu bytes, user:%s[%u] -> %s\n",
	       key_len, userid, id, clientid);


	err = client_id_load(lclientid, sizeof(lclientid));
	if (err) {
		debug("my clientid not set -- cannot store prekeys\n");
		return;
	}

	sess = cryptobox_session_find(g_cryptobox, userid, clientid, lclientid);
	if (sess) {
		output("prekey: session found\n");
	}
	else {
		info("conv: adding key to cryptobox for clientid=%s\n",
		     clientid);

		err = cryptobox_session_add_send(g_cryptobox, userid, clientid, lclientid,
						 key, key_len);
		if (err) {
			warning("cryptobox_session_add_send failed (%m)\n",
				err);
		}
	}
}


static void get_prekeys_cmd_handler(int argc, char *argv[])
{
	struct engine_user *self;
	struct le *le;
	int err = 0;
	(void) argc;

	static const struct prekey_handler pkh = {
		.prekeyh = prekey_handler
	};

	if (!conv_data.curr) {
		output("No conversation selected ...\n");
		return;
	}

	output("fetching prekeys for self\n");

	self = engine_get_self(conv_data.curr->engine);
	err = engine_get_prekeys(zcall_engine, self->id, &pkh);

	LIST_FOREACH(&conv_data.curr->memberl, le) {
		struct engine_conv_member *mbr = le->data;

		output("fetching prekeys for userid=\"%s\" \"%s\"...\n",
		       mbr->user->id, mbr->user->display_name);

		err |= engine_get_prekeys(zcall_engine, mbr->user->id, &pkh);
		if (err)
			output("Failed: %m.\n", err);
		else
			output("OK\n");
	}

}


static struct command get_prekeys_command = {
	.command = "get_prekeys",
	.h = get_prekeys_cmd_handler,
	.help = "Get prekeys for the current User in Conv (only 1:1).",
	.verbatim = true
};


#endif


static void conv_engine_user_clients_handler(int err, const char *clientidv[],
					     size_t clientidc, void *arg)
{
	struct engine_user *user = arg;
	size_t i;

	if (err == ECONNABORTED)
		return;

	if (err) {
		output("get user clients failed (%m)\n", err);
		return;
	}

	output("%s: has %zu clients:\n",
	       user->display_name,
	       clientidc);
	for(i = 0; i < clientidc; ++i) {
		output("\t%s\n", clientidv[i]);
	}
}


static void get_user_clients_cmd_handler(int argc, char *argv[])
{
	struct engine_conv_member *mbr;
	struct engine_user *user;
	int err = 0;
	(void) argc;

	if (!conv_data.curr) {
		output("No conversation selected ...\n");
		return;
	}

	if (conv_data.curr->type != ENGINE_CONV_ONE) {
		output("This conversation is not a regular 1:1 conv.\n");
		return;
	}

	mbr = list_ledata(conv_data.curr->memberl.head);
	if (!mbr) {
		output("No user members in this conversation\n");
		return;
	}
	user = mbr->user;

	output("getting clients for userid=\"%s\" \"%s\"...\n",
	       user->id, user->display_name);

	err = engine_get_user_clients(zcall_engine, user->id,
				      conv_engine_user_clients_handler, user);
	if (err)
		output("Failed: %m.\n", err);
	else
		output("OK\n");
}


static struct command get_user_clients_command = {
	.command = "get_user_clients",
	.h = get_user_clients_cmd_handler,
	.help = "Get clients for the current User in Conv (only 1:1).",
	.verbatim = true
};

static void use_audio_effect_cmd_handler(int argc, char *argv[])
{
    int err = 0;
    (void) argc;
    
    if (!conv_data.curr) {
        output("No conversation selected ...\n");
        return;
    }
    
    if (argc != 2) {
        output("usage: useAudioEffect effect 0 - 7 \n");
        return;
    }
    
    enum audio_effect effect;
    if(atoi(argv[1]) == 7){
        effect = AUDIO_EFFECT_HARMONIZER_MED;
    } else if(atoi(argv[1]) == 6){
        effect = AUDIO_EFFECT_VOCODER_MIN;
    } else if(atoi(argv[1]) == 5){
        effect = AUDIO_EFFECT_AUTO_TUNE_MED;
    } else if(atoi(argv[1]) == 4){
        effect = AUDIO_EFFECT_PITCH_UP_DOWN_MED;
    } else if(atoi(argv[1]) == 3){
        effect = AUDIO_EFFECT_CHORUS_MED;
    } else if(atoi(argv[1]) == 2){
        effect = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX;
    } else if(atoi(argv[1]) == 1){
        effect = AUDIO_EFFECT_PITCH_UP_SHIFT_MAX;
    } else {
        effect = AUDIO_EFFECT_NONE;
    }

#if USE_AVSLIB
    err = voe_set_audio_effect(effect);
#endif
    
    if (err)
	    output("Failed: %m.\n", err);
    else
	    output("OK\n");
}

struct command use_audio_effect_command = {
    .command = "useAudioEffect",
    .h = use_audio_effect_cmd_handler,
    .help = "Apply audio effect to transmitted audio.",
    .verbatim = true
};


/*** batch_call. Create hundreds of calls
 */

static void batch_call_handler(void *arg);


static void batch_call_complete(void)
{
	size_t i;
	double acc_dur = 0;
	size_t acc_setup = 0;
	//ssize_t acc_turn = 0;
	//ssize_t acc_nat = 0;
	//ssize_t acc_dtls = 0;

	output("Batch call summary for %zu/%zu calls:\n",
		  conv_data.batchi, conv_data.batchc);
	output("\n");
	output("Num:      Started:  Estab:    Duration:    Media-setup-ms:"
	       "  turn:   ice:   dtls:\n");

	for (i=0; i<conv_data.batchc; i++) {

		struct batch_call *bcall = &conv_data.batchv[i];
		int diff = (int)(bcall->ts_estab - bcall->ts_start);
		double dur;
		//struct mediaflow_stats *mf_stats = &bcall->mf_stats;

		dur = .001 * (double)(bcall->ts_ended - bcall->ts_estab);

		output("#%-3zu      %d         %d         %.1f"
			  "          %5d",
			  bcall->call_num,
			  bcall->started,
			  bcall->estab,
			  dur,
			  diff);

		//output("        %5d    %5d    %5d\n",
		//       bcall->mf_stats.turn_alloc,
		//       bcall->mf_stats.nat_estab,
		//       bcall->mf_stats.dtls_estab);

		acc_dur   += dur;
		acc_setup += diff;
		//acc_turn  += mf_stats->turn_alloc;
		//acc_nat   += mf_stats->nat_estab;
		//acc_dtls  += mf_stats->dtls_estab;
	}

	output("Average:                      "
	       //"%.1f            %.1f     %.1f     %.1f     %.1f\n",
	       "%.1f            %.1f\n",
	       1.0 * acc_dur / conv_data.batchc,
	       1.0 * (double)acc_setup / conv_data.batchc
	       //1.0 * (double)acc_turn / conv_data.batchc,
	       //1.0 * (double)acc_nat / conv_data.batchc,
	       //1.0 * (double)acc_dtls / conv_data.batchc
	       );

	output("\n");

	tmr_cancel(&conv_data.tmr_batch);
	conv_data.batchv = mem_deref(conv_data.batchv);
	conv_data.batchi = 0;
	conv_data.batchc = 0;
}


static void batch_end_handler(void *arg)
{
	struct batch_call *bcall = arg;

	output("batch_call: ending call number %zu\n", bcall->call_num);
	end_call_key_handler(0);

	bcall->ended = true;
	bcall->ts_ended = tmr_jiffies();

	conv_data.batchi += 1;

	if (conv_data.batchi >= conv_data.batchc) {

		output("batch-calling is complete.\n");
		batch_call_complete();
		return;
	}

	conv_data.batch_retry = 0;
	tmr_start(&conv_data.tmr_batch, 1*1000, batch_call_handler, 0);
}


static void batch_call_handler(void *arg)
{
	struct batch_call *bcall;
	bool incall = false;
	int state;

	state = wcall_get_state(calling3_get_wuser(), conv_data.curr->id);

	switch (state) {

	case WCALL_STATE_NONE:
	case WCALL_STATE_UNKNOWN:
		incall = false;
		break;

	default:
		incall = true;
		break;
	}

	if (incall) {
		if (conv_data.batch_retry++ >= MAX_BATCH_RETRIES) {
			output("batch_call: max reties reached, aborting\n");
			return;
		}
		else {
			uint32_t delay = conv_data.batch_retry;
			output("batch_call: still in a call, waiting %us\n", delay);
			tmr_start(&conv_data.tmr_batch, delay*1000, batch_call_handler, 0);
			return;
		}
	}
	conv_data.batch_retry = 0;

	bcall = &conv_data.batchv[conv_data.batchi];

	bcall->call_num = conv_data.batchi + 1;
	bcall->started = true;
	bcall->ts_start = tmr_jiffies();

	output("batch_call: starting call number %zu\n", bcall->call_num);
	call_key_handler(0);
}


void batch_call_established(void/*const struct mediaflow *mf*/)
{
	struct batch_call *bcall;
	//const struct mediaflow_stats *stats;

	if (!conv_data.batchv || !conv_data.batchc)
		return;

	bcall = &conv_data.batchv[conv_data.batchi];

	bcall->estab = true;
	bcall->ts_estab = tmr_jiffies();
/*
	stats = mediaflow_stats_get(mf);
	if (stats) {
		bcall->mf_stats = *stats;
	}
	else {
		warning("could not get mediaflow stats\n");
	}
*/
	tmr_start(&conv_data.tmr_batch, conv_data.batch_dur * 1000,
		  batch_end_handler, bcall);
}


static void batch_call_cmd_handler(int argc, char *argv[])
{
	size_t num, dur;

	if (argc < 3) {
		output("usage: batch_call <count> <duration>\n");
		return;
	}

	num = atoi(argv[1]);
	dur = atoi(argv[2]);

	output("starting batch-calling with %zu calls and"
	       " %zu seconds duration..\n", num, dur);

	if (!conv_data.curr) {
		output("No conversation selected ...\n");
		return;
	}
	if (conv_data.batchv) {
		output("Batch-mode already running.\n");
		return;
	}

	conv_data.batch_dur = dur;
	conv_data.batchv = mem_reallocarray(NULL, num,
					    sizeof(*conv_data.batchv), NULL);
	conv_data.batchi = 0;
	conv_data.batchc = num;

	tmr_start(&conv_data.tmr_batch, 1, batch_call_handler, NULL);
}


static struct command batch_call_command = {
	.command = "batch_call",
	.h = batch_call_cmd_handler,
	.help = "Batch calling.",
};


static void conv_call_cmd_handler(int argc, char *argv[])
{
	const char *convid;
	struct engine_conv *conv = 0;
	int err;

	if (argc < 2) {
		output("usage: conv_call <convid>\n");
		return;
	}

	convid = argv[1];

	output("starting conv-calling in conversation %s..\n", convid);

	err = engine_lookup_conv(&conv, zcall_engine, convid);
	if (err) {
		output("no such conversation (%s)\n", convid);
		return;
	}

	set_curr_conv(conv);

	output("Calling in conversation: %H\n",
	       engine_print_conv_name, conv);

	call_key_handler(0);
}


static struct command conv_call_command = {
	.command = "conv_call",
	.h = conv_call_cmd_handler,
	.help = "Make a call in a specific conversation.",
};


#if USE_AVSLIB
static void netprobe_handler(int err,
			     uint32_t rtt_avg,
			     size_t n_pkt_sent,
			     size_t n_pkt_recv,
			     void *arg)
{
	if (err) {
		output("netprobe failed (%m)\n", err);
		return;
	}

	output("Netprobe result:\n");
	output("        rtt:      %.1f ms\n", .001 * rtt_avg);
	output("        sent:     %zu packets\n", n_pkt_sent);
	output("        recv:     %zu packets\n", n_pkt_recv);
}


static void netprobe_cmd_handler(int argc, char *argv[])
{
	void *wuser = calling3_get_wuser();
	int err;

	err = wcall_netprobe(wuser, 10, 100, netprobe_handler, NULL);
	if (err) {
		output("netprobe failed (%m)\n", err);
	}
}


static struct command netprobe_command = {
	.command = "netprobe",
	.h = netprobe_cmd_handler,
	.help = "Run a netprobe.",
};
#endif

/*** conv_ready
 */

static void switch_convid(const char *convid)
{
	struct le *le;

	if (!convid || !*convid)
		return;

	LIST_FOREACH(&conv_data.convl, le) {
		struct engine_conv *conv = le->data;

		if (streq(conv->id, convid)) {
			if (!conv->archived) {
				conv_data.curr = conv;
				output("Current conversation: %H\n",
				       engine_print_conv_name,
				       conv_data.curr);
			}
			return;
		}
	}
}


void conv_ready(void)
{
	struct sobject *so;
	char *convid;
	int err;

	if (!zcall_store)
		return;

	err = store_user_open(&so, zcall_store, "zcall", "conv", "rb");
	if (err)
		return;

	err = sobject_read_lenstr(&convid, so);
	if (err)
		goto out;

	switch_convid(convid);

 out:
	mem_deref(convid);
	mem_deref(so);
}

/*** vstart, vstop, vpause
 */
static void vstart_cmd_help(void)
{
	output("Usage: vstart\n");
}

static void vstart_cmd_handler(int argc, char *argv[])
{
	if (WCALL_VIDEO_STATE_STARTED != conv_data.video_state) {
		output("Enabling preview, send state STARTED\n");
		preview_start();
		conv_data.video_state = WCALL_VIDEO_STATE_STARTED;
		calling3_set_video_send_state(conv_data.curr, conv_data.video_state);
	}
}

static struct command vstart_command = {
	.command = "vstart",
	.h = vstart_cmd_handler,
	.helph = vstart_cmd_help,
	.help = "start video send"
};

static void vstop_cmd_help(void)
{
	output("Usage: vstop\n");
}

static void vstop_cmd_handler(int argc, char *argv[])
{
	if (WCALL_VIDEO_STATE_STOPPED != conv_data.video_state) {
		output("Disabling preview, send state STOPPED\n");
		preview_stop();
		conv_data.video_state = WCALL_VIDEO_STATE_STOPPED;
		calling3_set_video_send_state(conv_data.curr, conv_data.video_state);
	}
}

static struct command vstop_command = {
	.command = "vstop",
	.h = vstop_cmd_handler,
	.helph = vstop_cmd_help,
	.help = "stop video send"
};

static void vpause_cmd_help(void)
{
	output("Usage: vpause\n");
}

static void vpause_cmd_handler(int argc, char *argv[])
{
	if (WCALL_VIDEO_STATE_STARTED == conv_data.video_state) {
		output("Disabling preview, send state PAUSED\n");
		preview_stop();
		conv_data.video_state = WCALL_VIDEO_STATE_PAUSED;
		calling3_set_video_send_state(conv_data.curr, conv_data.video_state);
	}
}

static struct command vpause_command = {
	.command = "vpause",
	.h = vpause_cmd_handler,
	.helph = vpause_cmd_help,
	.help = "pause video send"
};

static void vsave_cmd_help(void)
{
	output("Usage: vsave\n");
}

static void vsave_cmd_handler(int argc, char *argv[])
{
	output("Next frame will be saved in file <userid>.pgm\n");
	test_view_capture_next_frame();
}

static struct command vsave_command = {
	.command = "vsave",
	.h = vsave_cmd_handler,
	.helph = vsave_cmd_help,
	.help = "save next video frame for each user"
};


/*** House Keeping
 */

struct engine_lsnr conv_lsnr = {
	.addconvh = conv_added_handler,
#ifdef HAVE_CRYPTOBOX
	.otraddmsgh = otr_add_message_handler,
#endif
	.arg = NULL
};


int conv_init(void)
{
	register_key_stroke(&archive_stroke);
	register_key_stroke(&list_stroke);
	register_key_stroke(&info_stroke);
	register_key_stroke(&next_conv_stroke);
	register_key_stroke(&prev_conv_stroke);
	register_key_stroke(&next_unread_stroke);
	register_key_stroke(&prev_unread_stroke);
	register_key_stroke(&call_stroke);
	register_key_stroke(&accept_stroke);
	register_key_stroke(&reject_stroke);
	register_key_stroke(&end_stroke);
	register_key_stroke(&mute_stroke);
	register_key_stroke(&calling3_stroke);
	register_key_stroke(&calling3_stats_stroke);
	register_key_stroke(&video_stroke);
	register_key_stroke(&videop_stroke);
	register_key_stroke(&interrupt_stroke);
	register_key_stroke(&propsync_stroke);

	register_command(&switch_command);
	register_command(&log_command);

#if USE_AVSLIB
	register_command(&start_play_file_command);
	register_command(&stop_play_file_command);
#endif
	register_command(&say_command);

	register_command(&use_audio_effect_command);

#ifdef HAVE_CRYPTOBOX
	register_command(&get_prekeys_command);
#endif
	register_command(&get_user_clients_command);
	register_command(&batch_call_command);
	register_command(&conv_call_command);

#if USE_AVSLIB
	register_command(&netprobe_command);
	register_option(&bitrate_option);
	register_option(&br_option);
	register_option(&packetsize_option);
	register_option(&ps_option);
#endif
	register_option(&autoanswer_option);

	register_command(&vstart_command);
	register_command(&vstop_command);
	register_command(&vpause_command);
	register_command(&vsave_command);

	engine_lsnr_register(zcall_engine, &conv_lsnr);

	return 0;
}


const char *get_current_conv_id(void)
{
	if (conv_data.curr && conv_data.curr->id) {
		return conv_data.curr->id;
	}

	return "";
}


void conv_close(void)
{
	tmr_cancel(&conv_data.tmr_batch);
	conv_data.batchv = mem_deref(conv_data.batchv);
	conv_data.batchi = 0;
	conv_data.batchc = 0;

	list_clear(&conv_data.convl);
	engine_lsnr_unregister(&conv_lsnr);

	tmr_cancel(&conv_data.tmr_autoanswer);

	calling3_close();
}


void calling_incoming(void)
{
	if (conv_data.auto_answer || zcall_auto_answer) {
		uint32_t delay = 1;

		output("auto-answering incoming call"
		       " with delay of %u milliseconds\n",
		       delay);

		tmr_start(&conv_data.tmr_autoanswer, delay,
			  tmr_autoanswer_handler, NULL);
	}
	else {
		/* Poor mans ringtone */
		/*output("\033[10;1000]\033[11;1000]\a"); */
	}
}


void calling_established(void)
{
	struct batch_call *bcall;

	re_printf("calling: established\n");

	if (conv_data.batchv) {
		bcall = &conv_data.batchv[conv_data.batchi];

		bcall->estab = true;
		bcall->ts_estab = tmr_jiffies();

		tmr_start(&conv_data.tmr_batch, conv_data.batch_dur*1000,
			  batch_end_handler, bcall);
	}
}
