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

/*
 * Procedural "talking head" test pattern.
 *
 * Unlike the octotunnel (full-frame, hard 0/255 edges, rotating and
 * zooming, a worst case for VP8) this tries to cost an encoder roughly
 * what a webcam call does:
 *
 *  - static detail: textured wall, a bookshelf with hard-edged books,
 *    a window and a plant (costs bits on keyframes)
 *  - moving texture: hair, shirt and skin carry a texture that moves
 *    with the body at sub-pixel offsets (costs bits on every frame)
 *  - motion: sway and breathing, head turns and nods, a jaw and mouth
 *    driven by a fake speech envelope, blinks and an occasional hand
 *    gesture
 *  - camera: slow auto-exposure drift and luma sensor noise
 *
 * Everything is deterministic (fixed seed, driven by the frame number)
 * so runs are comparable.
 *
 * Output is a full I420 frame (Y, U and V planes, contiguous).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TH_PI         3.14159265f
#define TH_NOISE_LEN  65536
#define TH_TEX_BITS   8
#define TH_TEX_SIZE   (1 << TH_TEX_BITS)
#define TH_TEX_MASK   (TH_TEX_SIZE - 1)
#define TH_SEED       0x5eed1234u

void draw_talkinghead(uint8_t *buff, uint32_t w, uint32_t h,
		      uint32_t fnum, uint32_t fps);

struct ellipse {
	float cx, cy;   /* centre, full resolution pixels */
	float rx, ry;   /* radii, full resolution pixels */
	float edge;     /* soft edge width, full resolution pixels */
};

struct color {
	float y, u, v;
	float gx, gy;   /* luma shading: y += gx * nx + gy * ny */
	float tamp;     /* luma texture amplitude (0: none) */
	float tscale;   /* texture pixels per frame pixel */
	float tsx;      /* texture x stretch, e.g. hair strands */
	float tox, toy; /* texture offset, texture pixels (fabric folds) */
};

/* A smoothly moving random value: picks a new target every so often */
struct wander {
	float cur;
	float target;
	uint32_t next;
};

static struct {
	uint32_t w, h;
	uint8_t *bg;           /* background I420 frame */
	int8_t *noise;
	int8_t *tex;           /* tileable texture, TH_TEX_SIZE^2 */

	uint32_t rng;

	/* speech envelope state */
	uint32_t seg_end;      /* frame where the current segment ends */
	bool talking;
	float syl_hz;
	float syl_phase;

	/* blink state */
	uint32_t blink_at;     /* frame where the next blink starts */

	/* head pose */
	struct wander yaw;     /* -1 .. 1, turn left/right */
	struct wander pitch;   /* -1 .. 1, nod */

	/* camera jitter (desk bumps while typing) */
	uint32_t jit_at;
	uint32_t jit_len;
	float jit_x, jit_y;

	/* hand gesture */
	uint32_t gest_at;      /* frame where the next gesture starts */
	uint32_t gest_len;
	bool gest_left;
} th;

static uint32_t th_rand(void)
{
	/* xorshift32 */
	uint32_t x = th.rng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	th.rng = x;

	return x;
}

/* uniform in [lo, hi) */
static float th_randf(float lo, float hi)
{
	return lo + (hi - lo) * (float)(th_rand() & 0xffffff) / 16777216.0f;
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static uint8_t to_u8(float v)
{
	return (uint8_t)(clampf(v, 0.0f, 255.0f) + 0.5f);
}

static float smoothstep(float e0, float e1, float x)
{
	float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);

	return t * t * (3.0f - 2.0f * t);
}

static int ifloor(float v)
{
	int i = (int)v;

	return (v < (float)i) ? i - 1 : i;
}

/* Bilinear texture sample, wrapping, roughly in [-1, 1] */
static float tex_sample(float u, float v)
{
	int iu = ifloor(u), iv = ifloor(v);
	float fu = u - (float)iu, fv = v - (float)iv;
	int u0 = iu & TH_TEX_MASK, u1 = (iu + 1) & TH_TEX_MASK;
	int v0 = iv & TH_TEX_MASK, v1 = (iv + 1) & TH_TEX_MASK;
	const int8_t *t = th.tex;
	float a = t[(v0 << TH_TEX_BITS) + u0];
	float b = t[(v0 << TH_TEX_BITS) + u1];
	float c = t[(v1 << TH_TEX_BITS) + u0];
	float d = t[(v1 << TH_TEX_BITS) + u1];
	float top = a + fu * (b - a);
	float bot = c + fu * (d - c);

	return (top + fv * (bot - top)) / 127.0f;
}

