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
#include "avs_audio_effect.h"
#include "flowmgr.h"

int flowmgr_set_audio_effect(struct flowmgr *fm, enum audio_effect effect)
{
	int err = 0;
	(void)fm;

	err = voe_set_audio_effect(effect);
    
	return err;
}

enum audio_effect flowmgr_get_audio_effect(struct flowmgr *fm)
{
	(void)fm;

	return voe_get_audio_effect();
}