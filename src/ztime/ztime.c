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

#define _XOPEN_SOURCE       /* See feature_test_macros(7) */
#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <time.h>
#include <sys/time.h>
#include <re.h>
#include "avs_ztime.h"


#if defined(__ANDROID__)
#include <time64.h>


/*
 * 32-bit Android has only timegm64() and not timegm().
 * We replicate the behaviour of timegm() when the result overflows time_t.
 */
time_t timegm(struct tm* const t);
time_t timegm(struct tm* const t)
{
	/* time_t is signed on Android. */
	static const time_t kTimeMax = ~(1L << (sizeof(time_t) * 8 - 1));
	static const time_t kTimeMin = (1L << (sizeof(time_t) * 8 - 1));
	time64_t result = timegm64(t);
	if (result < kTimeMin || result > kTimeMax)
		return -1;
	return result;
}
#endif


/*
 * Example input:
 *
 *     2016-02-25T08:39:34.752Z
 */
int ztime_decode(struct ztime *ztime, const char *str)
{
	struct tm tm;
	struct pl pl_msec;

	if (!str)
		return EINVAL;

	if (!strptime(str, "%Y-%m-%dT%H:%M:%S", &tm)) {
		return EBADMSG;
	}

	if (re_regex(str, str_len(str), ".[0-9]+Z", &pl_msec))
		return EBADMSG;

	/* The timegm() function interprets the input structure as
	 * representing Universal Coordi-nated Time (UTC).
	 */

	if (ztime) {
		ztime->sec  = timegm(&tm);
		ztime->msec = pl_u32(&pl_msec);
	}

	return 0;
}


int ztime_get(struct ztime *ztime)
{
	struct timeval now;

	if (!ztime)
		return EINVAL;

	if (0 != gettimeofday(&now, NULL))
		return errno;

	ztime->sec  = now.tv_sec;
	ztime->msec = now.tv_usec / 1000;

	return 0;
}


/*
 * DIFF  :=  A - B
 */
int64_t ztime_diff(const struct ztime *za, const struct ztime *zb)
{
	int64_t a, b;

	if (!za || !zb)
		return 0LL;

	a = (int64_t)za->sec * 1000 + za->msec;
	b = (int64_t)zb->sec * 1000 + zb->msec;

	return a - b;
}
