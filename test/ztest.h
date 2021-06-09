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


int create_http_resp(struct http_msg **msgp, const char *str);
int re_main_wait(uint32_t timeout_ms);
int dns_init(struct dnsc **dnscp);
int create_dtls_srtp_context(struct tls **dtlsp, enum tls_keytype cert_type);
int ztest_set_ulimit(unsigned num);
