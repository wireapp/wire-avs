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

#ifndef AVS_LOG_H
#define AVS_LOG_H    1

/* libavs
 *
 * Log
 */

enum avs_log_level {
	LOG_LEVEL_DEBUG = 0,
	LOG_LEVEL_INFO  = 1,
	LOG_LEVEL_WARN  = 2,
	LOG_LEVEL_ERROR = 3,
};

typedef void (avs_log_h)(uint32_t level, const char *msg, void *arg);

struct avs_log {
	struct le le;
	avs_log_h *h;
	void *arg;
};

void avs_log_register_handler(struct avs_log *log);
void avs_log_unregister_handler(struct avs_log *log);
void avs_log_set_min_level(enum avs_log_level level);
enum avs_log_level avs_log_get_min_level(void);
void avs_log_enable_stderr(bool enable);
void avs_vlog(enum avs_log_level level, const char *fmt, va_list ap);
void avs_loglv(enum avs_log_level level, const char *fmt, ...);
void avs_vloglv(enum avs_log_level level, const char *fmt, va_list ap);
void avs_debug(const char *fmt, ...);
void avs_info(const char *fmt, ...);
void avs_warning(const char *fmt, ...);
void avs_error(const char *fmt, ...);

#define debug   avs_debug
#define info    avs_info
#define warning avs_warning
#define error   avs_error

/* anonymous IDs */
#define ANON_ID_LEN     16
#define ANON_CLIENT_LEN  5

const char *anon_id(char *outid, const char *inid);
const char *anon_client(char *outid, const char *inid);


void avs_log_mask_ipaddr(const char *msg);


#endif //#ifndef AVS_LOG_H
