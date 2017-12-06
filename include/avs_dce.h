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


struct dce;
struct dce_channel;

typedef int  (dce_send_h)(uint8_t *pkt, size_t len, void *arg);
typedef void (dce_estab_h)(void *arg);
typedef void (dce_open_chan_h)(int sid,
			       const char *label, const char *protocol,
			       void *arg);
typedef void (dce_close_chan_h)(int sid,
				const char *label, const char *protocol,
				void *arg);
typedef void (dce_data_h)(int sid, uint8_t *data, size_t len, void *arg);

int  dce_init(void);
void dce_close(void);


int  dce_alloc(struct dce **dcep,
	       dce_send_h *sendh,
	       dce_estab_h *estabh,
	       void *arg);

int  dce_channel_alloc(struct dce_channel **chp,
		       struct dce *dce,
		       const char *label,
		       const char *protocol,
		       dce_estab_h *estabh,
		       dce_open_chan_h *openh,
		       dce_close_chan_h *closeh,
		       dce_data_h *datah,
		       void *arg);

int  dce_connect(struct dce *dce, bool dtls_role_active);
int  dce_open_chan(struct dce *dce, struct dce_channel *ch);
int  dce_close_chan(struct dce *dce, struct dce_channel *ch);
int  dce_status(struct re_printf *pf, struct dce *dce);
int  dce_send(struct dce *dce, struct dce_channel *ch, const void *data, size_t len);
void dce_recv_pkt(struct dce *dce, const uint8_t *pkt, size_t len);
bool dce_snd_dry(struct dce *dce);
bool dce_is_chan_open(const struct dce_channel *ch);
