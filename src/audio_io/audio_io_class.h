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

#ifndef AVS_IO_CLASS_H_
#define AVS_IO_CLASS_H_

#include "avs_audio_io.h"
#include "webrtc/modules/audio_device/include/audio_device.h"

namespace webrtc {
    class audio_io_class : public AudioDeviceModule {
    public:
        virtual int32_t InitInternal() = 0;

        virtual int32_t TerminateInternal() = 0;
                
        virtual int32_t EnableSine() = 0;
    };
}

#endif
