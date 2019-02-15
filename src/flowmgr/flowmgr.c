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
#define _POSIX_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

#include <re/re.h>

#include "avs.h"
#include "avs_version.h"
#include "avs_vie.h"

#include "flowmgr.h"


static struct {
	struct list fml;

	struct msystem *msys;
} fsys;


int flowmgr_init(const char *msysname)
{
	int err;

	if (fsys.msys != NULL)
		return 0;

	info("flowmgr_init: msys=%s\n", msysname);

	err = marshal_init();
	if (err) {
		error("flow-manager: failed to init marshalling\n");

		return err;
	}

	err = msystem_get(&fsys.msys, msysname, NULL);
	if (err) {
		warning("flowmgr: msystem_init failed: %m\n", err);
		goto out;
	}

	info("flowmgr: initialized -- %s [machine %H]\n",
	     avs_version_str(), sys_build_get, 0);

	msystem_set_tid(fsys.msys, pthread_self());

 out:
	if (err)
		fsys.msys = mem_deref(fsys.msys);

	return err;
}


int flowmgr_start(void)
{
	int err = 0;

	msystem_start(fsys.msys);
	
	return err;
}


void flowmgr_close(void)
{
	flowmgr_wakeup();
	marshal_close();
	// XXX What is the lifetime of msystem? 
	fsys.msys = mem_deref(fsys.msys);
}


static void fm_destructor(void *arg)
{
	struct flowmgr *fm = arg;

	info("flowmgr(%p): destructor\n", fm);

	list_unlink(&fm->le);
}


int flowmgr_alloc(struct flowmgr **fmp, flowmgr_req_h *reqh,
		  flowmgr_err_h *errh, void *arg)
{
	struct flowmgr *fm;
	int err=0;

	if (!fmp) {
		return EINVAL;
	}

	fm = mem_zalloc(sizeof(*fm), fm_destructor);
	if (!fm)
		return ENOMEM;

	info("flowmgr(%p): alloc: (%s)\n", fm, avs_version_str());
	
	list_append(&fsys.fml, &fm->le, fm);

	if (err) {
		mem_deref(fm);
	}
	else {
		*fmp = fm;
	}

	return err;
}


struct flowmgr *flowmgr_free(struct flowmgr *fm)
{
	return mem_deref(fm);
}


int flowmgr_wakeup(void)
{
	return msystem_push(fsys.msys, 0, NULL);	
}


bool flowmgr_can_send_video(struct flowmgr *fm, const char *convid)
{
	return false;
}


void flowmgr_set_video_send_state(struct flowmgr *fm, const char *convid, enum flowmgr_video_send_state state)
{
	if (!fm)
		return;
}


bool flowmgr_is_sending_video(struct flowmgr *fm,
			      const char *convid, const char *partid)
{
	return false;
}


int flowmgr_is_ready(struct flowmgr *fm, bool *is_ready)
{
	if (!fm || !is_ready)
		return EINVAL;

	*is_ready = true;
	
	return 0;
}


void flowmgr_set_audio_state_handler(struct flowmgr *fm,
			flowmgr_audio_state_change_h *state_change_h,
			void *arg)
{
	voe_set_audio_state_handler(state_change_h, arg);
}


struct msystem *flowmgr_msystem(void)
{
	return fsys.msys;
}
