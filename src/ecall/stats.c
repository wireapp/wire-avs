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

#include <assert.h>
#include <string.h>
#include <re.h>
#include "avs_log.h"
#include "avs_zapi.h"
#include "avs_media.h"
#include "avs_dce.h"
#include "avs_uuid.h"
#include "avs_turn.h"
#include "avs_cert.h"
#include "avs_msystem.h"
#include "avs_econn.h"
#include "avs_econn_fmt.h"
#include "avs_icall.h"
#include "avs_ecall.h"
#include "avs_jzon.h"
#include "avs_mediastats.h"
#include "avs_version.h"
#include "ecall.h"


static bool stats_has_video(const struct mediaflow *mf)
{
	const struct rtp_stats* rtps = mediaflow_snd_video_rtp_stats(mf);
	bool has_video = false;
	if (rtps) {
		if (rtps->bit_rate_stats.max != -1) {
			has_video = true;
		}
	}
	return has_video;
}

static int round(int in, int round_to)
{
	int out = (in + (round_to >> 1))/round_to;
	out = out * round_to;
    
	return out;
}

bool ecall_stats_prepare(struct ecall *ecall, struct json_object *jobj,
			 int ecall_err)
{
	int err = 0;

	if (!ecall)
		return false;


	if (!ecall->mf)
		return false;

	bool ice = false;
	bool dtls = false;

	dtls = mediaflow_dtls_ready(ecall->mf);
	ice = mediaflow_ice_ready(ecall->mf);

	json_object_object_add(jobj, "version",
			       json_object_new_string(avs_version_str()));
	json_object_object_add(jobj, "protocol-version",
			       json_object_new_string(econn_proto_version));
	bool is_group = ecall_get_conf_part(ecall) ? true : false;
	json_object_object_add(jobj, "group",
			       json_object_new_boolean(is_group));

	err |= jzon_add_str(jobj, "direction",
			       econn_dir_name(econn_current_dir(ecall->econn)));

	json_object_object_add(jobj, "answered",
			       json_object_new_boolean(ecall->answered));
    
	err = jzon_add_int(jobj, "estab_time(ms)", round(ecall->call_estab_time, 10));
	if (err)
		return false;
	err = jzon_add_int(jobj, "audio_setup_time(ms)",
			   round(ecall->audio_setup_time, 10));
	if (err)
		return false;

	json_object_object_add(jobj, "dtls",
			       json_object_new_boolean(dtls));
	json_object_object_add(jobj, "ice",
			       json_object_new_boolean(ice));
	json_object_object_add(jobj, "video",
		       json_object_new_boolean(stats_has_video(ecall->mf)));

	err |= jzon_add_int(jobj, "media_time(s)",
			    mediaflow_get_media_time(ecall->mf)/1000);

	struct aucodec_stats *voe_stats = mediaflow_codec_stats(ecall->mf);
	if (voe_stats) {
		err |= jzon_add_int(jobj, "mic_vol(dB)",
				    voe_stats->in_vol.avg);
		err |= jzon_add_int(jobj, "spk_vol(dB)",
				    voe_stats->out_vol.avg);
		err |= jzon_add_int(jobj, "avg_rtt", round(voe_stats->rtt.avg, 10));
		err |= jzon_add_int(jobj, "max_rtt", round(voe_stats->rtt.max, 10));
		err |= jzon_add_int(jobj, "avg_jb_loss",
				    voe_stats->loss_d.avg);
		err |= jzon_add_int(jobj, "max_jb_loss",
				    voe_stats->loss_d.max);
		err |= jzon_add_int(jobj, "avg_jb_size",
				    voe_stats->jb_size.avg);
		err |= jzon_add_int(jobj, "max_jb_size",
				    voe_stats->jb_size.max);
		err |= jzon_add_int(jobj, "avg_loss_u", voe_stats->loss_u.avg);
		err |= jzon_add_int(jobj, "max_loss_u", voe_stats->loss_u.max);
	}
	const struct rtp_stats* rtps =mediaflow_rcv_audio_rtp_stats(ecall->mf);
	if (rtps) {
		err |= jzon_add_int(jobj, "avg_loss_d",
				    (int)rtps->pkt_loss_stats.avg);
		err |= jzon_add_int(jobj, "max_loss_d",
				    (int)rtps->pkt_loss_stats.max);
		err |= jzon_add_int(jobj, "avg_rate_d",
				    (int)rtps->bit_rate_stats.avg);
		err |= jzon_add_int(jobj, "min_rate_d",
				    (int)rtps->bit_rate_stats.min);
		err |= jzon_add_int(jobj, "avg_pkt_rate_d",
				    (int)rtps->pkt_rate_stats.avg);
		err |= jzon_add_int(jobj, "min_pkt_rate_d",
				    (int)rtps->pkt_rate_stats.min);
		err |= jzon_add_int(jobj, "a_dropouts", rtps->dropouts);
	}
	rtps = mediaflow_snd_audio_rtp_stats(ecall->mf);
	if (rtps) {
		err |= jzon_add_int(jobj, "avg_rate_u",
				    (int)rtps->bit_rate_stats.avg);
		err |= jzon_add_int(jobj, "min_rate_u",
				    (int)rtps->bit_rate_stats.min);
		err |= jzon_add_int(jobj, "avg_pkt_rate_u",
				    (int)rtps->pkt_rate_stats.avg);
		err |= jzon_add_int(jobj, "min_pkt_rate_u",
				    (int)rtps->pkt_rate_stats.min);
	}
	if (voe_stats) {
		struct json_object *jsess;
		jsess = json_object_new_string(voe_stats->audio_route);
		json_object_object_add(jobj, "audio_route", jsess);
	}
	rtps = mediaflow_rcv_video_rtp_stats(ecall->mf);
	if (rtps) {
		err |= jzon_add_int(jobj, "v_avg_rate_d",
				    (int)rtps->bit_rate_stats.avg);
		err |= jzon_add_int(jobj, "v_min_rate_d",
				    (int)rtps->bit_rate_stats.min);
		err |= jzon_add_int(jobj, "v_max_rate_d",
				    (int)rtps->bit_rate_stats.max);
		err |= jzon_add_int(jobj, "v_avg_frame_rate_d",
				    (int)rtps->frame_rate_stats.avg);
		err |= jzon_add_int(jobj, "v_min_frame_rate_d",
				    (int)rtps->frame_rate_stats.min);
		err |= jzon_add_int(jobj, "v_max_frame_rate_d",
				    (int)rtps->frame_rate_stats.max);
		err |= jzon_add_int(jobj, "v_dropouts", rtps->dropouts);
	}
	rtps = mediaflow_snd_video_rtp_stats(ecall->mf);
	if (rtps) {
		err |= jzon_add_int(jobj, "v_avg_rate_u",
				    (int)rtps->bit_rate_stats.avg);
		err |= jzon_add_int(jobj, "v_min_rate_u",
				    (int)rtps->bit_rate_stats.min);
		err |= jzon_add_int(jobj, "v_max_rate_u",
				    (int)rtps->bit_rate_stats.max);
		err |= jzon_add_int(jobj, "v_avg_frame_rate_u",
				    (int)rtps->frame_rate_stats.avg);
		err |= jzon_add_int(jobj, "v_min_frame_rate_u",
				    (int)rtps->frame_rate_stats.min);
		err |= jzon_add_int(jobj, "v_max_frame_rate_u",
				    (int)rtps->frame_rate_stats.max);
	}
	if (err)
		return false;

	const struct mediaflow_stats *mf_stats =mediaflow_stats_get(ecall->mf);
	if (mf_stats) {
		err |= jzon_add_int(jobj, "turn_alloc",
				    round(mf_stats->turn_alloc, 10));
		err |= jzon_add_int(jobj, "nat_estab",
				    round(mf_stats->nat_estab, 10));
		err |= jzon_add_int(jobj, "dtls_estab",
				    round(mf_stats->dtls_estab, 10));
		if (err)
			return false;
	}

	err |= jzon_add_int(jobj, "ecall_error", ecall_err);

	/* Mediaflow details: */
	err |= jzon_add_str(jobj, "local_cand", mediaflow_lcand_name(ecall->mf));
	err |= jzon_add_str(jobj, "remote_cand", mediaflow_rcand_name(ecall->mf));
	if (err)
		return false;

	err |= jzon_add_str(jobj, "crypto", "%H",
			   mediaflow_cryptos_print, ecall->crypto);

	return true;
}
