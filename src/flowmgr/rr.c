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

#define _POSIX_SOURCE 1
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <re/re.h>

#include "avs.h"

#include "flowmgr.h"


#define RR_MAGIC 0x8081bb4b
#define RR_GUARDTIME 5000


static struct {
	int nrr; /* # of pending rest requests */

	uint64_t rr_max;
	uint64_t rr_acc;
	size_t rr_total;
} fmstats = {
	0,
};


static void rr_destructor(void *arg)
{
	struct rr_resp *rr = arg;

	rr->magic = 0;

	list_unlink(&rr->le);

	--fmstats.nrr;
	debug("rr_destructor: %p nrr=%d\n", rr, fmstats.nrr);
}


/* "call" is optional */
int rr_alloc(struct rr_resp **rrp, struct flowmgr *fm, struct call *call,
	     rr_resp_h *resph, void *arg)
{
	struct rr_resp *rr;

	if (!fm)
		return EINVAL;

	rr = mem_zalloc(sizeof(*rr), rr_destructor);
	if (rr == NULL) {
		return ENOMEM;
	}

	++fmstats.nrr;
	++fmstats.rr_total;
	debug("rr_alloc: %p nrr=%d fm=%p call=%p\n", rr, fmstats.nrr, fm, call);

	rr->magic = RR_MAGIC;
	rr->fm = fm;
	//rr->call = call;
	rr->resph = resph;
	rr->arg = arg;

	list_append(&fm->rrl, &rr->le, rr);

	rr->ts_req = tmr_jiffies();

	*rrp = rr;

	return 0;
}


void rr_cancel(struct rr_resp *rr)
{
	if (!rr)
		return;

	if (rr->magic != RR_MAGIC) {
		warning("flowmgr: rr_cancel: rr invalid magic\n");
		return;
	}

	//if (rr->call)
	//list_unlink(&rr->call_le);
	list_unlink(&rr->le);
	
	//rr->call = NULL;
	rr->resph = NULL;
	
}


void rr_response(struct rr_resp *rr)
{
	uint64_t ms;

	if (!rr)
		return;

	if (rr->magic != RR_MAGIC) {
		warning("rr invalid magic\n");
		return;
	}

	rr->ts_resp = tmr_jiffies();

	ms = rr->ts_resp - rr->ts_req;

	if (ms > RR_GUARDTIME) {
		warning("flowmgr: slow request (%dms > %dms) [%s]\n",
			(int)ms, RR_GUARDTIME, rr->debug);
	}

	fmstats.rr_acc += ms;

	if (ms > fmstats.rr_max) {
		fmstats.rr_max = ms;
	}

	info("flowmgr: RR cur=%dms avg=%.2fms max=%dms    <%s>\n",
	     (int)ms,
	     fmstats.rr_acc / (double)fmstats.rr_total,
	     (int)fmstats.rr_max,
	     rr->debug);
}


bool rr_isvalid(const struct rr_resp *rr)
{
	if (!rr)
		return false;

	return RR_MAGIC == rr->magic;
}
