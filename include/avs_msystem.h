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

#ifndef AVS_MSYSTEM_H
#define AVS_MSYSTEM_H


#include <pthread.h>


struct msystem;


struct msystem_config {
	bool data_channel;
};

struct msystem_proxy {
	char *host;
	int port;
};

typedef void (msystem_activate_h)(void *arg);
typedef void (msystem_mute_h)(bool muted, void *arg);

void msystem_set_env(int env);
int msystem_get_env(void);

int msystem_get(struct msystem **msysp, const char *msysname,
		struct msystem_config *config,
		msystem_mute_h *muteh,
		void *arg);
void msystem_unregister_listener(void *arg);
bool msystem_is_initialized(struct msystem *msys);
struct msystem *msystem_instance(void);
struct tls *msystem_dtls(struct msystem *msys);
struct list *msystem_aucodecl(struct msystem *msys);
struct list *msystem_vidcodecl(struct msystem *msys);
bool msystem_get_loopback(struct msystem *msys);
bool msystem_get_privacy(struct msystem *msys);
const char *msystem_get_interface(struct msystem *msys);
void msystem_start(struct msystem *msys);
void msystem_stop(struct msystem *msys);
bool msystem_is_started(struct msystem *msys);
int  msystem_push(struct msystem *msys, int op, void *arg);
bool msystem_is_using_voe(struct msystem *msys);
void msystem_enable_loopback(struct msystem *msys, bool enable);
void msystem_enable_privacy(struct msystem *msys, bool enable);
void msystem_enable_kase(struct msystem *msys, bool enable);
bool msystem_have_kase(const struct msystem *msys);
void msystem_set_ifname(struct msystem *msys, const char *ifname);
int  msystem_enable_datachannel(struct msystem *msys, bool enable);
bool msystem_have_datachannel(const struct msystem *msys);
int msystem_update_conf_parts(struct list *partl);
struct dnsc *msystem_dnsc(void);
bool msystem_audio_is_activated(void);
void msystem_audio_set_activated(bool activated);


#define MAX_TURN_SERVERS 16


void msystem_set_tid(struct msystem *msys, pthread_t tid);
void msystem_enter(struct msystem *msys);
void msystem_leave(struct msystem *msys);

void msystem_set_auplay(const char *dev);
void msystem_stop_silencing(void);

int msystem_set_proxy(const char *host, int port);
struct msystem_proxy *msystem_get_proxy(void);


/*
 * Device
 */

int  msystem_start_mic_file_playout(const char fileNameUTF8[1024], int fs);
void msystem_stop_mic_file_playout(void);
void msystem_set_bitrate(int rate_bps);
void msystem_set_packet_size(int packet_size_ms);
bool msystem_get_muted(void);
void msystem_set_muted(bool muted);


#endif
