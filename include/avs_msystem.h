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


struct msystem;


struct msystem_config {
	bool data_channel;
};

int msystem_get(struct msystem **msysp, const char *msysname,
		enum tls_keytype cert_type, struct msystem_config *config);
bool msystem_is_initialized(struct msystem *msys);
struct tls *msystem_dtls(struct msystem *msys);
struct list *msystem_aucodecl(struct msystem *msys);
struct list *msystem_vidcodecl(struct msystem *msys);
struct list *msystem_flows(struct msystem *msys);
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
void msystem_enable_cbr(struct msystem *msys, bool enable);
bool msystem_have_cbr(const struct msystem *msys);
void msystem_set_ifname(struct msystem *msys, const char *ifname);
int  msystem_enable_datachannel(struct msystem *msys, bool enable);
bool msystem_have_datachannel(const struct msystem *msys);
struct call_config;
int  msystem_set_call_config(struct msystem *msys, struct call_config *cfg);
struct call_config *msystem_get_call_config(const struct msystem *msys);
int msystem_update_conf_parts(struct list *partl);

#define MAX_TURN_SERVERS 8

struct msystem_turn_server {
	struct sa srv;
	char user[128];
	char pass[128];
};

size_t msystem_get_turn_servers(struct msystem_turn_server **turnvp,
			     struct msystem *msys);


#endif
