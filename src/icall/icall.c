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

#include <re.h>
#include <avs.h>
#include <ctype.h>

void icall_set_functions(struct icall *icall,
			 icall_add_turnserver		*add_turnserver,
			 icall_add_sft			*add_sft,
			 icall_start			*start,
			 icall_answer			*answer,
			 icall_end			*end,
			 icall_reject			*reject,
			 icall_media_start		*media_start,
			 icall_media_stop		*media_stop,
			 icall_set_media_laddr          *set_media_laddr,
			 icall_set_vstate		*set_video_send_state,
			 icall_msg_recv			*msg_recv,
			 icall_sft_msg_recv		*sft_msg_recv,
			 icall_get_members		*get_members,
			 icall_set_quality_interval	*set_quality_interval,
			 icall_dce_send                 *dce_send,
			 icall_set_clients		*set_clients,
			 icall_update_mute_state	*update_mute_state,
			 icall_request_video_streams	*request_video_streams,
			 icall_set_media_key		*set_media_key,
			 icall_debug			*debug,
			 icall_stats			*stats,
			 icall_set_background           *set_background,
			 icall_activate                 *activate)
{
	if (!icall) {
		warning("icall_set_functions called on NULL icall\n");
		return;
	}
	icall->add_turnserver		= add_turnserver;
	icall->add_sft			= add_sft;
	icall->start			= start;
	icall->answer			= answer;
	icall->end			= end;
	icall->reject			= reject;
	icall->media_start		= media_start;
	icall->media_stop		= media_stop;
	icall->set_media_laddr          = set_media_laddr;
	icall->set_video_send_state	= set_video_send_state;
	icall->msg_recv			= msg_recv;
	icall->sft_msg_recv		= sft_msg_recv;
	icall->get_members		= get_members;
	icall->set_quality_interval	= set_quality_interval;
	icall->dce_send                 = dce_send;
	icall->set_clients		= set_clients;
	icall->update_mute_state	= update_mute_state;
	icall->request_video_streams	= request_video_streams;
	icall->set_media_key		= set_media_key;
	icall->debug			= debug;
	icall->stats			= stats;
	icall->set_background           = set_background;
	icall->activate                 = activate;
}


void icall_set_callbacks(struct icall *icall,
			 icall_send_h		*sendh,
			 icall_sft_h		*sfth,
			 icall_start_h		*starth,
			 icall_answer_h		*answerh,
			 icall_media_estab_h	*media_estabh,
			 icall_audio_estab_h	*audio_estabh,
			 icall_datachan_estab_h	*datachan_estabh,
			 icall_media_stopped_h	*media_stoppedh,
			 icall_group_changed_h	*group_changedh,
			 icall_leave_h		*leaveh,
			 icall_close_h		*closeh,
			 icall_metrics_h	*metricsh,
			 icall_vstate_changed_h	*vstate_changedh,
			 icall_acbr_changed_h	*acbr_changedh,
			 icall_muted_changed_h	*muted_changedh,
			 icall_quality_h	*qualityh,
			 icall_norelay_h	*norelayh,
			 icall_req_clients_h	*req_clientsh,
			 icall_audio_level_h    *audio_levelh,			 
			 icall_req_new_epoch_h  *req_new_epochh,
			 void			*arg)
{
	if (!icall) {
		warning("icall_set_functions called on NULL icall\n");
		return;
	}
	icall->sendh		= sendh;
	icall->sfth		= sfth;
	icall->starth		= starth;
	icall->answerh		= answerh;
	icall->media_estabh	= media_estabh;
	icall->audio_estabh	= audio_estabh;
	icall->datachan_estabh	= datachan_estabh;
	icall->media_stoppedh	= media_stoppedh;
	icall->group_changedh	= group_changedh;
	icall->leaveh		= leaveh;
	icall->closeh		= closeh;
	icall->metricsh		= metricsh;
	icall->vstate_changedh	= vstate_changedh;
	icall->acbr_changedh	= acbr_changedh;
	icall->muted_changedh	= muted_changedh;
	icall->qualityh		= qualityh;
	icall->norelayh		= norelayh;
	icall->req_clientsh	= req_clientsh;
	icall->audio_levelh     = audio_levelh;
	icall->req_new_epochh   = req_new_epochh;
	icall->arg		= arg;
}

