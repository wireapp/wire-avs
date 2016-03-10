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
#include "vie_render_view.h"

int vie_calc_viewport_for_video(const struct vrect *vp, const struct vsz *vs,
	struct vrect *dp)
{
	dp->x = dp->y = dp->w = dp->h = 0;

	if (vp->w == 0 || vp->h == 0 || vs->w == 0 || vs->h == 0) {
		return EINVAL;
	}

	if (vp->w * vs->h > vs->w * vp->h) {
		// viewport has higher aspect ratio, fit width
		dp->w = vp->w;
		dp->h = vp->w * vs->h / vs->w;
	}
	else {
		// viewport has lower aspect ratio, fit height
		dp->h = vp->h;
		dp->w = vp->h * vs->w / vs->h;
	}


	int pcx2 = (vp->x + vp->w);
	int pcy2 = (vp->y + vp->h);

	dp->x =	(pcx2 - dp->w) / 2;
	dp->y = (pcy2 - dp->h) / 2;

	return 0;
}

