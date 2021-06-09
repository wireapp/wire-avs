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
#include "avs_msystem.h"
#include "avs_voe.h"


int msystem_start_mic_file_playout(const char fileNameUTF8[1024], int fs)
{
	int err = 0;

	err = voe_start_playing_PCM_file_as_microphone(fileNameUTF8, fs);

	return err;
}


void msystem_stop_mic_file_playout(void)
{
	voe_stop_playing_PCM_file_as_microphone();
}


void msystem_set_bitrate(int rate_bps)
{
	voe_set_bitrate(rate_bps);
}


void msystem_set_packet_size(int packet_size_ms)
{
	voe_set_packet_size(packet_size_ms);
}



