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

#include <string.h>
#include <re/re.h>
#include "avs_aucodec.h"
#include "avs_log.h"
#include "avs_uuid.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_flowmgr.h"
#include "avs_rest.h"
#include "avs_voe.h"
#include "flowmgr.h"


int flowmgr_vm_start_record(struct flowmgr *fm, const char fileNameUTF8[1024])
{
	int err = 0;
	(void)fm;

	voe_vm_start_record(fileNameUTF8);
    
	return err;
}


int flowmgr_vm_stop_record(struct flowmgr *fm)
{
	int err = 0;
	(void)fm;

	voe_vm_stop_record();

	return err;
}


int flowmgr_vm_get_length(struct flowmgr *fm,
			  const char fileNameUTF8[1024],
			  int* length_ms)
{
	int err = 0;
	(void)fm;
    
	voe_vm_get_length(fileNameUTF8, length_ms);
    
	return err;
}


int flowmgr_vm_start_play(struct flowmgr *fm,
			  const char fileNameUTF8[1024],
			  int start_time_ms,
			  flowmgr_vm_play_status_h *handler,
			  void *arg)
{
	int err = 0;
	(void)fm;

	voe_vm_start_play(fileNameUTF8, start_time_ms,
			  (vm_play_status_h*)handler, arg);
    
	return err;
}


int flowmgr_vm_stop_play(struct flowmgr *fm)
{
	int err = 0;
	(void)fm;
    
	voe_vm_stop_play();
    
	return err;
}
