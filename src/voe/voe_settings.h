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
#ifndef VOE_SETTINGS_H__
#define VOE_SETTINGS_H__

#ifdef __APPLE__
#       include "TargetConditionals.h"
#endif

#define FORCE_AUDIO_RTP_RECORDING 0
#define FORCE_AUDIO_PREPROC_RECORDING 0

/* ---  Opus settings --- */
#define ZETA_USE_INBAND_FEC              true

#define ZETA_OPUS_USE_STEREO             false
/* By using Opus in mono mode we use the VoIP setting */

#define ZETA_OPUS_BITRATE_LO_BPS         24000
#define ZETA_OPUS_BITRATE_HI_BPS         32000
/* For speech 32000 bps is enough also with inband FEC */

#define ZETA_USE_DTX                     false

/* --- HP Filter Settings              --- */
#define ZETA_USE_HP                          true

#define AGC_COMPRESSION_GAIN_IGNORE 0xDEAD

/* --- Automatic Gain Control settings --- */
#if TARGET_OS_IPHONE
    #define ZETA_USE_EXTERNAL_AUDIO_DEVICE      1
    #define ZETA_USE_AGC_EARPIECE            true
    #define ZETA_USE_AGC_SPEAKER             true
    #define ZETA_USE_AGC_HEADSET             true
    #define ZETA_AGC_MODE_EARPIECE           webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_EARPIECE_CONF      webrtc::kAgcFixedDigital
/* In Speaker mode we dont use Adaptive AGC - risk of adapting to background noise / speakers */
    #define ZETA_AGC_MODE_SPEAKER            webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_SPEAKER_CONF       webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_HEADSET            webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_HEADSET_CONF       webrtc::kAgcFixedDigital
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE          9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE_CONF     9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER           9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER_CONF      9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET           9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET_CONF      9
#elif defined(ANDROID)
    #define ZETA_USE_EXTERNAL_AUDIO_DEVICE      0
    #define ZETA_USE_AGC_EARPIECE            true
    #define ZETA_USE_AGC_SPEAKER             true
    #define ZETA_USE_AGC_HEADSET             true
    #define ZETA_AGC_MODE_EARPIECE           webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_EARPIECE_CONF      webrtc::kAgcFixedDigital
/* In Speaker mode we dont use Adaptive AGC - risk of adapting to background noise / speakers */
    #define ZETA_AGC_MODE_SPEAKER            webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_SPEAKER_CONF       webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_HEADSET            webrtc::kAgcFixedDigital
    #define ZETA_AGC_MODE_HEADSET_CONF       webrtc::kAgcFixedDigital
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE          9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE_CONF     9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER           9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER_CONF      9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET           9
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET_CONF      9
#else // Desktop
    #define ZETA_USE_EXTERNAL_AUDIO_DEVICE      0
    #define ZETA_USE_AGC_EARPIECE            true
    #define ZETA_USE_AGC_SPEAKER             true
    #define ZETA_USE_AGC_HEADSET             true
/* On Desktop we dont distinguish between handsfree and Headset as we cannot detect it reliably */
    #define ZETA_AGC_MODE_EARPIECE           webrtc::kAgcAdaptiveAnalog
    #define ZETA_AGC_MODE_EARPIECE_CONF      webrtc::kAgcAdaptiveAnalog
    #define ZETA_AGC_MODE_SPEAKER            webrtc::kAgcAdaptiveAnalog
    #define ZETA_AGC_MODE_SPEAKER_CONF       webrtc::kAgcAdaptiveAnalog
    #define ZETA_AGC_MODE_HEADSET            webrtc::kAgcAdaptiveAnalog
    #define ZETA_AGC_MODE_HEADSET_CONF       webrtc::kAgcAdaptiveAnalog
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE          AGC_COMPRESSION_GAIN_IGNORE
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_EARPIECE_CONF     AGC_COMPRESSION_GAIN_IGNORE
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER           AGC_COMPRESSION_GAIN_IGNORE
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_SPEAKER_CONF      AGC_COMPRESSION_GAIN_IGNORE
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET           AGC_COMPRESSION_GAIN_IGNORE
    #define ZETA_AGC_DIG_COMPRESS_GAIN_DB_HEADSET_CONF      AGC_COMPRESSION_GAIN_IGNORE
#endif

