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

#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>


static void print_udp_bufsize(int af)
{
	struct udp_sock *us = NULL;
	struct sa laddr;
	int fd;
	int sndbuf;
	int rcvbuf;
	socklen_t sndbuf_len = sizeof(sndbuf);
	socklen_t rcvbuf_len = sizeof(rcvbuf);
	int err;

	sa_init(&laddr, af);

	err = udp_listen(&us, &laddr, NULL, NULL);
	if (err) {
		warning("test: udp_listen failed (%m)\n", err);
		goto out;
	}

	fd = udp_sock_fd(us, af);
	if (-1 == fd) {
		warning("test: no udp socket fd\n");
		goto out;
	}

	getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &sndbuf_len);
	getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &rcvbuf_len);

	re_printf("test: udp socket buffersize for %-8s:"
		  "    SO_SNDBUF=%d SO_RCVBUF=%d\n",
		  net_af2name(af),
		  sndbuf, rcvbuf);

 out:
	mem_deref(us);
}


TEST(version, print)
{
	log_set_min_level(LOG_LEVEL_INFO);
	log_enable_stderr(true);

	avs_print_versions();

	avs_print_network();

	print_udp_bufsize(AF_INET);
	print_udp_bufsize(AF_INET6);
}
