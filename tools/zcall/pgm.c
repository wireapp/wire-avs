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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pgm.h"

void pgm_load(struct pgm *pgm, const char *fname)
{
	char buf[1024];
	FILE *inf;
	int bsize, br, bpos;
	int tstart, tend, state;

	if (!pgm || !fname) {
		return;
	}
	memset(pgm, 0, sizeof(struct pgm));

	inf = fopen(fname, "rb");
	if (!inf) {
		return;
	}

	br = fread(buf, 1, 1024, inf);

	if (buf[0] != 'P' || buf[1] != '5' || buf[2] != '\n') {
		return;
	}

	tstart = 3;
	tend = tstart;
	state = 0;
	while (state < 2) {
		if (buf[tend] == '\n') {
			buf[tend] = '\0';
			if (buf[tstart] != '#') {
				switch (state) {
				case 0:
					sscanf(buf + tstart, "%d %d", &pgm->w, &pgm->h);
					break;
				default:
					break;
				}
				state++;
			}
			tstart = tend = tend + 1;
		}
		else {
			tend++;
			if (tend >= br) {
				pgm->w = pgm->h = 0;
				return;
			}
		}
	}

	pgm->s = pgm->w;
	bsize = pgm->w * pgm->h;
	pgm->buf = malloc(bsize);

	bpos = br - tstart;
	memcpy(pgm->buf, buf + tstart, bpos);
	while (!feof(inf) && bpos < bsize) {
		br = fread(pgm->buf + bpos, 1, bsize > 1024 ? 1024 : bsize, inf);
		bpos += br;
	}
	fclose(inf);

}

void pgm_save(struct pgm *pgm, const char *fname)
{
	unsigned char *lptr;
	FILE *outf;
	int y, s;

	if (!pgm || !fname) {
		return;
	}

	outf = fopen(fname, "wb");
	if (!outf) {
		return;
	}

	fprintf(outf, "P5\n%d %d\n255\n", pgm->w, pgm->h);

	s = pgm->s ? pgm->s : pgm->w;
	lptr = pgm->buf;
	for (y = 0; y < pgm->h; y++) {
		fwrite(lptr, 1, pgm->w, outf);
		lptr += s;
	}

	fclose(outf);
}

