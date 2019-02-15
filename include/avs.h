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
#ifndef AVS_H__
#define AVS_H__

#ifdef __APPLE__
#define AVS_EXPORT __attribute__((visibility("default")))
#else
#ifdef ANDROID
#define AVS_EXPORT __attribute__((visibility("default")))
#else
#define AVS_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif


#include "avs_base.h"
#include "avs_cert.h"
#include "avs_conf_pos.h"
#include "avs_dict.h"
#include "avs_jzon.h"
#include "avs_kase.h"
#include "avs_log.h"
#include "avs_aucodec.h"
#include "avs_extmap.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_msystem.h"
#include "avs_nevent.h"
#include "avs_packetqueue.h"
#include "avs_serial.h"
#include "avs_store.h"
#include "avs_string.h"
#include "avs_trace.h"
#include "avs_turn.h"
#include "avs_vidcodec.h"
#include "avs_uuid.h"
#include "avs_ztime.h"

#include "avs_rest.h"
#include "avs_flowmgr.h"
#include "avs_config.h"
#include "avs_engine.h"
#include "avs_netprobe.h"
#include "avs_network.h"
#include "avs_audummy.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_icall.h"
#include "avs_ecall.h"
#include "avs_egcall.h"

#include "avs_mediamgr.h"

#include "avs_voe.h"    
#include "avs_version.h"
    
#include "avs_audio_effect.h"
    
#include "avs_dce.h"

#ifdef HAVE_CRYPTOBOX
#include "avs_cryptobox.h"
#endif

#ifdef HAVE_PROTOBUF
#include "avs_protobuf.h"
#endif

    
#ifdef __cplusplus
}
#endif


#endif
