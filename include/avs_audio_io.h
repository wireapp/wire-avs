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

#ifndef AVS_AUDIO_IO_H
#define AVS_AUDIO_IO_H

#define AUDIO_IO_BUF_DUR ((double)0.02)

enum audio_io_mode{
    AUDIO_IO_MODE_NORMAL = 0,
    AUDIO_IO_MODE_MOCK,
    AUDIO_IO_MODE_MOCK_REALTIME,
};

struct audio_io{
    //webrtc::audio_io_class *aioc;
    void *aioc;
};

#ifdef __cplusplus
extern "C" {
#endif

int  audio_io_alloc(struct audio_io **aiop,
               enum audio_io_mode mode);

int  audio_io_init(struct audio_io *aio);
    
int  audio_io_terminate(struct audio_io *aio);
    
int  audio_io_enable_sine(struct audio_io *aio);

int  audio_io_reset(struct audio_io *aio);
	
#ifdef __cplusplus
    }
#endif
    
#endif // AVS_AUDIO_IO_H
