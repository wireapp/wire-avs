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
/* zclient-call -- cli version
 *
 * Input and output
 */

#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#ifdef HAVE_READLINE
#include <wordexp.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif
#ifdef LIST_INIT
#undef LIST_INIT
#endif
#ifdef LIST_FOREACH
#undef LIST_FOREACH
#endif
#include <re.h>
#include <avs.h>
#include "options.h"
#include "cli.h"


enum io_mode {
	MODE_DEFAULT = 0,
	MODE_COMMAND
};

enum par_mode {
	PAR_NORMAL = 0,
	PAR_WORDWRAP
};

struct io_par {
	struct le le;

	enum par_mode mode;
	char *content;
	char *indent;
};

static struct {
	enum io_mode mode;     /* The mode we are currently in. */

	struct list strokel;   /* Key stroke handlers.  */
	struct list cmdl;      /* Command handlers.  */

	bool term_set;         /* terminal handling for single key input.  */
	struct termios term;

	struct list parl;      /* pending paragraphs in command mode.  */

	output_h *outputh;
	void     *outputh_arg;

} io = {
	.mode = MODE_DEFAULT,

	.strokel = LIST_INIT,
	.cmdl = LIST_INIT,

	.term_set = false,

	.parl = LIST_INIT
};


/*** Paragraph handling
 */

static void par_normal(const char *content)
{
	(void)re_fprintf(stdout, "%s", content);
}


static size_t get_term_width(void)
{
	struct winsize w;
	int err;

	err = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	if (err)
		return 80;

	return w.ws_col;
}


static void par_wordwrap(const char *content, const char *indent)
{
	char *word;
	size_t linelen, curlinelen, len, wordlen;
	bool first;
	int convs;

	if (!indent)
		indent = "";

	word = mem_zalloc(strlen(content), NULL);
	if (!word)
		return;

	linelen = get_term_width();

	curlinelen = linelen;
	len = 0;
	first = true;

	/* Print leading white space.
	 */
	while (isspace(*content)) {
		fwrite(content, 1, 1, stdout);
		++content;
		--curlinelen;
	}

	while (*content) {
		convs = sscanf(content, "%s", word);
		if (convs != 1)
			break;
		wordlen = strlen(word);

		if (len + wordlen > curlinelen) {
			if (len == 0) {
				/* Word is too long. Print what fits and
				 * continue.
				 */
				fwrite(content, curlinelen, 1, stdout);
				content += curlinelen;
			}
			re_fprintf(stdout, "\n%s", indent);
			if (first) {
				curlinelen = linelen - strlen(indent);
				first = false;
			}
			len = 0;
		}
		else {
			(void)re_fprintf(stdout, "%s ", word);

			content += wordlen;
			len += wordlen + 1;
			while (isspace(*content)) {
				if (*content == '\n') {
					re_fprintf(stdout, "\n%s", indent);
					if (first) {
						curlinelen = linelen -
							strlen(indent);
						first = false;
					}
					len = 0;
				}
				++content;
			}
		}
	}
	fwrite("\n", 1, 1, stdout);

	goto out;

 out:
	mem_deref(word);
}


static void io_par_destructor(void *arg)
{
	struct io_par *par = arg;

	mem_deref(par->content);
	mem_deref(par->indent);
}


static int append_par(enum par_mode mode, char *content,
		      const char *indent)
{
	struct io_par *par;
	int err = 0;

	par = mem_zalloc(sizeof(*par), io_par_destructor);
	if (!par)
		return ENOMEM;

	par->mode = mode;
	par->content = content;
	if (indent) {
		err = str_dup(&par->indent, indent);
		if (err)
			goto out;
	}
	list_append(&io.parl, &par->le, par);

 out:
	if (err)
		mem_deref(par);
	return 0;
}


/*** Mode-sensitive output.
 *
 * The idea here is that when we are in command mode, we buffer all input
 * until the user has finished typing their command and then dump it all
 * onto stdout in one fell swoop.
 */


void voutput(const char *fmt, va_list ap)
{
	int err;

	if (io.mode == MODE_COMMAND) {
		char *content;

		err = re_vsdprintf(&content, fmt, ap);
		if (err)
			return;

		append_par(PAR_NORMAL, content, NULL);
	}
	else {
		char *content = NULL;

		err = re_vsdprintf(&content, fmt, ap);
		if (err)
			return;

		if (io.outputh)
			io.outputh(content, io.outputh_arg);
		else
			fwrite(content, strlen(content), 1, stdout);

		mem_deref(content);
	}
}