const char *icall_vstate_name(enum icall_vstate state)
{
	switch (state) {

	case ICALL_VIDEO_STATE_STOPPED:     return "STOPPED";
	case ICALL_VIDEO_STATE_STARTED:     return "STARTED";
	case ICALL_VIDEO_STATE_SCREENSHARE: return "SCREENSHARE";
	case ICALL_VIDEO_STATE_BAD_CONN:    return "BADCONN";
	case ICALL_VIDEO_STATE_PAUSED:      return "PAUSED";
	default: return "???";
	}
}

static void client_destructor(void *arg)
{
	struct icall_client *cli = arg;

	mem_deref(cli->userid);
	mem_deref(cli->clientid);
	list_unlink(&cli->le);
}

static void make_lower(char *str)
{
	char *p = str;
	while(*p) {
		*p = tolower(*p);
		++p;
	}
}

struct icall_client *icall_client_alloc(const char *userid,
					const char *clientid)
{
	struct icall_client *cli = NULL;

	cli = mem_zalloc(sizeof(*cli), client_destructor);

	if (cli) {
		str_dup(&cli->userid,   userid);
		str_dup(&cli->clientid, clientid);
		if (cli->userid) {
			make_lower(cli->userid);
		}

		if (cli->clientid) {
			make_lower(cli->clientid);
		}
	}

	return cli;

}

char *icall_metrics2json(const struct icall_metrics *metrics,
			 const char *reason)
{
	struct json_object *jmetrics = NULL;
	char *jsonstr = NULL;

	jmetrics = jzon_alloc_object();
	if (reason) {
		char *lreason = NULL;
		char *c;

		str_dup(&lreason, reason);
		if (lreason) {
			for (c = lreason; *c != 0; c++)
				*c = tolower(*c);

			jzon_add_str(jmetrics, "end_reason", lreason);
		}
		mem_deref(lreason);
	}

	if (metrics) {
		switch (metrics->conv_type) {
		case ICALL_CONV_TYPE_ONEONONE:
			jzon_add_str(jmetrics, "conv_type", "oneonone");
			break;

		case ICALL_CONV_TYPE_GROUP:
			jzon_add_str(jmetrics, "conv_type", "group");
			break;

		case ICALL_CONV_TYPE_CONFERENCE:
			jzon_add_str(jmetrics, "conv_type", "conference-proteus");
			break;

		case ICALL_CONV_TYPE_CONFERENCE_MLS:
			jzon_add_str(jmetrics, "conv_type", "conference-mls");
			break;
		}

		jzon_add_bool(jmetrics, "initiator", metrics->initiator);
		jzon_add_int(jmetrics, "duration_call", metrics->duration_call);
		jzon_add_int(jmetrics, "duration_active", metrics->duration_active);
		jzon_add_int(jmetrics, "participants_max", metrics->participants_max);
		jzon_add_int(jmetrics, "participants_audio_max", metrics->participants_audio_max);
		jzon_add_int(jmetrics, "participants_video_max", metrics->participants_video_max);
		jzon_add_int(jmetrics, "participants_video_req", metrics->participants_video_req);
		jzon_add_int(jmetrics, "packetloss_last", metrics->packetloss_last);
		jzon_add_int(jmetrics, "packetloss_max", metrics->packetloss_max);
		jzon_add_int(jmetrics, "rtt_last", metrics->rtt_last);
		jzon_add_int(jmetrics, "rtt_max", metrics->rtt_max);
		jzon_add_int(jmetrics, "reconnects_attempted", metrics->reconnects_attempted);
		jzon_add_int(jmetrics, "reconnects_successful", metrics->reconnects_successful);
	}

	jzon_encode(&jsonstr, jmetrics);
	mem_deref(jmetrics);

	return jsonstr;
}

