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


struct netprobe_result {
	uint32_t rtt_avg;  /* micro-seconds */

	size_t n_pkt_sent;
	size_t n_pkt_recv;
};

typedef void (netprobe_h)(int err, const struct netprobe_result *result,
			  void *arg);


struct netprobe;

int netprobe_alloc(struct netprobe **npb, const struct sa *turn_srv,
		   int proto, bool secure,
		   const char *turn_username, const char *turn_password,
		   size_t pkt_count, uint32_t pkt_interval_ms,
		   netprobe_h *h, void *arg);