/*
 * Alpha blend a soft-edged ellipse into one plane. sub is the plane
 * subsampling (1 for Y, 2 for U/V); val is the plane value at the centre
 * and gx/gy a linear gradient over the normalised ellipse coordinates.
 * If tamp is set, a texture anchored to the ellipse centre is added, so
 * it moves with the ellipse.
 */
static void plane_ellipse(uint8_t *p, uint32_t pw, uint32_t ph, uint32_t sub,
			  const struct ellipse *e,
			  float val, float gx, float gy,
			  float tamp, float tscale, float tsx,
			  float tox, float toy)
{
	float cx = e->cx / sub, cy = e->cy / sub;
	float rx = e->rx / sub, ry = e->ry / sub;
	float edge = e->edge / sub;
	float rmin = rx < ry ? rx : ry;
	float ts = tscale * (float)sub;
	int x0, x1, y0, y1, x, y;

	if (rx < 0.5f || ry < 0.5f)
		return;
	if (edge < 0.5f)
		edge = 0.5f;

	{
		float fx0 = floorf(cx - rx - edge);
		float fx1 = ceilf(cx + rx + edge);
		float fy0 = floorf(cy - ry - edge);
		float fy1 = ceilf(cy + ry + edge);

		x0 = (int)fx0;
		x1 = (int)fx1;
		y0 = (int)fy0;
		y1 = (int)fy1;
	}
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > (int)pw) x1 = (int)pw;
	if (y1 > (int)ph) y1 = (int)ph;

	for (y = y0; y < y1; y++) {
		float dy = (float)y + 0.5f - cy;
		float ny = dy / ry;
		uint8_t *row = p + (size_t)y * pw;

		for (x = x0; x < x1; x++) {
			float dx = (float)x + 0.5f - cx;
			float nx = dx / rx;
			float d = sqrtf(nx * nx + ny * ny);
			/* approx. distance outside the boundary in pixels */
			float dpx = (d - 1.0f) * rmin;
			float a = 1.0f - smoothstep(-edge * 0.5f,
						    edge * 0.5f, dpx);
			float v;

			if (a <= 0.0f)
				continue;

			v = val + gx * nx + gy * ny;
			if (tamp != 0.0f) {
				v += tamp * tex_sample(dx * ts * tsx + tox
						       + 1000.0f,
						       dy * ts + toy + 1000.0f);
			}
			row[x] = to_u8(row[x] + a * (v - row[x]));
		}
	}
}

