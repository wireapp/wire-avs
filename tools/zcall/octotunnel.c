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

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

struct fpoint {
	float x;
	float y;
};

struct fquad {
	struct fpoint p[4];
};

struct ipoint {
	int32_t x;
	int32_t y;
};

struct iquad {
	struct ipoint p[4];
};

struct iedge {
	struct ipoint p[2];
	float dx;
	float cx;
};

void draw_octotunnel(uint8_t *buff, uint32_t bw, uint32_t bh);

static void edge_append(struct iedge *edges, uint32_t *nedges,
	const struct ipoint *p0, const struct ipoint *p1)
{
	struct iedge *e = &edges[*nedges];

	if(p0->y == p1->y) {
		return;
	}
	else if (p0->y < p1->y) {
		e->p[0] = *p0;
		e->p[1] = *p1;
	}
	else {
		e->p[0] = *p1;
		e->p[1] = *p0;
	}

	e->dx = (float)(e->p[1].x - e->p[0].x) / (float)(e->p[1].y - e->p[0].y);
	e->cx = (float)(e->p[0].x);

	(*nedges)++;
}

static void edgelist_sort(struct iedge *e, uint32_t nedges)
{
	struct iedge t;
	uint32_t p = 0;

	if (nedges == 0) {
		return;
	}
	while(p < nedges - 1) {
		if(e[p].p[0].y > e[p + 1].p[0].y ||
		(e[p].p[0].y == e[p + 1].p[0].y && e[p].p[0].x > e[p + 1].p[0].x)) {
			t = e[p];
			e[p] = e[p + 1];
			e[p + 1] = t;
			if (p > 0) {
				p--;
			}
		}
		else {
			p++;
		}
	}
}

/*
static void edgelist_print(struct iedge *e, uint32_t nedges)
{
	uint32_t p;

	printf("-----\n");
	for (p = 0; p < nedges; p++) {
		printf("p0: (%d,%d) p1: (%d,%d) cx: %.2f dx: %.2f\n",
			e[p].p[0].x, e[p].p[0].y, e[p].p[1].x, e[p].p[1].y, e[p].cx, e[p].dx);
	}
}
*/

static void draw_quad(uint8_t *buf, uint32_t uw, uint32_t uh, const struct iquad *q)
{
	struct iedge edges[4];
	uint32_t nedges = 0;
	int32_t w = (int32_t)uw;
	int32_t h = (int32_t)uh;

	edge_append(edges, &nedges, &q->p[0], &q->p[1]);
	edge_append(edges, &nedges, &q->p[1], &q->p[2]);
	edge_append(edges, &nedges, &q->p[2], &q->p[3]);
	edge_append(edges, &nedges, &q->p[3], &q->p[0]);

	//edgelist_print(edges, nedges);
	edgelist_sort(edges, nedges);
	//edgelist_print(edges, nedges);

	int32_t cy = edges[0].p[0].y;
	while(1) {
		int32_t xo[2];
		uint32_t xp = 0;
		uint32_t ep = 0;

		for (ep = 0; ep < nedges; ep++) {
			if (edges[ep].p[0].y <= cy && edges[ep].p[1].y > cy) {
				int32_t x = edges[ep].cx;
				if (x < 0) {
					x = 0;
				}
				xo[xp] = x;
				edges[ep].cx += edges[ep].dx;
				xp++;
			}
		}
		if (xp == 0 || cy >= h) {
			break;
		}

		if (cy > 0) {
			if (xo[0] > xo[1]) {
				int32_t t;
				t = xo[0];
				xo[0] = xo[1];
				xo[1] = t;
			}
			uint8_t *bl = buf + w * cy;
			int32_t x;
			for (x = xo[0]; x < xo[1] && x < w; x++) {
				bl[x] = 255;
			}
		}
		cy++;
	}
		
}

static void fquad_rotate(const struct fquad *q, float angle, struct fquad *r)
{
	float s = sin(angle);
	float c = cos(angle);

	for (uint32_t p = 0; p < 4; p++) {
		r->p[p].x = q->p[p].x * c + q->p[p].y * -s;
		r->p[p].y = q->p[p].x * s + q->p[p].y * c;
	}
}

static void scale_convert(const struct fquad *q, float scale, struct iquad *r)
{
	for (uint32_t p = 0; p < 4; p++) {
		r->p[p].x = (int32_t)(q->p[p].x * scale);
		r->p[p].y = (int32_t)(q->p[p].y * scale);
	}
}

static void iquad_translate(const struct iquad *q, int32_t xo, int32_t yo, struct iquad *r)
{
	for (uint32_t p = 0; p < 4; p++) {
		r->p[p].x = q->p[p].x + xo;
		r->p[p].y = q->p[p].y + yo;
	}
}

static void iquad_rot90(const struct iquad *q, struct iquad *r)
{
	struct iquad t;
	for (uint32_t p = 0; p < 4; p++) {
		t.p[p].x = q->p[p].y;
		t.p[p].y = -q->p[p].x;
	}
	*r = t;
}

static struct {
	float rot;
	float scale;
}octotunnel = {
	0.0f,
	5.0f
};

void draw_octotunnel(uint8_t *buff, uint32_t bw, uint32_t bh)
{
	uint32_t bsz = bw * bh;
	float sq = sqrtf(2.0f);
	struct fquad q = {{{sq, 0.0f}, {2.0f, 0.0f}, {sq, sq}, {1.0f, 1.0f}}};
	int flip = 0;
	float fortyfive = 0.785f;

	float scale = octotunnel.scale;

	memset(buff, 0, bsz);

	for (uint32_t r = 0; r < 12; r++) {
		struct fquad fq;
		struct iquad iq, iq2;
		float rr = flip ? octotunnel.rot + fortyfive : octotunnel.rot;

		fquad_rotate(&q, rr, &fq);
		scale_convert(&fq, scale, &iq);
		for (uint32_t s = 0; s < 4; s++) {
			iquad_translate(&iq, bw/2, bh/2, &iq2);
			draw_quad(buff, bw, bh, &iq2);
			iquad_rot90(&iq, &iq);
		}
		scale *= sq;
		flip = !flip;
	}

	octotunnel.scale *= 1.03f;
	octotunnel.rot += fortyfive / 25.0f;

	if(octotunnel.scale > 5.0f * sq) {
		octotunnel.scale /= sq;
		octotunnel.rot -= fortyfive;
	}
}

