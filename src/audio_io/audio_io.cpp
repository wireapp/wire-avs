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

#ifdef __APPLE__
#       include "TargetConditionals.h"
#endif

#include <re.h>

#include "avs_audio_io.h"
#include "src/audio_io/mock/fake_audiodevice.h"
#if TARGET_OS_IPHONE
#include "src/audio_io/ios/audio_io_ios.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include "avs_base.h"
#include "avs_log.h"
#ifdef __cplusplus
}
#endif

static void audio_io_destructor(void *arg)
{
    struct audio_io *aio = (struct audio_io *)arg;
    
    webrtc::audio_io_class *aioc = (webrtc::audio_io_class *)aio->aioc;
    if(aioc){
        aioc->TerminateInternal();
        delete aioc;
    }
}

int  audio_io_alloc(struct audio_io **aiop,
                    enum audio_io_mode mode)
{
    if (!aiop)
        return EINVAL;
    
    struct audio_io *aio = (struct audio_io *)mem_zalloc(sizeof(*aiop), audio_io_destructor);
    if (!aiop)
        return ENOMEM;
    
    webrtc::audio_io_class *aioc = NULL;
    bool realtime = true;
    if (avs_get_flags() & AVS_FLAG_AUDIO_TEST){
        mode = AUDIO_IO_MODE_MOCK_REALTIME;
    }
    switch (mode){
        case AUDIO_IO_MODE_NORMAL:
        {
#if TARGET_OS_IPHONE // For now we only have our own ios audio implementation
            aioc = new webrtc::audio_io_ios();
#endif
        }break;
        
        case AUDIO_IO_MODE_MOCK:
            realtime = false;
        case AUDIO_IO_MODE_MOCK_REALTIME:
        {
            aioc = new webrtc::fake_audiodevice(realtime);
        }break;
        
        default:
            warning("audio_io: audio_io_alloc unknown mode \n");
    }
    if(aioc){
        aioc->InitInternal();
    }
    aio->aioc = aioc;
    
    *aiop = aio;
    
    return 0;
}

int  audio_io_init(struct audio_io *aio)
{
    if(!aio)
        return -1;
    
    if(aio->aioc){
        webrtc::audio_io_class *aioc = (webrtc::audio_io_class *)aio->aioc;
        aioc->InitInternal();
    }
    return 0;
}

int  audio_io_terminate(struct audio_io *aio)
{
    if(!aio)
        return -1;

    if(aio->aioc){
        webrtc::audio_io_class *aioc = (webrtc::audio_io_class *)aio->aioc;
        aioc->TerminateInternal();
    }
    return 0;
}

int  audio_io_enable_sine(struct audio_io *aio)
{
    if(!aio)
        return -1;
    
    if(aio->aioc){
        webrtc::audio_io_class *aioc = (webrtc::audio_io_class *)aio->aioc;
        aioc->EnableSine();
    }
    return 0;
}

int audio_io_reset(struct audio_io *aio)
{
	int res;
	
	if(aio->aioc) {
		webrtc::audio_io_class *aioc;

		aioc = (webrtc::audio_io_class *)aio->aioc;
		res = aioc->ResetAudioDevice();
	}

	return res;
}