static void draw_ellipse(uint8_t *buff, uint32_t w, uint32_t h,
			 const struct ellipse *e, const struct color *c)
{
	uint8_t *py = buff;
	uint8_t *pu = py + w * h;
	uint8_t *pv = pu + (w / 2) * (h / 2);
	float tsx = c->tsx > 0.0f ? c->tsx : 1.0f;

	plane_ellipse(py, w, h, 1, e, c->y, c->gx, c->gy,
		      c->tamp, c->tscale, tsx, c->tox, c->toy);
	plane_ellipse(pu, w / 2, h / 2, 2, e, c->u, 0.0f, 0.0f,
		      0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	plane_ellipse(pv, w / 2, h / 2, 2, e, c->v, 0.0f, 0.0f,
		      0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
}

/* Hard-edged rectangle, full resolution coordinates, with a texture */
static void draw_rect(uint8_t *buff, uint32_t w, uint32_t h,
		      int x0, int y0, int x1, int y1,
		      float yv, float uv, float vv, float tamp)
{
	uint8_t *py = buff;
	uint8_t *pu = py + w * h;
	uint8_t *pv = pu + (w / 2) * (h / 2);
	int x, y;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > (int)w) x1 = (int)w;
	if (y1 > (int)h) y1 = (int)h;

	for (y = y0; y < y1; y++) {
		for (x = x0; x < x1; x++) {
			float t = tamp * tex_sample((float)x * 0.5f,
						    (float)y * 3.0f);

			py[y * w + x] = to_u8(yv + t);
		}
	}
	for (y = y0 / 2; y < y1 / 2; y++) {
		for (x = x0 / 2; x < x1 / 2; x++) {
			pu[y * (w / 2) + x] = to_u8(uv);
			pv[y * (w / 2) + x] = to_u8(vv);
		}
	}
}

/* Tileable value noise, a few octaves, into th.tex */
static void make_texture(void)
{
	float *acc = calloc(TH_TEX_SIZE * TH_TEX_SIZE, sizeof(float));
	float lat[64 * 64];
	float amp = 1.0f, maxv = 0.0f;
	int cells, o, x, y;

	if (!acc)
		return;

	for (o = 0, cells = 4; o < 5; o++, cells *= 2, amp *= 0.6f) {
		int step = TH_TEX_SIZE / cells;

		for (x = 0; x < cells * cells; x++)
			lat[x] = th_randf(-1.0f, 1.0f);

		for (y = 0; y < TH_TEX_SIZE; y++) {
			int cy = y / step;
			float fy = (float)(y % step) / (float)step;

			fy = fy * fy * (3.0f - 2.0f * fy);
			for (x = 0; x < TH_TEX_SIZE; x++) {
				int cx = x / step;
				float fx = (float)(x % step) / (float)step;
				int cx1 = (cx + 1) % cells;
				int cy1 = (cy + 1) % cells;
				float a, b, c, d;

				fx = fx * fx * (3.0f - 2.0f * fx);
				a = lat[cy * cells + cx];
				b = lat[cy * cells + cx1];
				c = lat[cy1 * cells + cx];
				d = lat[cy1 * cells + cx1];
				acc[y * TH_TEX_SIZE + x] += amp *
					((a + fx * (b - a)) * (1.0f - fy) +
					 (c + fx * (d - c)) * fy);
			}
		}
	}

	for (x = 0; x < TH_TEX_SIZE * TH_TEX_SIZE; x++) {
		float a = fabsf(acc[x]);

		if (a > maxv)
			maxv = a;
	}
	if (maxv <= 0.0f)
		maxv = 1.0f;
	for (x = 0; x < TH_TEX_SIZE * TH_TEX_SIZE; x++)
		th.tex[x] = (int8_t)(127.0f * acc[x] / maxv);

	free(acc);
}

static void make_background(void)
{
	uint32_t w = th.w, h = th.h;
	uint8_t *py = th.bg;
	uint8_t *pu = py + w * h;
	uint8_t *pv = pu + (w / 2) * (h / 2);
	float s = (float)w / 640.0f;
	uint32_t x, y;
	int row, sx0, sx1, sy0, sy1, shelf_h;

	/* wall: vertical gradient, soft vignette and a plaster texture */
	for (y = 0; y < h; y++) {
		float fy = (float)y / (float)h;

		for (x = 0; x < w; x++) {
			float fx = (float)x / (float)w - 0.5f;
			float t = tex_sample((float)x * 2.5f / s,
					     (float)y * 2.5f / s);
			float t2 = tex_sample((float)x * 0.15f / s + 77.0f,
					      (float)y * 0.15f / s + 31.0f);

			py[y * w + x] = to_u8(150.0f - 35.0f * fy
					      - 60.0f * fx * fx
					      + 12.0f * t + 12.0f * t2);
		}
	}
	for (y = 0; y < h / 2; y++) {
		for (x = 0; x < w / 2; x++) {
			float fx = (float)x / (float)(w / 2);

			/* beige, slightly warmer to the right */
			pu[y * (w / 2) + x] = to_u8(120.0f - 4.0f * fx);
			pv[y * (w / 2) + x] = to_u8(134.0f + 4.0f * fx);
		}
	}

	/* window light, top left */
	{
		struct ellipse e = {0.16f * w, 0.22f * h,
				    0.13f * w, 0.20f * h, 30.0f * s};
		struct color c = {205.0f, 124.0f, 128.0f, 0.0f, -10.0f,
				  6.0f, 0.4f / s, 1.0f};

		draw_ellipse(th.bg, w, h, &e, &c);
	}

	/* bookshelf, right: two shelves of books with random widths,
	 * heights and colours
	 */
	sx0 = (int)(0.66f * w);
	sx1 = (int)(0.99f * w);
	sy0 = (int)(0.10f * h);
	sy1 = (int)(0.62f * h);
	shelf_h = (sy1 - sy0) / 2;
	draw_rect(th.bg, w, h, sx0 - (int)(6 * s), sy0 - (int)(6 * s),
		  sx1 + (int)(6 * s), sy1 + (int)(6 * s),
		  70.0f, 118.0f, 140.0f, 10.0f);
	for (row = 0; row < 2; row++) {
		int by1 = sy0 + (row + 1) * shelf_h - (int)(4 * s);
		int bx = sx0;

		draw_rect(th.bg, w, h, sx0, by1, sx1, by1 + (int)(5 * s),
			  95.0f, 116.0f, 142.0f, 6.0f);
		while (bx < sx1) {
			int bw = (int)(th_randf(5.0f, 14.0f) * s);
			int bh = (int)(th_randf(0.65f, 0.95f) * shelf_h);
			static const float pal[][3] = {
				{ 60.0f, 110.0f, 170.0f},  /* dark red */
				{ 45.0f, 150.0f, 118.0f},  /* navy */
				{ 70.0f, 115.0f, 105.0f},  /* green */
				{160.0f,  90.0f, 150.0f},  /* mustard */
				{200.0f, 120.0f, 132.0f},  /* cream */
				{ 30.0f, 128.0f, 128.0f},  /* black */
				{ 80.0f, 115.0f, 145.0f},  /* brown */
				{100.0f, 140.0f, 100.0f},  /* teal */
				{140.0f,  90.0f, 175.0f},  /* orange */
				{120.0f, 128.0f, 128.0f},  /* grey */
			};
			const float *pc = pal[th_rand() % 10];
			float by = pc[0] + th_randf(-15.0f, 15.0f);
			float bu = pc[1] + th_randf(-6.0f, 6.0f);
			float bv = pc[2] + th_randf(-6.0f, 6.0f);

			if (bx + bw > sx1)
				bw = sx1 - bx;
			draw_rect(th.bg, w, h, bx, by1 - bh, bx + bw, by1,
				  by, bu, bv, 12.0f);
			/* spine label */
			if (bw > (int)(7 * s)) {
				draw_rect(th.bg, w, h,
					  bx + 1, by1 - bh + bh / 4,
					  bx + bw - 1, by1 - bh + bh / 4
					  + (int)(5 * s),
					  by > 120.0f ? 40.0f : 210.0f,
					  bu, bv, 0.0f);
			}
			bx += bw + (int)(th_randf(0.0f, 2.0f) * s);
		}
	}

	/* framed poster, between window and shelf */
	draw_rect(th.bg, w, h, (int)(0.34f * w), (int)(0.04f * h),
		  (int)(0.52f * w), (int)(0.20f * h),
		  45.0f, 128.0f, 128.0f, 0.0f);
	draw_rect(th.bg, w, h, (int)(0.34f * w) + (int)(4 * s),
		  (int)(0.04f * h) + (int)(4 * s),
		  (int)(0.52f * w) - (int)(4 * s),
		  (int)(0.20f * h) - (int)(4 * s),
		  150.0f, 140.0f, 110.0f, 40.0f);
	for (row = 0; row < 3; row++) {
		struct ellipse e = {(0.38f + 0.05f * row) * w,
				    (0.10f + 0.02f * row) * h,
				    0.025f * w, 0.035f * h, 2.0f * s};
		struct color c = {60.0f + 50.0f * row,
				  100.0f + 25.0f * row, 170.0f - 30.0f * row,
				  0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

		draw_ellipse(th.bg, w, h, &e, &c);
	}

	/* desk edge at the bottom, wood grain */
	draw_rect(th.bg, w, h, 0, (int)(0.90f * h), (int)w, (int)h,
		  110.0f, 110.0f, 150.0f, 28.0f);

	/* plant, bottom left */
	{
		struct ellipse e = {0.10f * w, 0.62f * h,
				    0.08f * w, 0.16f * h, 6.0f * s};
		struct color c = {75.0f, 108.0f, 112.0f, 10.0f, 12.0f,
				  45.0f, 4.0f / s, 1.0f};

		draw_ellipse(th.bg, w, h, &e, &c);
	}
	{
		struct ellipse e = {0.10f * w, 0.84f * h,
				    0.05f * w, 0.08f * h, 3.0f * s};
		struct color c = {95.0f, 115.0f, 150.0f, 0.0f, 10.0f,
				  8.0f, 1.0f / s, 1.0f};

		draw_ellipse(th.bg, w, h, &e, &c);
	}
}

static void wander_init(struct wander *wd)
{
	wd->cur = 0.0f;
	wd->target = 0.0f;
	wd->next = 0;
}

static float wander_step(struct wander *wd, uint32_t fnum, uint32_t fps,
			 float lo, float hi, float tmin, float tmax,
			 float rate)
{
	if (fnum >= wd->next) {
		wd->target = th_randf(lo, hi);
		wd->next = fnum + (uint32_t)(th_randf(tmin, tmax) * fps);
	}
	wd->cur += (wd->target - wd->cur) * rate;

	return wd->cur;
}

static int th_init(uint32_t w, uint32_t h)
{
	uint32_t i;

	free(th.bg);
	free(th.noise);
	free(th.tex);
	memset(&th, 0, sizeof(th));

	th.w = w;
	th.h = h;
	th.rng = TH_SEED;

	th.bg = malloc((size_t)w * h * 3 / 2);
	th.noise = malloc(TH_NOISE_LEN);
	th.tex = calloc(TH_TEX_SIZE * TH_TEX_SIZE, 1);
	if (!th.bg || !th.noise || !th.tex) {
		free(th.bg);
		free(th.noise);
		free(th.tex);
		th.bg = NULL;
		th.noise = NULL;
		th.tex = NULL;
		th.w = th.h = 0;
		return -1;
	}

	/* triangular noise in [-6, 6], sigma ~2.9 (indoor webcam) */
	for (i = 0; i < TH_NOISE_LEN; i++) {
		int a = (int)(th_rand() % 7);
		int b = (int)(th_rand() % 7);

		th.noise[i] = (int8_t)(a - b);
	}

	make_texture();
	make_background();

	th.talking = false;
	th.seg_end = 0;
	th.blink_at = 0;
	wander_init(&th.yaw);
	wander_init(&th.pitch);
	th.gest_at = 0;
	th.jit_at = 0;

	return 0;
}

/* Fake speech envelope in [0, 1]: talk spurts of syllables at 3-5 Hz,
 * separated by pauses of 0.5-2 s.
 */
static float speech_env(uint32_t fnum, uint32_t fps)
{
	float dt = 1.0f / (float)fps;
	float env;

	if (fnum >= th.seg_end) {
		th.talking = !th.talking;
		if (th.talking) {
			th.seg_end = fnum + (uint32_t)(th_randf(1.5f, 5.0f)
						       * fps);
			th.syl_hz = th_randf(3.0f, 5.0f);
		}
		else {
			th.seg_end = fnum + (uint32_t)(th_randf(0.5f, 2.0f)
						       * fps);
		}
	}

	if (!th.talking)
		return 0.0f;

	/* drift the syllable rate a bit so it isn't a pure tone */
	th.syl_hz = clampf(th.syl_hz + th_randf(-0.3f, 0.3f), 3.0f, 5.0f);
	th.syl_phase += 2.0f * TH_PI * th.syl_hz * dt;
	if (th.syl_phase > 2.0f * TH_PI)
		th.syl_phase -= 2.0f * TH_PI;

	env = sinf(th.syl_phase);
	env = env > 0.0f ? env : 0.0f;

	return env * th_randf(0.6f, 1.0f);
}

/* Eye openness in [0, 1]: a ~200 ms blink every 3-6 s */
static float eye_open(uint32_t fnum, uint32_t fps)
{
	uint32_t blen = fps / 5 ? fps / 5 : 1;
	float p;

	if (th.blink_at == 0)
		th.blink_at = fnum + (uint32_t)(th_randf(1.0f, 3.0f) * fps);

	if (fnum < th.blink_at)
		return 1.0f;

	if (fnum >= th.blink_at + blen) {
		th.blink_at = fnum + (uint32_t)(th_randf(3.0f, 6.0f) * fps);
		return 1.0f;
	}

	p = (float)(fnum - th.blink_at) / (float)blen;   /* 0..1 */

	return 0.1f + 0.9f * fabsf(2.0f * p - 1.0f);
}

/* Hand gesture progress: returns 0 when no gesture, else 0..1 lift, and
 * sets *wave to a side to side offset in [-1, 1].
 */
static float gesture(uint32_t fnum, uint32_t fps, float *wave)
{
	float p, lift;

	*wave = 0.0f;

	if (th.gest_at == 0) {
		th.gest_at = fnum + (uint32_t)(th_randf(3.0f, 6.0f) * fps);
		th.gest_len = (uint32_t)(th_randf(2.0f, 3.5f) * fps);
		th.gest_left = th_rand() & 1;
	}

	if (fnum < th.gest_at)
		return 0.0f;

	if (fnum >= th.gest_at + th.gest_len) {
		th.gest_at = fnum + (uint32_t)(th_randf(3.0f, 7.0f) * fps);
		th.gest_len = (uint32_t)(th_randf(2.0f, 3.5f) * fps);
		th.gest_left = th_rand() & 1;
		return 0.0f;
	}

	p = (float)(fnum - th.gest_at) / (float)th.gest_len;
	/* rise over the first 25%, hold, fall over the last 25% */
	lift = smoothstep(0.0f, 0.25f, p) * (1.0f - smoothstep(0.75f, 1.0f, p));
	*wave = sinf(2.0f * TH_PI * 1.8f * (float)(fnum - th.gest_at)
		     / (float)fps);

	return lift;
}

/* Camera jitter: bursts of 1-3 s every 3-8 s where the image shakes by
 * up to a couple of pixels at sub-pixel offsets, as a laptop camera does
 * while typing. Between bursts the camera settles back to rest.
 */
static void camera_jitter(uint32_t fnum, uint32_t fps, float s,
			  float *jx, float *jy)
{
	bool active = false;

	if (th.jit_at == 0) {
		th.jit_at = fnum + (uint32_t)(th_randf(2.0f, 5.0f) * fps);
		th.jit_len = (uint32_t)(th_randf(1.0f, 3.0f) * fps);
	}
	if (fnum >= th.jit_at + th.jit_len) {
		th.jit_at = fnum + (uint32_t)(th_randf(3.0f, 8.0f) * fps);
		th.jit_len = (uint32_t)(th_randf(1.0f, 3.0f) * fps);
	}
	else if (fnum >= th.jit_at) {
		active = true;
	}

	if (active) {
		th.jit_x += (th_randf(-2.0f * s, 2.0f * s) - th.jit_x) * 0.6f;
		th.jit_y += (th_randf(-1.2f * s, 1.2f * s) - th.jit_y) * 0.6f;
	}
	else {
		th.jit_x *= 0.6f;
		th.jit_y *= 0.6f;
		if (fabsf(th.jit_x) < 0.05f && fabsf(th.jit_y) < 0.05f)
			th.jit_x = th.jit_y = 0.0f;
	}

	*jx = th.jit_x;
	*jy = th.jit_y;
}

/* Copy the background shifted by (jx, jy) pixels (chroma by half that),
 * bilinear for sub-pixel offsets, edges clamped.
 */
static void copy_background(uint8_t *buff, float jx, float jy)
{
	int w = (int)th.w, h = (int)th.h;
	int cw = w / 2, ch = h / 2;
	int p, x, y;

	if (jx == 0.0f && jy == 0.0f) {
		memcpy(buff, th.bg, (size_t)w * h * 3 / 2);
		return;
	}

	for (p = 0; p < 3; p++) {
		int pw = p ? cw : w, ph = p ? ch : h;
		float dx = p ? jx * 0.5f : jx, dy = p ? jy * 0.5f : jy;
		/* source position = dest - d */
		int ix = ifloor(-dx), iy = ifloor(-dy);
		int fx = (int)((-dx - (float)ix) * 256.0f);
		int fy = (int)((-dy - (float)iy) * 256.0f);
		size_t base = p == 0 ? 0 : (size_t)w * h
			+ (size_t)(p - 1) * cw * ch;
		const uint8_t *src = th.bg + base;
		uint8_t *dst = buff + base;

		for (y = 0; y < ph; y++) {
			int sy0 = y + iy, sy1 = sy0 + 1;
			const uint8_t *r0, *r1;

			sy0 = sy0 < 0 ? 0 : (sy0 >= ph ? ph - 1 : sy0);
			sy1 = sy1 < 0 ? 0 : (sy1 >= ph ? ph - 1 : sy1);
			r0 = src + (size_t)sy0 * pw;
			r1 = src + (size_t)sy1 * pw;

			for (x = 0; x < pw; x++) {
				int sx0 = x + ix, sx1 = sx0 + 1;
				int top, bot;

				sx0 = sx0 < 0 ? 0 : (sx0 >= pw ? pw - 1 : sx0);
				sx1 = sx1 < 0 ? 0 : (sx1 >= pw ? pw - 1 : sx1);
				top = r0[sx0] * (256 - fx) + r0[sx1] * fx;
				bot = r1[sx0] * (256 - fx) + r1[sx1] * fx;
				dst[(size_t)y * pw + x] = (uint8_t)
					((top * (256 - fy) + bot * fy
					  + (1 << 15)) >> 16);
			}
		}
	}
}

void draw_talkinghead(uint8_t *buff, uint32_t w, uint32_t h,
		      uint32_t fnum, uint32_t fps)
{
	float jx, jy;
	uint32_t ysz = w * h;
	float s = (float)w / 640.0f;
	float t, sway_x, sway_y, breath, env, eopen, yaw, pitch;
	float lift, wave, gain;
	float hcx, hcy, hrx, hry, fx;
	uint32_t i, off;

	if (!buff || w < 16 || h < 16)
		return;
	if (fps == 0)
		fps = 15;

	if (th.w != w || th.h != h || !th.bg) {
		if (th_init(w, h))
			return;
	}

	t = (float)fnum / (float)fps;
	sway_x = s * (16.0f * sinf(2.0f * TH_PI * 0.27f * t)
		      + 8.0f * sinf(2.0f * TH_PI * 0.53f * t + 1.0f)
		      + 3.0f * sinf(2.0f * TH_PI * 1.10f * t + 0.3f));
	sway_y = s * (6.0f * sinf(2.0f * TH_PI * 0.21f * t + 0.5f)
		      + 2.0f * sinf(2.0f * TH_PI * 0.43f * t + 2.0f));
	breath = 1.0f + 0.015f * sinf(2.0f * TH_PI * 0.25f * t);
	env = speech_env(fnum, fps);
	eopen = eye_open(fnum, fps);
	yaw = wander_step(&th.yaw, fnum, fps, -1.0f, 1.0f, 1.5f, 4.0f,
			  0.12f);
	pitch = wander_step(&th.pitch, fnum, fps, -0.6f, 0.6f, 0.8f, 2.5f,
			    0.2f);
	if (th.talking)
		pitch += 0.25f * env;
	lift = gesture(fnum, fps, &wave);

	camera_jitter(fnum, fps, s, &jx, &jy);
	copy_background(buff, jx, jy);
	sway_x += jx;
	sway_y += jy;

	hcx = 0.5f * w + sway_x + 10.0f * s * yaw;
	hcy = 0.42f * h + sway_y + 7.0f * s * pitch;
	hrx = 0.14f * w * breath;
	hry = 0.245f * h * breath;
	/* facial features shift with the head turn */
	fx = hcx + 0.22f * hrx * yaw;

	/* torso, textured shirt */
	{
		struct ellipse e = {0.5f * w + 0.5f * sway_x,
				    1.00f * h + 0.3f * sway_y,
				    0.37f * w * breath, 0.34f * h * breath,
				    4.0f * s};
		/* folds drift relative to the body as it moves */
		struct color c = {72.0f, 155.0f, 112.0f, -18.0f, 10.0f,
				  32.0f, 7.0f / s, 1.0f,
				  30.0f * sinf(2.0f * TH_PI * 0.35f * t)
				  + 20.0f * yaw,
				  25.0f * sinf(2.0f * TH_PI * 0.22f * t + 1.0f)
				  + 40.0f * (breath - 1.0f) / 0.015f};

		draw_ellipse(buff, w, h, &e, &c);
	}
	/* neck */
	{
		struct ellipse e = {hcx - 2.0f * s * yaw, hcy + 0.95f * hry,
				    0.45f * hrx, 0.40f * hry, 3.0f * s};
		struct color c = {125.0f, 112.0f, 152.0f, -10.0f, -8.0f,
				  4.0f, 1.0f / s, 1.0f};

		draw_ellipse(buff, w, h, &e, &c);
	}
	/* hair (behind the face), strand-like texture */
	{
		struct ellipse e = {hcx - 0.05f * hrx * yaw,
				    hcy - 0.06f * hry,
				    1.04f * hrx, 1.02f * hry, 3.0f * s};
		struct color c = {42.0f, 124.0f, 133.0f, -6.0f, -6.0f,
				  40.0f, 7.0f / s, 5.0f,
				  25.0f * yaw, 10.0f * pitch};

		draw_ellipse(buff, w, h, &e, &c);
	}
	/* face, lit from the upper left; the jaw drops while talking */
	{
		struct ellipse e = {hcx + 0.06f * hrx * yaw,
				    hcy + 0.07f * hry + 0.02f * hry * env,
				    (0.94f - 0.05f * fabsf(yaw)) * hrx,
				    (0.93f + 0.02f * env) * hry, 3.0f * s};
		struct color c = {155.0f, 110.0f, 152.0f,
				  -22.0f - 8.0f * yaw, -14.0f,
				  10.0f, 4.0f / s, 1.0f};

		draw_ellipse(buff, w, h, &e, &c);
	}
	/* eyes */
	for (i = 0; i < 2; i++) {
		float side = i ? 1.0f : -1.0f;
		/* the far eye gets narrower when turning */
		float narrow = 1.0f - 0.35f * clampf(side * -yaw, 0.0f, 1.0f);
		float ex = fx + side * 0.36f * hrx * (1.0f - 0.15f * fabsf(yaw));
		float ey = hcy - 0.02f * hry + 0.04f * hry * pitch;
		struct ellipse brow = {ex, ey - 0.16f * hry - 0.03f * hry * env,
				       0.20f * hrx * narrow, 0.035f * hry,
				       2.0f * s};
		struct ellipse eye = {ex, ey, 0.15f * hrx * narrow,
				      0.065f * hry * eopen, 1.5f * s};
		struct color cb = {55.0f, 124.0f, 134.0f, 0.0f, 0.0f,
				   10.0f, 3.0f / s, 3.0f};
		struct color ce = {35.0f, 128.0f, 128.0f, 0.0f, 0.0f,
				   0.0f, 0.0f, 1.0f};

		draw_ellipse(buff, w, h, &brow, &cb);
		draw_ellipse(buff, w, h, &eye, &ce);
	}
	/* nose shadow */
	{
		struct ellipse e = {fx + 0.05f * hrx + 0.08f * hrx * yaw,
				    hcy + 0.22f * hry + 0.04f * hry * pitch,
				    0.10f * hrx, 0.14f * hry, 4.0f * s};
		struct color c = {132.0f, 112.0f, 152.0f, 6.0f, 0.0f,
				  0.0f, 0.0f, 1.0f};

		draw_ellipse(buff, w, h, &e, &c);
	}
	/* mouth, opening with the speech envelope */
	{
		struct ellipse e = {fx, hcy + 0.50f * hry + 0.05f * hry * env
					+ 0.03f * hry * pitch,
				    (0.30f + 0.04f * env) * hrx
					* (1.0f - 0.2f * fabsf(yaw)),
				    (0.035f + 0.13f * env) * hry,
				    2.0f * s};
		struct color c = {70.0f - 25.0f * env, 118.0f, 170.0f,
				  0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

		draw_ellipse(buff, w, h, &e, &c);
	}
	/* hand gesture: forearm (sleeve) and hand rising from the bottom */
	if (lift > 0.0f) {
		float side = th.gest_left ? -1.0f : 1.0f;
		float hx = 0.5f * w + side * 0.26f * w + sway_x
			+ 20.0f * s * wave * lift;
		float hy = h * (1.15f - 0.48f * lift);
		struct ellipse arm = {hx + side * 0.04f * w, hy + 0.20f * h,
				      0.085f * w, 0.24f * h, 4.0f * s};
		struct ellipse hand = {hx, hy, 0.067f * w, 0.10f * h,
				       3.0f * s};
		struct color ca = {68.0f, 155.0f, 112.0f, -10.0f, 0.0f,
				   32.0f, 7.0f / s, 1.0f,
				   40.0f * wave * lift, 60.0f * lift};
		struct color chd = {150.0f, 110.0f, 152.0f, -15.0f, -10.0f,
				    6.0f, 1.5f / s, 1.0f};

		draw_ellipse(buff, w, h, &arm, &ca);
		draw_ellipse(buff, w, h, &hand, &chd);
	}

	/* auto-exposure drift plus sensor noise on luma */
	gain = 1.0f + 0.025f * sinf(2.0f * TH_PI * 0.07f * t)
		+ 0.010f * sinf(2.0f * TH_PI * 0.31f * t + 1.0f);
	off = (fnum * 7919u) % TH_NOISE_LEN;
	for (i = 0; i < ysz; i++) {
		int v = (int)((float)buff[i] * gain + 0.5f)
			+ th.noise[(off + i) % TH_NOISE_LEN];

		buff[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
	}

	/* weaker chroma noise, [-2, 2] */
	off = (fnum * 104729u + 12345u) % TH_NOISE_LEN;
	for (i = ysz; i < ysz * 3 / 2; i++) {
		int v = (int)buff[i]
			+ th.noise[(off + i) % TH_NOISE_LEN] / 3;

		buff[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
	}
}
