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
 * Application header
 */


/* Input/output  */

int  input_init(void);
void input_shutdown(void);
void input_close(void);

void set_debug_mode(bool debug);
bool debug_mode(void);
void voutput(const char *fmt, va_list ap);
void output(const char *fmt, ...);
void voutpar(const char *indent, const char *fmt, va_list ap);
void outpar(const char *indent, const char *fmt, ...);

typedef bool(key_stroke_h)(int ch);
struct key_stroke {
	struct le le;
	int ch;
	key_stroke_h *h;
	char *help;
};
void register_key_stroke(struct key_stroke *ks);
void unregister_key_stroke(struct key_stroke *ks);

typedef void(command_h)(int argc, char *argv[]);
typedef void(cmd_help_h)(void);
struct command {
	struct le le;
	char *command;
	command_h *h;
	cmd_help_h *helph;
	char *help;
	bool verbatim;  /* entire line as one single argument  */
};
void register_command(struct command *cmd);
void unregister_command(struct command *cmd);

void io_stroke_input(char ch);
void io_command_input(const char *line);

typedef void (output_h)(const char *str, void *arg);

void register_output_handler(output_h *outputh, void *arg);


/* Logging  */

int clilog_init(enum log_level level, const char *path);
void clilog_close(void);


/* Conversation handling  */

int  conv_init(void);
void conv_ready(void);
void conv_close(void);
const char *get_current_conv_id(void);
void set_curr_conv(struct engine_conv *conv);
void batch_call_established(void/*const struct mediaflow *mf*/);


/* User handling  */

int  user_init(void);
void user_close(void);


/* Get your engine here.  */
struct engine *zcall_engine;
struct store *zcall_store;
extern bool zcall_auto_answer;
extern bool zcall_video;
extern bool zcall_av_test;
extern bool event_estab;
extern int g_trace;
extern bool g_ice_privacy;
extern bool g_use_kase;
extern bool g_use_conference;
extern char *g_sft_url;
#ifdef HAVE_CRYPTOBOX
extern struct cryptobox *g_cryptobox;
#endif


/* Manage Clients/Devices */

int  client_init(void);
void client_close(void);

int  client_id_load(char *buf, size_t sz);
int  client_id_save(const char *clientid);
void client_id_delete(void);


/* Protobuf layer */

void handle_calling_event(struct engine_conv *conv,
			  struct engine_user *from_user,
			  const char *from_clientid,
			  const struct ztime *timestamp,
			  const char *data);


/* Calling 3 */

extern bool zcall_audio_cbr;
extern bool zcall_force_audio;

int  calling3_init(void);
void calling3_recv_msg(const char *convid,
		       const char *from_userid,
		       const char *from_clientid,
		       const struct ztime *timestamp,
		       const char *data);
int  calling3_start(struct engine_conv *conv);
void calling3_answer(struct engine_conv *conv);
void calling3_reject(struct engine_conv *conv);
void calling3_end(struct engine_conv *conv);
void calling3_set_video_send_state(struct engine_conv *conv, int state);
void calling3_propsync(struct engine_conv *conv);
void calling3_close(void);
void calling3_dump(void);
void calling3_stats(void);


/* "handlers" */
void calling_incoming(void);
void calling_established(void);


/*
 * REST Server
 */

int  restsrv_init(uint16_t lport);
void restsrv_close(void);


/*
 * Pairing
 */

int  pairing_init(void);
void pairing_close(void);
