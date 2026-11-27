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
#include <avs_wcall.h>
#include <pthread.h>
#include <unistd.h>
#include "test_capturer.h"
#include "pgm.h"

enum captype {
	CAP_TYPE_STATIC = 0,
	CAP_TYPE_DYNAMIC
};

static struct {
	enum captype typ;
	uint8_t *buffer;
	uint32_t width;
	uint32_t height;
	uint32_t fps;
	pthread_t thread;
	bool running;
} _capturer;

void draw_octotunnel(uint8_t *buff, uint32_t bw, uint32_t bh);

static uint8_t *draw_byte(uint8_t *buff, uint8_t byte, uint32_t scale)
{
	uint8_t *bpos = buff;
	int32_t bit;
	uint32_t p;

	byte ^= 0xAA;
	for (bit = 7; bit >=0 ; bit--) {
		uint8_t color = (byte & (1 << bit)) ? 255 : 0;

		for (p = 0; p < scale; p++) {
			*bpos = color;
			bpos++;
		}
	}

	return bpos;
}

static void draw_fnum(uint8_t *buff, uint32_t bw, uint32_t fnum)
{
	uint32_t scale = bw / 80;
	uint32_t plen = 6 * 8 * scale;
	uint32_t line;
	uint8_t *bpos = buff;

	bpos = draw_byte(bpos, 0, scale);
	bpos = draw_byte(bpos, (fnum >> 24) & 0xFF, scale);
	bpos = draw_byte(bpos, (fnum >> 16) & 0xFF, scale);
	bpos = draw_byte(bpos, (fnum >> 8) & 0xFF, scale);
	bpos = draw_byte(bpos, fnum & 0xFF, scale);
	bpos = draw_byte(bpos, 0, scale);

	bpos = buff;
	for (line = 0; line < scale; line++) {
		memcpy(bpos, buff, plen);
		bpos += bw;
	}
}

int32_t test_capturer_framenum(uint8_t *buf, uint32_t w)
{
	int32_t fnum = -1;
	uint32_t scale = w / 80;
	uint32_t p, x, l;
	uint32_t lens[8];

	p = 0;
	for (x = 0; x < 8; x += 2) {
		lens[x] = lens[x + 1] = 0;
		while(buf[p] > 200 && lens[x] < scale * 2) {
			p++;
			lens[x]++;
		}

		while(buf[p] < 50 && lens[x + 1] < scale * 2) {
			p++;
			lens[x + 1]++;
		}
	}

	l = lens[0];
	for (x = 1; x < 7; x++) {
		if (l != lens[x]) {
			return -1;
		}
	}

	fnum = 0;
	p = (l * 17) / 2;
	for (x = 0; x < 32; x++) {
		fnum <<= 1;
		if (buf[p] > 128) {
			fnum++;
		}
		p += l;
	}

	fnum ^= 0xAAAAAAAA;
	
	return fnum;
}

/* Sender id stamp: 32 cells in one row, each cell w/40 pixels wide and
 * high, starting at y = w/40 (below the frame number). Bits, MSB first:
 * sync (8) | id (16) | check (8)
 */
#define STAMP_CELLS    32
#define STAMP_SYNC     0xA5
#define STAMP_CHECK(id) ((((id) >> 8) ^ ((id) & 0xFF) ^ 0x5A) & 0xFF)

static uint32_t stamp_bits(uint16_t id)
{
	return ((uint32_t)STAMP_SYNC << 24) | ((uint32_t)id << 8) |
		STAMP_CHECK(id);
}

static void stamp_geometry(int w, int h, uint32_t cell,
			   int *x0, int *x1, int *y0, int *y1)
{
	*x0 = (int)((uint64_t)cell * w / 40);
	*x1 = (int)((uint64_t)(cell + 1) * w / 40);
	*y0 = w / 40;
	*y1 = 2 * (w / 40);
	if (*y1 > h)
		*y1 = h;
}

void test_capturer_stamp_id(struct avs_vidframe *frame, uint16_t id)
{
	uint32_t bits = stamp_bits(id);
	uint32_t c;
	int y;

	if (!frame || frame->type != AVS_VIDFRAME_I420 ||
	    frame->w < 80 || frame->h < 8)
		return;

	for (c = 0; c < STAMP_CELLS; c++) {
		uint8_t color = (bits & (1u << (31 - c))) ? 255 : 0;
		int x0, x1, y0, y1;

		stamp_geometry(frame->w, frame->h, c, &x0, &x1, &y0, &y1);
		for (y = y0; y < y1; y++) {
			memset(frame->y + y * frame->ys + x0, color, x1 - x0);
		}
		/* neutral chroma */
		for (y = y0 / 2; y < (y1 + 1) / 2; y++) {
			memset(frame->u + y * frame->us + x0 / 2, 128,
			       (x1 - x0 + 1) / 2);
			memset(frame->v + y * frame->vs + x0 / 2, 128,
			       (x1 - x0 + 1) / 2);
		}
	}
}

