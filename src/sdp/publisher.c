/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

/* Publisher SDP is browser-owned. Preserve its media sections and attributes. */
#include <re.h>
#include <avs.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *line_end(const char *line, const char *end)
{
	const char *next = memchr(line, '\n', (size_t)(end - line));
	return next ? next + 1 : end;
}

static int payload(const char *line, const char *end, const char *prefix)
{
	size_t n = strlen(prefix);
	char *tail;
	unsigned long pt;
	if ((size_t)(end - line) <= n || memcmp(line, prefix, n)) return -1;
	if (line[n] < '0' || line[n] > '9') return -1;
	pt = strtoul(line + n, &tail, 10);
	if (tail >= end || (*tail != ' ' && *tail != '\t') || pt > 127) return -1;
	return (int)pt;
}

/* Replace existing cbr values, preserving all other fmtp parameters. */
static int write_cbr(struct mbuf *out, const char *line, const char *end)
{
	const char *token = strpbrk(line, " \t"), *cursor = line;
	bool found = false;
	int err = 0;
	while (token && token < end) {
		const char *next = memchr(token, ';', (size_t)(end - token));
		const char *value_end = next ? next : end;
		while (token < value_end && (*token == ' ' || *token == '\t')) ++token;
		if (value_end - token >= 4 && !memcmp(token, "cbr=", 4)) {
			const char *value = token + 4, *last = value_end;
			while (last > value && (last[-1] == ' ' || last[-1] == '\t')) --last;
			err |= mbuf_write_mem(out, (const uint8_t *)cursor, (size_t)(value - cursor));
			err |= mbuf_write_str(out, "1");
			cursor = last;
			found = true;
		}
		token = next ? next + 1 : NULL;
	}
	err |= mbuf_write_mem(out, (const uint8_t *)cursor, (size_t)(end - cursor));
	if (!found) err |= mbuf_write_str(out, ";cbr=1");
	return err;
}

int sdp_publisher_local(char **result, const char *sdp, bool audio_cbr)
{
	const char *section, *end;
	struct mbuf *out;
	int err = 0;
	if (!result || !sdp) return EINVAL;
	if (!audio_cbr) return str_dup(result, sdp);
	out = mbuf_alloc(strlen(sdp) + 128);
	if (!out) return ENOMEM;
	end = sdp + strlen(sdp);
	for (section = sdp; section < end;) {
		const char *next_section = line_end(section, end), *line;
		bool opus[128] = {false}, fmtp[128] = {false};
		bool audio = end - section >= 8 && !memcmp(section, "m=audio ", 8);
		while (next_section < end && strncmp(next_section, "m=", 2))
			next_section = line_end(next_section, end);
		for (line = section; audio && line < next_section; line = line_end(line, next_section)) {
			const char *stop = line_end(line, next_section);
			int pt = payload(line, stop, "a=rtpmap:");
			if (pt >= 0) {
				const char *codec = strpbrk(line, " \t");
				while (codec && codec < stop && (*codec == ' ' || *codec == '\t')) ++codec;
				if (codec && stop - codec >= 11 && !strncasecmp(codec, "opus/48000/", 11)) opus[pt] = true;
			}
		}
		for (line = section; !err && line < next_section; line = line_end(line, next_section)) {
			const char *stop = line_end(line, next_section), *content = stop;
			int pt = audio ? payload(line, stop, "a=fmtp:") : -1;
			if (pt >= 0 && opus[pt]) {
				while (content > line && (content[-1] == '\r' || content[-1] == '\n')) --content;
				err = write_cbr(out, line, content);
				if (!err) err = mbuf_write_mem(out, (const uint8_t *)content, (size_t)(stop - content));
				fmtp[pt] = true;
			}
			else err = mbuf_write_mem(out, (const uint8_t *)line, (size_t)(stop - line));
		}
		for (int pt = 0; !err && pt < 128; ++pt)
			if (opus[pt] && !fmtp[pt]) {
				if (out->end && out->buf[out->end - 1] != '\n') err = mbuf_write_str(out, "\r\n");
				if (!err) err = mbuf_printf(out, "a=fmtp:%d cbr=1\r\n", pt);
			}
		section = next_section;
	}
	out->pos = 0;
	if (!err) err = mbuf_strdup(out, result, out->end);
	mem_deref(out);
	return err;
}