/* --- Echo Control Settings --- */
#if TARGET_OS_IPHONE
    #define ZETA_USE_AEC_EARPIECE            false
    #define ZETA_USE_AEC_SPEAKER             false
#ifdef ZETA_IOS_STEREO_PLAYOUT
    #define ZETA_USE_BUILD_IN_AEC_HEADSET
    #define ZETA_USE_AEC_HEADSET             true
#else
    #define ZETA_USE_AEC_HEADSET             false
#endif
    // AEC(M) defines has to be set but is not in use
    #define ZETA_AEC_MODE                    webrtc::kEcAec
    #define ZETA_AEC_DELAY_CORRECTION        false
    #define ZETA_AEC_DELAY_AGNOSTIC          false
    #define ZETA_AECM_CNG                    false
    #define ZETA_AECM_MODE_EARPIECE          webrtc::kAecmLoudEarpiece
    #define ZETA_AECM_MODE_SPEAKER           webrtc::kAecmLoudSpeakerphone
    #define ZETA_AECM_MODE_HEADSET           webrtc::kAecmQuietEarpieceOrHeadset
#elif defined(ANDROID)
    #define ZETA_USE_BUILD_IN_AEC_EARPIECE
    #define ZETA_USE_BUILD_IN_AEC_SPEAKER
    #define ZETA_USE_BUILD_IN_AEC_HEADSET
    #define ZETA_USE_AEC_EARPIECE            true
    #define ZETA_USE_AEC_SPEAKER             true
    #define ZETA_USE_AEC_HEADSET             true
    #define ZETA_AEC_MODE                    webrtc::kEcAecm
    #define ZETA_AEC_DELAY_CORRECTION        false
    #define ZETA_AEC_DELAY_AGNOSTIC          false
    #define ZETA_AECM_CNG                    false
    #define ZETA_AECM_MODE_EARPIECE          webrtc::kAecmLoudEarpiece
    #define ZETA_AECM_MODE_SPEAKER           webrtc::kAecmLoudSpeakerphone
    #define ZETA_AECM_MODE_HEADSET           webrtc::kAecmQuietEarpieceOrHeadset
#else // Desktop
    #define ZETA_USE_AEC_EARPIECE            true
    #define ZETA_USE_AEC_SPEAKER             true
    #define ZETA_USE_AEC_HEADSET             true
    #define ZETA_AEC_MODE                    webrtc::kEcConference
    #define ZETA_AEC_DELAY_CORRECTION        true
    #define ZETA_AEC_DELAY_AGNOSTIC          false
    // AECM defines has to be set but is not in use
    #define ZETA_AECM_CNG                    false
    #define ZETA_AECM_MODE_EARPIECE          webrtc::kAecmLoudEarpiece
    #define ZETA_AECM_MODE_SPEAKER           webrtc::kAecmLoudSpeakerphone
    #define ZETA_AECM_MODE_HEADSET           webrtc::kAecmQuietEarpieceOrHeadset
#endif

/* --- Noise Reduction Settings --- */
#if TARGET_OS_IPHONE
    #define ZETA_USE_NS                      true
    #define ZETA_NS_MODE_EARPIECE            webrtc::kNsHighSuppression
    #define ZETA_NS_MODE_SPEAKER             webrtc::kNsHighSuppression
    #define ZETA_NS_MODE_HEADSET             webrtc::kNsHighSuppression
#elif defined(ANDROID)
    #define ZETA_USE_NS                      true
    #define ZETA_NS_MODE_EARPIECE            webrtc::kNsHighSuppression
    #define ZETA_NS_MODE_SPEAKER             webrtc::kNsHighSuppression
    #define ZETA_NS_MODE_HEADSET             webrtc::kNsHighSuppression
#else // Desktop
    #define ZETA_USE_NS                      true
    #define ZETA_NS_MODE_EARPIECE            webrtc::kNsHighSuppression
    #define ZETA_NS_MODE_SPEAKER             webrtc::kNsHighSuppression
    #define ZETA_NS_MODE_HEADSET             webrtc::kNsHighSuppression
#endif

/* --- Recieve Side Noise Reduction Settings --- */
#if TARGET_OS_IPHONE
      // No rcv side noise reduction for iOS
#elif defined(ANDROID)
      // No rcv side noise reduction for Android
#else // Desktop
    #define ZETA_USE_RCV_NS                  true
    #define ZETA_RCV_NS_MODE                 webrtc::kNsModerateSuppression
#endif

#endif