int32_t test_capturer_read_id(const struct avs_vidframe *frame)
{
	uint32_t bits = 0;
	uint32_t c;
	uint16_t id;

	if (!frame || frame->type != AVS_VIDFRAME_I420 ||
	    frame->w < 80 || frame->h < 8)
		return -1;

	for (c = 0; c < STAMP_CELLS; c++) {
		int x0, x1, y0, y1, x, y;
		uint32_t sum = 0, n = 0;
		int mx, my;

		stamp_geometry(frame->w, frame->h, c, &x0, &x1, &y0, &y1);

		/* average the inner half of the cell */
		mx = (x1 - x0) / 4;
		my = (y1 - y0) / 4;
		for (y = y0 + my; y < y1 - my; y++) {
			for (x = x0 + mx; x < x1 - mx; x++) {
				sum += frame->y[y * frame->ys + x];
				n++;
			}
		}
		bits <<= 1;
		if (n && sum / n > 128)
			bits |= 1;
	}

	if ((bits >> 24) != STAMP_SYNC)
		return -1;

	id = (bits >> 8) & 0xFFFF;
	if ((bits & 0xFF) != STAMP_CHECK(id))
		return -1;

	return id;
}

static void *frame_thread(void *arg)
{
	uint32_t ysz = _capturer.width * _capturer.height;
	uint32_t usz = ysz / 4;
	uint32_t fsz = ysz * 3 / 2;
	uint32_t delay = 1000000 / _capturer.fps;
	uint32_t fnum = 0;
	uint64_t first, current;

	if (!_capturer.buffer) {
		_capturer.buffer = malloc(fsz);
		if (!_capturer.buffer) {
			return NULL;
		}
		memset(_capturer.buffer, 128, fsz);
	}


	struct avs_vidframe frame;

	memset(&frame, 0, sizeof(frame));

	frame.type = AVS_VIDFRAME_I420;
	frame.w = _capturer.width;
	frame.h = _capturer.height;
	frame.y = _capturer.buffer;
	frame.u = frame.y + ysz;
	frame.v = frame.u + usz;
	frame.ys = _capturer.width;
	frame.us = frame.ys / 2;
	frame.vs = frame.us;

	first = tmr_jiffies();

	while (_capturer.running) {
		struct timespec ts;

		ts.tv_sec = 0;
		ts.tv_nsec = delay * 1000;
		
		nanosleep(&ts, NULL);

		if (_capturer.typ == CAP_TYPE_DYNAMIC) {
			draw_octotunnel(_capturer.buffer, _capturer.width, _capturer.height);
			draw_fnum(_capturer.buffer, _capturer.width, fnum++);
		}

		current = tmr_jiffies();
		frame.ts = (uint32_t)(current - first);

		wcall_handle_frame(&frame);
	}
	
	free(_capturer.buffer);

	return NULL;
}

void test_capturer_init(void)
{
	memset(&_capturer, 0, sizeof(_capturer));
}

void test_capturer_start_static(const char *fname, uint32_t fps)
{
	int err =0;
	struct pgm pgm;
	int fsize;

	if (_capturer.running) {
		return;
	}

	memset(&_capturer, 0, sizeof(_capturer));
	_capturer.typ = CAP_TYPE_STATIC;
	pgm_load(&pgm, fname);
	_capturer.width = pgm.w;
	_capturer.height = pgm.h;
	_capturer.fps = fps;

	fsize = pgm.w * pgm.h;
	_capturer.buffer = malloc(fsize * 3 / 2);
	memcpy(_capturer.buffer, pgm.buf, fsize);
	memset(_capturer.buffer + fsize, 128, fsize / 2);

	_capturer.running = true;
	err = pthread_create(&_capturer.thread, NULL, frame_thread, NULL);
	if (err) {
		_capturer.running = false;
		return;
	}
}

void test_capturer_start_dynamic(uint32_t w, uint32_t h, uint32_t fps)
{
	int err =0;

	if (_capturer.running) {
		return;
	}

	memset(&_capturer, 0, sizeof(_capturer));
	_capturer.typ = CAP_TYPE_DYNAMIC;
	_capturer.width = w;
	_capturer.height = h;
	_capturer.fps = fps;
	_capturer.running = true;
	err = pthread_create(&_capturer.thread, NULL, frame_thread, NULL);
	if (err) {
		_capturer.running = false;
		return;
	}
	
}

void test_capturer_stop(void)
{
	_capturer.running = false;
	pthread_join(_capturer.thread, NULL);
}