void output(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	voutput(fmt, ap);
	va_end(ap);
}


void voutpar(const char *indent, const char *fmt, va_list ap)
{
	char *content;
	int err;

	err = re_vsdprintf(&content, fmt, ap);
	if (err)
		return;

	if (io.mode == MODE_COMMAND) {
		append_par(PAR_WORDWRAP, content, indent);
	}
	else {
		par_wordwrap(content, indent);
		mem_deref(content);
	}
}


void outpar(const char *indent, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	voutpar(indent, fmt, ap);
	va_end(ap);
}


static void flush_output(void)
{
	struct le *le;

	LIST_FOREACH(&io.parl, le) {
		struct io_par *par = le->data;

		if (par->mode == PAR_NORMAL)
			par_normal(par->content);
		else
			par_wordwrap(par->content, par->indent);
	}

	list_flush(&io.parl);
}


/* Keyboard input.
 */

static int prepare_terminal(void)
{
	struct termios now;
	int err = 0;

	if (tcgetattr(STDIN_FILENO, &io.term) < 0) {
		err = errno;
		(void)re_fprintf(stderr,
				 "tcgetattr failed: %d\n", err);
		goto out;
	}

	now = io.term;

	now.c_lflag |= ISIG;
	now.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);

	/* required on Solaris */
	now.c_cc[VMIN] = 1;
	now.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &now) < 0) {
		(void)re_fprintf(stderr,
				 "tcsetattr failed: %d\n", err);
		err = errno;
		goto out;
	}

	io.term_set = true;

 out:
	return err;
}


static void release_terminal(void)
{
	if (io.term_set) {
		tcsetattr(STDIN_FILENO, TCSANOW, &io.term);
		io.term_set = false;
	}
}


static void handle_words(const char *line, size_t wordc, char **wordv)
{
	struct command *found;
	struct le *le;
	size_t len;

	len = strlen(wordv[0]);

	found = NULL;
	for (le = io.cmdl.head; le; le = le->next) {
		struct command *cmd = le->data;

		if (strncmp(cmd->command, wordv[0], len))
			continue;

		if (found) {
			output("%s: not a unique command abbreviation.\n",
				wordv[0]);
			goto out;
		}
		found = cmd;
	}
	if (!found) {
		output("Command not found.\n");
		goto out;
	}

	if (found->verbatim) {
		char *s[2];

		s[0] = wordv[0];
		s[1] = (char *)line + len;
		while (isspace(*s[1]))
			++s[1];
		found->h(2, s);
	}
	else
		found->h((int)wordc, wordv);

 out:
	return;
}


#ifdef HAVE_READLINE
static void handle_line(const char *line)
{
	wordexp_t p;

	if (line == NULL) {
		re_cancel();
		return;
	}

	add_history(line);
	if (0 != wordexp(line, &p, 0)) {
		re_fprintf(stderr, "wordexp failed\n");
		return;
	}

	if (p.we_wordc < 1)
		goto out;

	handle_words(line, p.we_wordc, p.we_wordv);

 out:
	wordfree(&p);
}
#else

static void handle_line(const char *line)
{
	struct str_wordexp p;
	int err;

	if (line == NULL) {
		re_cancel();
		return;
	}

	err = str_wordexp(&p, line);
	if (err) {
		re_fprintf(stderr, "str_wordexp failed (%m)\n", err);
		return;
	}

	if (p.wordc < 1)
		goto out;

	handle_words(line, p.wordc, p.wordv);

 out:
	str_wordfree(&p);
}

#endif


#ifdef HAVE_READLINE
static void readline_line_handler(char *line)
{
	rl_callback_handler_remove();
	io.mode = MODE_DEFAULT;
	flush_output();
	(void)prepare_terminal();

	handle_line(line);

	if (line)
		free(line);
}
#else
static void non_readline_line_handler(char *line)
{
	io.mode = MODE_DEFAULT;
	flush_output();
	(void)prepare_terminal();

	handle_line(line);
}
#endif


