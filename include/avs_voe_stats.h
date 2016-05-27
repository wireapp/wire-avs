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

#ifndef AVS_VOE_STATS_H
#define AVS_VOE_STATS_H

#ifdef __cplusplus
extern "C" {
#endif

struct max_min_avg{
	float max;
	float min;
	float avg;
};
        
struct aucodec_stats {
	struct max_min_avg out_vol;
	struct max_min_avg in_vol;
	struct max_min_avg loss_d;
	struct max_min_avg loss_u;
	struct max_min_avg rtt;
	struct max_min_avg jb_size;
	int16_t test_score;
};
    
#ifdef __cplusplus
}
#endif

#endif // AVS_VOE_STATS_H