static void enable_readline(void)
{
	release_terminal();

#ifdef HAVE_READLINE
	rl_callback_handler_install(":", readline_line_handler);
#endif

	io.mode = MODE_COMMAND;
}


static void print_keys(void)
{
	struct le *le;

	output("Keys:\n");
	output("   q   quit\n");
	output("   :   enter command mode\n");

	LIST_FOREACH(&io.strokel, le) {
		struct key_stroke *stroke = le->data;
		output("   %c   %s\n", stroke->ch, stroke->help);
	}
}


static void help_cmd_handler(int argc, char *argv[])
{
	struct le *le;

	if (argc == 1) {
		LIST_FOREACH(&io.cmdl, le) {
			struct command *cmd = le->data;

			output("%-16s .. %s\n", cmd->command, cmd->help);
		}
	}
	else if (argc == 2) {
		LIST_FOREACH(&io.cmdl, le) {
			struct command *cmd = le->data;

			if (!strcasecmp(cmd->command, argv[1])) {
				if (cmd->helph)
					cmd->helph();
				else
					output("No help available.\n");
				return;
			}
		}
		print_option_help(argv[1]);
	}
}

static struct command help_cmd = {
	.command = "help",
	.h = help_cmd_handler,
	.help = "this here"
};


static void stdin_default_handler(void)
{
	int ch = fgetc(stdin);

	switch (ch) {

	case '\n': /* Just do a line feed.  */
		output("\n");
		break;

	case 'q': /* Quit.  */
		if (zcall_engine)
			engine_shutdown(zcall_engine);
		else
			re_cancel();
		input_shutdown();
		break;

	case ':': /* Enter command mode.  */
		enable_readline();
		break;

	case 'h':
	case '?': /* Print help.  */
		print_keys();
		break;

	default:
		io_stroke_input(ch);
		break;
	}
}


static void stdin_handler(int flags, void *arg)
{
	(void)arg;

	if (flags && FD_READ) {
		if (io.mode == MODE_COMMAND) {
#ifdef HAVE_READLINE
			rl_callback_read_char();
#else

			char buf[512] = "";
			char *nl;

			fgets(buf, sizeof(buf), stdin);
			nl = strstr(buf, "\n");
			if (nl)
				*nl = '\0';

			non_readline_line_handler(buf);
#endif
		}
		else {
			stdin_default_handler();
		}
	}
}


int input_init(void)
{
	int err = 0;

	err = fd_listen(STDIN_FILENO, FD_READ, stdin_handler, NULL);
	if (err) {
		(void)re_fprintf(stderr,
				 "Listening to stdin failed: %d\n", err);
		goto out;
	}
	err = prepare_terminal();
	if (err) {
		goto out;
	}

	register_command(&help_cmd);

 out:
	return err;
}


void input_shutdown(void)
{
	fd_close(STDIN_FILENO);
}


void input_close(void)
{
	release_terminal();

	fd_close(STDIN_FILENO);
	list_flush(&io.parl);
}


void register_key_stroke(struct key_stroke *ks)
{
	struct le *le;

	if (!ks)
		return;

	LIST_FOREACH(&io.strokel, le) {
		struct key_stroke *leks = le->data;

		if (tolower(leks->ch) > tolower(ks->ch)) {
			list_insert_before(&io.strokel, le, &ks->le, ks);
			return;
		}
	}

	list_append(&io.strokel, &ks->le, ks);
}


void unregister_key_stroke(struct key_stroke *ks)
{
	if (!ks)
		return;

	list_unlink(&ks->le);
}


void register_command(struct command *cmd)
{
	if (!cmd)
		return;

	list_append(&io.cmdl, &cmd->le, cmd);
}


void unregister_command(struct command *cmd)
{
	if (!cmd)
		return;

	list_unlink(&cmd->le);
}


void io_stroke_input(char ch)
{
	struct le *le;

	le = io.strokel.head;

	while (le) {
		struct key_stroke *stroke = le->data;
		le = le->next;

		if (stroke->ch == ch) {
			if (stroke->h(ch)) {
				break;
			}
		}
	}
}


void io_command_input(const char *line)
{
	handle_line(line);
}


void register_output_handler(output_h *outputh, void *arg)
{
	io.outputh = outputh;
	io.outputh_arg = arg;
}
