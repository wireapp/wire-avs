/*
* Wire
* Copyright (C) 2019 Wire Swiss GmbH
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

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <re.h>
#include <avs.h>
#include <avs_version.h>
#include <avs_audio_io.h>
#include <avs_audio_level.h>

#ifdef __cplusplus
}
#endif

#include "api/scoped_refptr.h"
#include "api/peer_connection_interface.h"
#include "api/call/call_factory_interface.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/media_stream_interface.h"
#include "api/data_channel_interface.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "rtc_base/logging.h"
#include "rtc_base/physical_socket_server.h"
#include "media/engine/webrtc_media_engine.h"
#include "system_wrappers/include/field_trial.h"

#include "modules/video_capture/video_capture_factory.h"

#include "p2p/client/basic_port_allocator.h"

#include "capture_source.h"
#include "cbr_detector_local.h"
#include "cbr_detector_remote.h"
#include "frame_encryptor_wrapper.h"
#include "frame_decryptor_wrapper.h"
#include "video_renderer.h"
#include "stats.h"

#include <avs_peerflow.h>

#include "peerflow.h"

#define TMR_STATS_INTERVAL      1000
#define TMR_CBR_INTERVAL        2500
#define TMR_RESTART_INTERVAL    2000

#define DOUBLE_ENCRYPTION 1

#define GROUP_PTIME 40

static const char trials_str[] =
	"WebRTC-GenericDescriptorAdvertised/Enabled/WebRTC-GenericDescriptor/Enabled/";

static struct {
	std::unique_ptr<rtc::Thread> thread;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory;	
	bool initialized;

	struct {
		struct lock *lock;
		struct mqueue *q;
		struct list l;
	} mq;
	
	struct lock *lock;
	struct list pfl;

	class LogSink *logsink;	

	struct {
		bool muted;
	} audio;


#if PC_STANDALONE
	struct dnsc *dnsc;
	struct http_cli *http_cli;
#endif
} g_pf = {
	.initialized = false,
	.lock = NULL,
	.pfl = LIST_INIT,
	.audio = {
		  .muted = false,
	},
#if  PC_STANDALONE
	.dnsc = NULL,
	.http_cli = NULL,
#endif
};


struct peerflow {
	struct iflow iflow;
	char *convid;
	char *userid_self;
	char *clientid_self;
	struct conf_member *cm; /* member for 1/1 conv */

	enum icall_call_type call_type;
	enum icall_conv_type conv_type;

	bool gathered;
	enum icall_vstate vstate;
	size_t ncands;

	rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConn;

	webrtc::PeerConnectionInterface::RTCConfiguration *config;
	webrtc::PeerConnectionInterface:: RTCOfferAnswerOptions *offerOptions;
	webrtc::PeerConnectionInterface:: RTCOfferAnswerOptions *answerOptions;

	webrtc::PeerConnectionObserver *observer;
	webrtc::CreateSessionDescriptionObserver *sdpObserver;
	webrtc::CreateSessionDescriptionObserver *addDecoderSdpObserver;
	rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface> sdpRemoteObserver;

	webrtc::SetSessionDescriptionObserver *offerObserver;
	webrtc::SetSessionDescriptionObserver *answerObserver;
	webrtc::RtpReceiverObserverInterface *rtpObserver;
	
	rtc::scoped_refptr<webrtc::RtpSenderInterface> rtpSender;

	struct keystore *keystore;

	rtc::scoped_refptr<wire::CbrDetectorLocal> cbr_det_local;
	rtc::scoped_refptr<wire::CbrDetectorRemote> cbr_det_remote;
	struct tmr tmr_cbr;
	struct tmr tmr_restart;

	struct {
		rtc::scoped_refptr<webrtc::AudioSourceInterface> source;
		rtc::scoped_refptr<webrtc::AudioTrackInterface> track;

		
		bool local_cbr;
		bool remote_cbr;
	} audio;

	struct {
		rtc::scoped_refptr<webrtc::VideoTrackInterface>	track;
		struct list renderl;
		bool negotiated;
	} video;

	struct {
		rtc::scoped_refptr<webrtc::DataChannelInterface> ch;
		webrtc::DataChannelObserver *observer;

	} dc;

	struct iflow_stats stats;

	/* conf members */
	struct {
		struct list list;
		struct lock *lock;
	} cml;

	char *userid_remote;
	char *clientid_remote;

	struct le le;

	struct tmr tmr_stats;
	wire::NetStatsCallback *netStatsCb;
	std::string remoteSdp;
	bool selective_audio;
	bool selective_video;
};


static webrtc::SessionDescriptionInterface *sdp_interface(const char *sdp,
							  webrtc::SdpType type);

static void pf_norelay_handler(bool local, void *arg);


enum {
      MQ_PC_ALLOC          = 0x01,
      MQ_PC_ESTAB          = 0x02,
      MQ_PC_CLOSE          = 0x03,
      MQ_PC_GATHER         = 0x04,
      MQ_PC_RESTART_NOW    = 0x05,
      MQ_PC_RESTART_DELAY  = 0x06,
      MQ_PC_RESTART_CANCEL = 0x07,

      MQ_DC_ESTAB          = 0x10,
      MQ_DC_OPEN           = 0x11,
      MQ_DC_CLOSE          = 0x12,
      MQ_DC_DATA           = 0x13,
      
      MQ_HTTP_SEND         = 0x20,

      MQ_INTERNAL_SET_MUTE = 0x30,
};


struct mq_data {
	struct peerflow *pf;
	int id;
	struct le le;
	bool handled;

	union {
		struct {
			int err;
		} close;

		struct {
			char *sdp;
			char *type;
		} gather;

		struct {
			int id;
			char *label;
		} dcestab;
		
		struct {
			int id;
			struct mbuf *mb;
		} dcdata;
	} u;
};


static bool valid_pf(struct peerflow *pf)
{
	bool found = false;
	struct le *le;

	if (!pf)
		return false;
	
	for(le = g_pf.pfl.head; !found && le; le = le->next) {
		found = le->data == pf;
	}

	return found;
}


static const char *signal_state_name(
	webrtc::PeerConnectionInterface::SignalingState state)
{
	switch(state) {
	case webrtc::PeerConnectionInterface::kStable:
		return "Stable";
		
	case webrtc::PeerConnectionInterface::kHaveLocalOffer:
		return "HaveLocalOffer";

	case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
		return "HaveLocalPrAnswer";

	case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
		return "HaveRemoteOffer";

	case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
		return "HaveRemotePrAnswer";

	case webrtc::PeerConnectionInterface::kClosed:
		return "Closed";

	default:
		return "???";
	}
}


static const char *gathering_state_name(
	webrtc::PeerConnectionInterface::IceGatheringState state)
{
	switch(state) {
		
	case webrtc::PeerConnectionInterface::kIceGatheringNew:
		return "New";

	case webrtc::PeerConnectionInterface::kIceGatheringGathering:
		return "Gathering";
		
	case webrtc::PeerConnectionInterface::kIceGatheringComplete:
		return "Complete";

	default:
		return "???";
	}
}

const char *ice_connection_state_name(
	webrtc::PeerConnectionInterface::IceConnectionState state)
{	
	switch(state) {
	case webrtc::PeerConnectionInterface::kIceConnectionNew:
		return "New";

	case webrtc::PeerConnectionInterface::kIceConnectionChecking:
		return "Checking";

	case webrtc::PeerConnectionInterface::kIceConnectionConnected:
		return "Connected";
		
	case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
		return "Completed";

	case webrtc::PeerConnectionInterface::kIceConnectionFailed:
		return "Failed";
		
	case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
		return "Disconnected";

	case webrtc::PeerConnectionInterface::kIceConnectionClosed:
		return "Closed";

	case webrtc::PeerConnectionInterface::kIceConnectionMax:
		return "Max";
		
	default:
		return "???";
	}
}

static const char *connection_state_name(
	webrtc::PeerConnectionInterface::PeerConnectionState state)
{
	switch(state) {
	case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
		return "New";

	case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
		return "Connecting";

	case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
		return "Connected";

	case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
		return "Disconnected";

	case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
		return "Failed";

	case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
		return "Closed";

	default:
		return "???";
	}
}


class PeerConnectionThread : public rtc::Thread {
public:
	virtual void Run() {
		debug("pf thread: %p running\n", pthread_self());

		ProcessMessages(rtc::ThreadManager::kForever);
	}
};

static void md_destructor(void *arg)
{
	struct mq_data *md = (struct mq_data *)arg;

	switch(md->id) {
	case MQ_PC_GATHER:
		mem_deref(md->u.gather.sdp);
		mem_deref(md->u.gather.type);
		break;

	case MQ_DC_ESTAB:
	case MQ_DC_OPEN:
	case MQ_DC_CLOSE:
		mem_deref(md->u.dcestab.label);
		break;
		
	case MQ_DC_DATA:
		mem_deref(md->u.dcdata.mb);
		break;

	default:
		break;
	}

	list_unlink(&md->le);
}

static void push_mq(struct mq_data *md)
{
	lock_write_get(g_pf.mq.lock);
	list_append(&g_pf.mq.l, &md->le, md);
		
	mqueue_push(g_pf.mq.q, md->id, md);
	lock_rel(g_pf.mq.lock);
	
}

static void send_close(struct peerflow *pf, int err)
{
	struct mq_data *md;
	
	md = (struct mq_data *)mem_zalloc(sizeof(*md), md_destructor);
	if (!md) {
		warning("pf(%p): could not alloc md\n");
		return;
	}
	md->id = MQ_PC_CLOSE;
	md->pf = pf;
	md->u.close.err = err;

	push_mq(md);
}

static void set_all_mute(bool muted)
{
	struct le *le;

	for(le = g_pf.pfl.head; le; le = le->next) {
		struct peerflow *pf = (struct peerflow *)le->data;

		if (pf && pf->audio.track) {
			pf->audio.track->set_enabled(!muted);
		}
	}
}


void timer_restart(void *arg)
{
	struct peerflow *pf = (struct peerflow*)arg;
	info("pf(%p): calling restarth due to network drop\n", pf);

	IFLOW_CALL_CB(pf->iflow, restarth,
		false, pf->iflow.arg);
}


static void handle_mq(struct peerflow *pf, struct mq_data *md, int id)
{
	switch(id) {
	case MQ_PC_ESTAB:
		IFLOW_CALL_CB(pf->iflow, estabh,
			"dtls", "opus", pf->iflow.arg);
		IFLOW_CALL_CB(pf->iflow, rtp_stateh,
			true, true, pf->iflow.arg);
		break;

	case MQ_PC_GATHER:
		IFLOW_CALL_CB(pf->iflow, gatherh,
			pf->iflow.arg);
		break;

	case MQ_PC_CLOSE:
		IFLOW_CALL_CB(pf->iflow, closeh,
			EINTR, pf->iflow.arg);
		break;

	case MQ_PC_RESTART_NOW:
		tmr_start(&pf->tmr_restart, 1, timer_restart, pf);
		break;

	case MQ_PC_RESTART_DELAY:
		tmr_start(&pf->tmr_restart, TMR_RESTART_INTERVAL, timer_restart, pf);
		break;

	case MQ_PC_RESTART_CANCEL:
		tmr_cancel(&pf->tmr_restart);
		break;

	case MQ_DC_OPEN:
		info("%s DC_OPEN pf=%p arg=%p\n", __FUNCTION__, pf, pf->iflow.arg);
		IFLOW_CALL_CB(pf->iflow, dce_estabh,
			pf->iflow.arg);
		break;

	case MQ_DC_ESTAB:
		info("%s DC_ESTAB pf=%p arg=%p\n", __FUNCTION__, pf, pf->iflow.arg);
		break;

	case MQ_DC_CLOSE:
		IFLOW_CALL_CB(pf->iflow, dce_closeh,
			pf->iflow.arg);
		break;

	case MQ_DC_DATA:
		IFLOW_CALL_CB(pf->iflow, dce_recvh,
			      md->u.dcdata.mb->buf,
			      md->u.dcdata.mb->end,
			      pf->iflow.arg);
		break;

	case MQ_INTERNAL_SET_MUTE:
		set_all_mute(g_pf.audio.muted);		
		break;

	default:
		break;
	}

	md->handled = true;
	
}

static void run_mq_on_pf(struct peerflow *pf, bool call_func)
{
	struct le *le;
	
	lock_write_get(g_pf.mq.lock);
	le = g_pf.mq.l.head;
	while (le) {
		struct mq_data *md = (struct mq_data *)le->data;

		md = (struct mq_data *)mem_ref(md);
		le = le->next;
		if (pf == md->pf) {
			if (!call_func) {
				md->handled = true;
			}
			else {
				if (!md->handled) {
					lock_rel(g_pf.mq.lock);
					handle_mq(pf, md, md->id);
					lock_write_get(g_pf.mq.lock);					
				}
				mem_deref(md);
			}
		}
		mem_deref(md);
	}
	lock_rel(g_pf.mq.lock);
}

struct mq_entry {
	struct mq_data *md;

	struct le le;
};

static void mqe_destructor(void *arg)
{
	struct mq_entry *mqe = (struct mq_entry *)arg;

	list_unlink(&mqe->le);
	lock_write_get(g_pf.mq.lock);
	mem_deref(mqe->md);
	lock_rel(g_pf.mq.lock);
}

static void run_all_mq(void)
{
	struct list mql = LIST_INIT;
	struct le *le;
	
	lock_write_get(g_pf.mq.lock);
	le = g_pf.mq.l.head;
	while(le) {
		struct mq_data *md = (struct mq_data *)le->data;
		struct mq_entry *mqe;

		mqe = (struct mq_entry *)mem_zalloc(sizeof(*mqe), mqe_destructor);
		if (!mqe)
			continue;

		mqe->md = md;
		list_append(&mql, &mqe->le, mqe);

		le = le->next;
		list_unlink(&md->le);
	}
	lock_rel(g_pf.mq.lock);
		
	LIST_FOREACH(&mql, le) {
		struct mq_entry *mqe = (struct mq_entry *)le->data;
		struct mq_data *md = mqe->md;
		struct peerflow *pf = md->pf;

		lock_write_get(g_pf.lock);
		if (valid_pf(pf)) {
			pf = (struct peerflow *)mem_ref(pf);
		}
		else {
			debug("pf(%p): handle_all: spurious event: 0x%02x\n", pf, md->id);
			pf = NULL;
		}
		lock_rel(g_pf.lock);
		
		if (pf != NULL) {
			if (!md->handled) {
				handle_mq(pf, md, md->id);
			}
			lock_write_get(g_pf.lock);
			mem_deref(pf);
			lock_rel(g_pf.lock);
		}
	}
	list_flush(&mql);
}

static void mq_handler(int id, void *data, void *arg)
{
	struct mq_data *md = (struct mq_data *)data;
	struct peerflow *pf = md->pf;

	(void)arg;

	switch(id) {
	case MQ_INTERNAL_SET_MUTE:
		pf = NULL;
		break;

	default:
		run_all_mq();
		break;
	}

 out:
	return;
}


static enum log_level severity2level(rtc::LoggingSeverity severity)
{
	switch(severity) {
	case rtc::LS_VERBOSE:
		return LOG_LEVEL_DEBUG;

	case rtc::LS_INFO:
		return LOG_LEVEL_INFO;
		
	case rtc::LS_WARNING:
		return LOG_LEVEL_WARN;
		
	case rtc::LS_ERROR:
		return LOG_LEVEL_ERROR;
		
	default:
		return LOG_LEVEL_WARN;
	}
}

class LogSink : public rtc::LogSink {
public:
	LogSink() {};
	virtual void OnLogMessage(const std::string& msg,
					   rtc::LoggingSeverity severity,
					   const char* tag) {
		
		enum log_level lvl = severity2level(severity);
		loglv(lvl, "[%s] %s", tag, msg.c_str());
	}

	virtual void OnLogMessage(const std::string& msg,
				   rtc::LoggingSeverity severity) {
		
		enum log_level lvl = severity2level(severity);
		loglv(lvl, "%s", msg.c_str());
	}
	
	
	virtual void OnLogMessage(const std::string& msg) {
		error("%s", msg.c_str());
	}	
};


void peerflow_start_log(void)
{
	if (!g_pf.logsink) {
		g_pf.logsink = new LogSink();
		rtc::LogMessage::AddLogToStream(g_pf.logsink, rtc::LS_VERBOSE);
	}
}


static void peerflow_set_mute(bool muted)
{
	struct mq_data *md;
	
	info("pf: set_mute: %d muted=%d\n", muted, g_pf.audio.muted);
	
	if (g_pf.audio.muted == muted)
		return;
	
	g_pf.audio.muted = muted;

	if (!g_pf.initialized) {
		warning("pf: set_mute: not initialized\n");
		return;
	}

	md = (struct mq_data *)mem_zalloc(sizeof(*md), md_destructor);
	if (!md) {
		warning("pf: set_mute: failed to alloc md\n");
		return;
	}
	md->id = MQ_INTERNAL_SET_MUTE;
	push_mq(md);
}

static bool peerflow_get_mute(void)
{
	if (g_pf.initialized)
		return g_pf.audio.muted;
	else
		return false;
}

int peerflow_set_funcs(void)
{
	iflow_set_alloc(peerflow_alloc);

	iflow_register_statics(peerflow_destroy,
			       peerflow_set_mute,
			       peerflow_get_mute);

	return 0;
}

int peerflow_init(void)
{
	webrtc::AudioDeviceModule *adm;
	webrtc::PeerConnectionFactoryDependencies pc_deps;
	cricket::MediaEngineDependencies me_deps;
	int err;

	if (g_pf.initialized)
		return EALREADY;

	info("pf_init\n");
	err = mqueue_alloc(&g_pf.mq.q, mq_handler, NULL);
	if (err)
		goto out;
	
	err = lock_alloc(&g_pf.mq.lock);
	if (err)
		goto out;
	
	list_init(&g_pf.mq.l);

	err = lock_alloc(&g_pf.lock);
	if (err)
		goto out;

	//rtc::LogMessage::LogToDebug(rtc::LS_INFO);

#ifndef ANDROID	/* webrtc logging crashes on Android due to JNI/JNA mix */
	//peerflow_start_log();
#endif
	
	//pf_platform_init();

	g_pf.thread = rtc::Thread::Create();
	g_pf.thread->Start();
#if 1
	g_pf.thread->Invoke<void>(RTC_FROM_HERE, [] {
		info("pf: starting runnable\n");
		pc_platform_init();
		info("pf: platform initialized\n");
	});
#else
	g_pf.thread->BlockingCall([] {
		info("pf: starting runnable\n");
		pc_platform_init();
		info("pf: platform initialized\n");		
	});
#endif

	webrtc::field_trial::InitFieldTrialsFromString(trials_str);

	adm = (webrtc::AudioDeviceModule *)audio_io_create_adm();
	if (adm)
		me_deps.adm = adm;
	me_deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory().release();
	me_deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
	me_deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
	me_deps.video_encoder_factory = webrtc::CreateBuiltinVideoEncoderFactory();
	me_deps.video_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
	me_deps.audio_processing = webrtc::AudioProcessingBuilder().Create();

	pc_deps.signaling_thread = g_pf.thread.get();
	pc_deps.media_engine = cricket::CreateMediaEngine(std::move(me_deps));
	pc_deps.call_factory = webrtc::CreateCallFactory();
	pc_deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();

	//pc_deps.event_log_factory = webrtc::CreateRtcEventLogFactory();
	g_pf.pc_factory = webrtc::CreateModularPeerConnectionFactory(std::move(pc_deps));

	if (!g_pf.pc_factory) {
		err = ENOSYS;
		goto out;
	}

	g_pf.initialized = true;

 out:
	return err;
}

void peerflow_destroy(void)
{
	info("peerflow_destroy: initialized=%d\n", g_pf.initialized);

	wire::CaptureSource::ReleaseInstance();

	if (!g_pf.initialized)
		return;

	// This seems to hang forever
	//g_pf.thread->Stop();

	g_pf.mq.q = (struct mqueue *)mem_deref(g_pf.mq.q);
	list_flush(&g_pf.mq.l);
	g_pf.mq.lock = (struct lock *)mem_deref(g_pf.mq.lock);

	g_pf.lock = (struct lock *)mem_deref(g_pf.lock);

	g_pf.initialized = false;

	rtc::LogMessage::RemoveLogToStream(g_pf.logsink);
	delete g_pf.logsink;
	g_pf.logsink = nullptr;
}


#if PC_STANDALONE
static void send_sdp(const char *sdp, const char *type,
		     struct peerflow *pf)
{
	struct mq_http_send *mqd;
		
	mqd = (struct mq_http_send *)mem_zalloc(sizeof(*mqd), NULL);
		
	struct econn_message *emsg;
	emsg = econn_message_alloc();
	econn_message_init(emsg, ECONN_SETUP, "sess");

	str_ncpy(emsg->src_userid, "aaaa-bbbb",
		 sizeof(emsg->src_userid));
	str_ncpy(emsg->src_clientid, "12345",
		 sizeof(emsg->src_clientid));
	str_dup(&emsg->u.setup.sdp_msg, sdp);
	econn_props_alloc(&emsg->u.setup.props, NULL);

	char *setup_msg;
	econn_message_encode(&setup_msg, emsg);
	mem_deref(emsg);

	mqd->data = (uint8_t *)setup_msg;
	mqd->len = str_len(setup_msg);				
	mqd->arg = (void *)pf;
	
	
	mqueue_push(g_pf.mq, MQ_HTTP_SEND, mqd);
}
#endif

class DataChanObserver : public webrtc::DataChannelObserver {
 public:
	DataChanObserver(struct peerflow *pf,
			 rtc::scoped_refptr<webrtc::DataChannelInterface> dc)
	{
		pf_ = pf;
		dc_ = dc;
	}
	
	// The data channel state have changed.
	virtual void OnStateChange() {

		webrtc::DataChannelInterface::DataState state;
		struct mq_data *md;

		state = dc_->state();

		md = (struct mq_data *)mem_zalloc(sizeof(*md), md_destructor);
		md->pf = pf_;
		md->u.dcestab.id = dc_->id();
		md->id = MQ_DC_OPEN;
		str_dup(&md->u.dcestab.label, dc_->label().c_str());
				
		switch (state) {
		case webrtc::DataChannelInterface::kOpen:			
			if (pf_->dc.ch == nullptr)
				pf_->dc.ch = dc_;

			md->id = MQ_DC_OPEN;
			break;

		case webrtc::DataChannelInterface::kClosed:
			md->id = MQ_DC_CLOSE;
			break;

		default:
			mem_deref(md);
			return;
		}

		push_mq(md);
		//mqueue_push(g_pf.mq, md->id, md);
	}
	
	//  A data buffer was successfully received.
	virtual void OnMessage(const webrtc::DataBuffer& buffer) {

		struct mq_data *md;

		md = (struct mq_data *)mem_zalloc(sizeof(*md), md_destructor);
		md->pf = pf_; //(struct peerflow *)mem_ref(pf_);
		md->u.dcdata.mb = mbuf_alloc(buffer.size());
		if (!md->u.dcdata.mb) {
			warning("pf(%p): dce data: no mbuf\n", pf_);
			mem_deref(md);
			return;
		}

		mbuf_write_mem(md->u.dcdata.mb,
			       (const uint8_t *)buffer.data.cdata(),
			       buffer.size());

		md->u.dcdata.id = dc_->id();
		
		md->id = MQ_DC_DATA;

		push_mq(md);
		//mqueue_push(g_pf.mq, md->id, md);
	}
	
	// The data channel's buffered_amount has changed.
	virtual void OnBufferedAmountChange(uint64_t previous_amount) {
		//printf("DC:bufferAmountChange:%llu\n", previous_amount);
	}

private:
	struct peerflow *pf_;
	rtc::scoped_refptr<webrtc::DataChannelInterface> dc_;
	
};

class RtpObserver : public webrtc::RtpReceiverObserverInterface {
public:
	RtpObserver(rtc::scoped_refptr<webrtc::RtpReceiverInterface> rcvr,
		    struct peerflow *pf)
		:
		rcvr_(rcvr),
		pf_(pf)
	{
	}
	
	// Note: Currently if there are multiple RtpReceivers of the same media type,
	// they will all call OnFirstPacketReceived at once.
	//
	// In the future, it's likely that an RtpReceiver will only call
	// OnFirstPacketReceived when a packet is received specifically for its
	// SSRC/mid.
	virtual void OnFirstPacketReceived(cricket::MediaType media_type)
	{
		std::vector<std::string> streams = rcvr_->stream_ids();
		info("pf(%p): OnFirstPacketReceived: mediatype=%s "
		     "sources: %zu streams=%zu\n",
		     pf_, cricket::MediaTypeToString(media_type).c_str(),
		     rcvr_->GetSources().size(), streams.size());
	}

private:
	rtc::scoped_refptr<webrtc::RtpReceiverInterface> rcvr_;
	struct peerflow *pf_;
};


static void invoke_gather(struct peerflow *pf,
			  const webrtc::SessionDescriptionInterface *isdp)
{
	std::string sdp_str;
	struct mq_data *md;
	const char *type;
	
	type = SdpTypeToString(isdp->GetType());
	
	md = (struct mq_data *)mem_zalloc(sizeof(*md), md_destructor);
	md->id = MQ_PC_GATHER;	
	md->pf = pf;
	str_dup(&md->u.gather.type, type);

	push_mq(md);
}

static void disconnect_timeout_handler(void *arg)
{
	struct peerflow *pf;

	pf = (struct peerflow *)arg;
	IFLOW_CALL_CB(pf->iflow, restarth,
		false, pf->iflow.arg);
}

class AvsPeerConnectionObserver : public webrtc::PeerConnectionObserver {
public:
	AvsPeerConnectionObserver(struct peerflow *pf) {
		pf_ = pf;
	}
	
	~AvsPeerConnectionObserver() {
	}

	// Triggered when the SignalingState changed.
	virtual void OnSignalingChange (
		webrtc::PeerConnectionInterface::SignalingState state) {
		
		info("pf(%p): signaling change: %s\n",
		     pf_, signal_state_name(state));

		switch(state) {
		case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
			if (!pf_->gathered) {
				pf_->peerConn->CreateAnswer(pf_->sdpObserver,
							   *pf_->answerOptions);
			}
			break;

		default:
			break;
		}
	}

	// Triggered when media is received on a new stream from remote peer.
	virtual void OnAddStream (
		rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {

		info("pf(%p): OnAddStream %s added\n",
		     pf_, stream->id().c_str());
	}

	// Triggered when a remote peer closes a stream.
	virtual void OnRemoveStream (
		rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {

		info("pf(%p): stream %s added\n",
		     pf_, stream->id().c_str());
	}

	// Triggered when a remote peer opens a data channel.
	virtual void OnDataChannel (
		rtc::scoped_refptr<webrtc::DataChannelInterface> dc) {

		struct mq_data *md;

		info("pf(%p): data channel %d opened\n", pf_, dc->id());

		webrtc::DataChannelObserver *observer;
		
		observer = new DataChanObserver(pf_, dc);
		dc->RegisterObserver(observer);

		md = (struct mq_data *)mem_zalloc(sizeof(*md), md_destructor);
		if (!md) {
			warning("pf(%p): failed to alloc md\n", pf_);
			return;
		}

		md->pf = pf_;
		md->u.dcestab.id = dc->id();
		str_dup(&md->u.dcestab.label, dc->label().c_str());

		md->id = MQ_DC_ESTAB;

		push_mq(md);
		//mqueue_push(g_pf.mq, md->id, md);
	}

	// Triggered when renegotiation is needed. For example, an ICE restart
	// has begun.
	virtual void OnRenegotiationNeeded() {
		info("pf(%p): renegotiation needed\n", pf_);
	}


	void SendMQMessage(int msgid) {
		struct mq_data *md;
		md = (struct mq_data *)mem_zalloc(sizeof(*md),
						  md_destructor);
		if (!md) {
			warning("pf(%p): could not alloc md\n", pf_);
			return;
		}
		md->pf = pf_;
		md->id = msgid;

		push_mq(md);
	}

	// Called any time the IceConnectionState changes.
	//
	// Note that our ICE states lag behind the standard slightly. The most
	// notable differences include the fact that "failed" occurs after 15
	// seconds, not 30, and this actually represents a
	// combination ICE + DTLS state, so it may be "failed" if DTLS
	// fails while ICE succeeds.
	virtual void OnIceConnectionChange (
	       webrtc::PeerConnectionInterface::IceConnectionState state) {
		struct mq_data *md;

		info("pf(%p): ice connection state: %s\n",
		     pf_, ice_connection_state_name(state));
		
		switch (state) {
		case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected:
			warning("pf(%p): ice connection: %s\n",
			     pf_, ice_connection_state_name(state));

			SendMQMessage(MQ_PC_RESTART_DELAY);
			break;

		case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionFailed:
			warning("pf(%p): ice connection: %s\n",
			     pf_, ice_connection_state_name(state));

			SendMQMessage(MQ_PC_RESTART_NOW);
			break;

		case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected:
		case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionClosed:
			SendMQMessage(MQ_PC_RESTART_CANCEL);
			break;
			
		default:
			info("pf(%p): ice connection: %s\n",
			     pf_, ice_connection_state_name(state));
			break;
		}
			
	}

	// Called any time the PeerConnectionState changes.
	virtual void OnConnectionChange (
	      webrtc::PeerConnectionInterface::PeerConnectionState state) {

		struct mq_data *md;
		
		info("pf(%p): connection change: %s\n",
		     pf_, connection_state_name(state));

		if (!pf_)
			return;

		switch (state) {
		case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:

			md = (struct mq_data *)mem_zalloc(sizeof(*md),
							  md_destructor);
			if (!md) {
				warning("pf(%p): could not alloc md\n", pf_);
				return;
			}
			md->pf = pf_;
			md->id = MQ_PC_ESTAB;

			push_mq(md);
			//mqueue_push(g_pf.mq, md->id, md);
			break;
			
		case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
		{
			wire::CallStatsCallback *cb = new wire::CallStatsCallback(pf_);
                        pf_->peerConn->GetStats(cb);
				
			send_close(pf_, 0);
		}
		break;

		default:
			break;

		}
	}

	// Called any time the IceGatheringState changes.
	virtual void OnIceGatheringChange (
		webrtc::PeerConnectionInterface::IceGatheringState state) {

		const webrtc::SessionDescriptionInterface *isdp;
				
		info("pf(%p): IceGathering: %s gathered=%s ncands=%d\n",
		     pf_, gathering_state_name(state),
		     pf_->gathered ? "yes" : "no",
		     pf_->ncands);
		
		switch(state) {
		case webrtc::PeerConnectionInterface::kIceGatheringGathering:
			break;

		case webrtc::PeerConnectionInterface::kIceGatheringComplete:
			if (pf_->gathered)
				return;

			isdp = pf_->peerConn->local_description();
			if (!isdp) {
				warning("pf(%p): ice gathering "
					"no local SDP\n", pf_);
				return;
			}

			// Check that we have at least 1 candidate
			if (pf_->ncands < 1) {
				info("pc(%p): ice gathering no candidates\n", pf_);

				/*
				if (isdp->GetType() == webrtc::SdpType::kOffer) {
					pf_->peerConn->CreateOffer(pf_->sdpObserver, *pf_->offerOptions);
				}
				else {
					pf_->peerConn->CreateAnswer(pf_->sdpObserver, *pf_->answerOptions);
				}
				*/
				return;
			}
			pf_->gathered = true;
			invoke_gather(pf_, isdp);
			break;

		default:
			break;
		}

	out:
		return;
	}

	// A new ICE candidate has been gathered.
	virtual void OnIceCandidate(const webrtc::IceCandidateInterface* icand)
	{
		std::string cand_str;
		const cricket::Candidate& cand = icand->candidate();

		icand->ToString(&cand_str);
		info("pf(%p): OnIceCandidate: %s has %d candidates\n",
		     pf_, cand_str.c_str(), pf_->ncands);

		++pf_->ncands;

		if (!pf_->gathered && cand.type() == std::string("relay")) {
			const webrtc::SessionDescriptionInterface *isdp;
			
			info("pf(%p): received first RELAY candidate\n", pf_);

			pf_->gathered = true;
			isdp = pf_->peerConn->local_description();
			if (isdp)
				invoke_gather(pf_, isdp);
			else {
				warning("pf(%p): ice candidate "
					"no local SDP\n", pf_);
				return;
			}
			
		}
	}

	// Ice candidates have been removed.
	virtual void OnIceCandidatesRemoved(
		const std::vector<cricket::Candidate>& candidates) {
	}

	// Called when the ICE connection receiving status changes.
	virtual void OnIceConnectionReceivingChange(bool receiving) {
	}

	// This is called when a receiver and its track are created.
	// Note: This is called with both Plan B and Unified Plan semantics.
	// Unified Plan users should prefer OnTrack,
	// OnAddTrack is only called as backwards
	// compatibility
	// (and is called in the exact same situations as OnTrack).
	virtual void OnAddTrack(
		rtc::scoped_refptr<webrtc::RtpReceiverInterface> rx,
		const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {
		rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track =
			rx->track();
		char *label = NULL;
		char *kind = NULL;
		const char *userid = NULL;
		const char *clientid = NULL;
		const char *dec_uid = NULL;
		int err = 0;

		if (track == nullptr)
			return;

		str_dup(&label, track->id().c_str());
		str_dup(&kind, track->kind().c_str());
			
		info("pf(%p): OnAddTrack %s track added id=%s\n", pf_, kind, label);

		track->set_enabled(true);

		struct conf_member *cm;

		lock_write_get(pf_->cml.lock);
		cm = conf_member_find_by_label(&pf_->cml.list, label);
		if (cm) {
			userid = cm->userid;
			clientid = cm->clientid;
			dec_uid = cm->userid_hash;
		}
		else if (pf_->conv_type == ICALL_CONV_TYPE_CONFERENCE) {
			userid = "";
			clientid = "";
		}
		else if (pf_->conv_type != ICALL_CONV_TYPE_CONFERENCE &&
			 pf_->userid_remote && pf_->clientid_remote) {
			userid = pf_->userid_remote;
			clientid = pf_->clientid_remote;
		}
		else {
			warning("pf(%p); no conf member for label: %s\n",
				pf_, label);
			lock_rel(pf_->cml.lock);
			goto out;
		}
		lock_rel(pf_->cml.lock);

		if (streq(kind, webrtc::MediaStreamTrackInterface::kVideoKind))
		{
			rtc::VideoSinkWants vsw;
			webrtc::VideoTrackInterface *vtrack =
				(webrtc::VideoTrackInterface *)track.get();
			struct render_le *render;
			wire::VideoRendererSink *sink = new wire::VideoRendererSink(pf_,
				userid, clientid);

			render = (struct render_le *)mem_zalloc(sizeof(*render),
				render_destructor);
			render->sink = sink;
			render->track = vtrack;
			list_append(&pf_->video.renderl, &render->le, render);
			vtrack->AddOrUpdateSink(sink, vsw);

#if DOUBLE_ENCRYPTION
			if (pf_->conv_type == ICALL_CONV_TYPE_CONFERENCE) {
				rtc::scoped_refptr<wire::FrameDecryptor> decryptor(
					rtc::make_ref_counted<wire::FrameDecryptor>(
					FRAME_MEDIA_VIDEO, pf_));
				err = decryptor->SetKeystore(pf_->keystore);
				if (err) {
					warning("pf(%p): failed to set keystore for "
						"decryptor (%m)\n",
						pf_, err);
				}
				if (dec_uid) {
					err = decryptor->SetUserID(dec_uid);
					if (err) {
						warning("pf(%p): failed to set userid for "
							"decryptor (%m)\n",
							pf_, err);
					}
				}
				rx->SetFrameDecryptor(decryptor);
			}
#endif
		}
		else if (streq(kind, webrtc::MediaStreamTrackInterface::kAudioKind))
		{
#if DOUBLE_ENCRYPTION
			if (pf_->conv_type == ICALL_CONV_TYPE_CONFERENCE) {
				rtc::scoped_refptr<wire::FrameDecryptor> decryptor(
					rtc::make_ref_counted<wire::FrameDecryptor>(
					FRAME_MEDIA_AUDIO, pf_));
				err = decryptor->SetKeystore(pf_->keystore);
				if (err) {
					warning("pf(%p): failed to set keystore for "
						"decryptor (%m)\n",
						pf_, err);
				}
				if (dec_uid) {
					err = decryptor->SetUserID(dec_uid);
					if (err) {
						warning("pf(%p): failed to set userid for "
							"decryptor (%m)\n",
							pf_, err);
					}
				}
				rx->SetFrameDecryptor(decryptor);
			}
			else
#endif
			if (pf_->conv_type == ICALL_CONV_TYPE_ONEONONE) {
				rx->SetFrameDecryptor(pf_->cbr_det_remote);
			}
		}

out:
		mem_deref(label);
		mem_deref(kind);
	}

	// This is called when signaling indicates a transceiver will
	// be receiving media from the remote endpoint.
	// This is fired during a call to
	// SetRemoteDescription. The receiving track can be accessed by:
	// |transceiver->receiver()->track()| and its associated streams by
	// |transceiver->receiver()->streams()|.
	// Note: This will only be called if Unified Plan semantics
	// are specified.
	// This behavior is specified in section 2.2.8.2.5 of the "Set the
	// RTCSessionDescription" algorithm:
	// https://w3c.github.io/webrtc-pf/#set-description
	virtual void OnTrack(
		rtc::scoped_refptr<webrtc::RtpTransceiverInterface> txrx) {

		info("pf(%p): OnTrack: track added: id=%s\n",
		     pf_, txrx->receiver()->id().c_str());

		//rtc::VideoSinkWants vsw;
		//receiver->track()->AddOrUpdateSink(new VideoRendererSink(), vsw);
		
	}

	// Called when signaling indicates that media will no longer
	// be received on a track.
	// With Plan B semantics, the given receiver will have been
	// removed from the PeerConnection and the track muted.
	// With Unified Plan semantics, the receiver will remain but the
	// transceiver will have changed direction to either sendonly or
	//inactive.
	// https://w3c.github.io/webrtc-pf/#process-remote-track-removal
	virtual void OnRemoveTrack(
		rtc::scoped_refptr<webrtc::RtpReceiverInterface> rx) {

		rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track =
			rx->track();

		struct le *le;

		LIST_FOREACH(&pf_->video.renderl, le) {
			struct render_le *render;
			render = (struct render_le *)le->data;
			
			if (render->track == track) {
				list_unlink(&render->le);
				break;
			}
		}
	}

	// Called when an interesting usage is detected by WebRTC.
	// An appropriate action is to add information about the context of the
	// PeerConnection and write the event to some kind of
	// "interesting events" log function.
	// The heuristics for defining what constitutes "interesting" are
	// implementation-defined.
	virtual void OnInterestingUsage(int usage_pattern) {
	}

private:
	struct peerflow *pf_;
};

class OfferObserver : public webrtc::SetSessionDescriptionObserver {
public:
	OfferObserver(struct peerflow *pf)
		:
		pf_(pf) {
	};

	virtual ~OfferObserver() {
	};

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	virtual void OnSuccess() {
		bool success;
		
		info("pf(%p): setSdpOffer successfull\n", pf_);
	}

	virtual void OnFailure(webrtc::RTCError err) {

		warning("pf(%p): setSdpOffer failed: %s\n",
			pf_, err.message());

		send_close(pf_, EINTR);
	}

private:
	struct peerflow *pf_;
};


class AnswerObserver : public webrtc::SetSessionDescriptionObserver {
public:
	AnswerObserver(struct peerflow *pf)
		:
		pf_(pf) {
	};

	virtual ~AnswerObserver() {
	};

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	virtual void OnSuccess() {
		bool success;

		info("pf(%p): setSdpAnswer sucessfull\n", pf_);
	}

	virtual void OnFailure(webrtc::RTCError err) {
		warning("pf(%p): setSdpAnswer failed: %s\n",
			pf_, err.message());

		send_close(pf_, EINTR);
	}

private:
	struct peerflow *pf_;
};

class SdpRemoteObserver
	:
	public webrtc::SetRemoteDescriptionObserverInterface {
public:
	SdpRemoteObserver(struct peerflow *pf)
		:
		pf_(pf) {
	};
	~SdpRemoteObserver() {
	};

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	
	virtual void OnSetRemoteDescriptionComplete(webrtc::RTCError err) {		
	}

private:
	struct peerflow *pf_;
};


webrtc::SessionDescriptionInterface *sdp_interface(const char *sdp,
						   webrtc::SdpType type)
{
	webrtc::SdpParseError parse_err;
	webrtc::SessionDescriptionInterface *isdp = nullptr;

	if (!sdp)
		return nullptr;

	switch (type) {
	case webrtc::SdpType::kOffer:
		isdp = webrtc::CreateSessionDescription(webrtc::SessionDescriptionInterface::kOffer, sdp, &parse_err);
		break;
		
	case webrtc::SdpType::kAnswer:
		isdp = webrtc::CreateSessionDescription(webrtc::SessionDescriptionInterface::kAnswer, sdp, &parse_err);
		break;

	default:
		break;
	}

	if (isdp == nullptr) {
		warning("peerflow: failed to parse SDP: "
			"line=%s reason=%s\n",
			parse_err.line.c_str(),
			parse_err.description.c_str());
	}

	return isdp;
}


class SdpObserver : public webrtc::CreateSessionDescriptionObserver {
public:
	SdpObserver(struct peerflow *pf)
		:
		pf_(pf) {
	};

	~SdpObserver() {
	};

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	virtual void OnSuccess(webrtc::SessionDescriptionInterface *isdp) {
		
		webrtc::SdpType type;
		struct sdp_session *sess = NULL;
		std::string sdp_str;
		const char *sdp = NULL;
		webrtc::SessionDescriptionInterface *imod_sdp = nullptr;
		int err;

		type = isdp->GetType();
		info("pf(%p): SDP-%s created successfully\n",
		     pf_, SdpTypeToString(type));

		if (!isdp->ToString(&sdp_str)) {
			warning("pf(%p): ToString failed\n");
			return;
		}
		
		
		switch(type) {
		case webrtc::SdpType::kOffer:
			err = sdp_dup(&sess, pf_->conv_type,
				      sdp_str.c_str(), true);
			if (err) {
				warning("pf(%p): sdp_dup failed: %m\n", err);
				return;
			}

			sdp_check(sdp_str.c_str(), true, true, NULL, pf_norelay_handler, NULL, pf_);
			sdp = sdp_modify_offer(sess, pf_->conv_type, pf_->audio.local_cbr);

			info("SDP-fromPC: %s\n", sdp);
			
			imod_sdp = sdp_interface(sdp, webrtc::SdpType::kOffer);
			pf_->peerConn->SetLocalDescription(
					pf_->offerObserver,
					imod_sdp);
			break;

		case webrtc::SdpType::kAnswer:			
			err = sdp_dup(&sess, pf_->conv_type,
				      sdp_str.c_str(), false);
			if (err) {
				warning("pf(%p): sdp_dup failed: %m\n", err);
				return;
			}
			
			sdp_check(sdp_str.c_str(), true, false, NULL, pf_norelay_handler, NULL, pf_);

			sdp = sdp_modify_answer(sess, pf_->conv_type, pf_->audio.local_cbr);
			imod_sdp = sdp_interface(sdp, webrtc::SdpType::kAnswer);
			pf_->peerConn->SetLocalDescription(
					pf_->answerObserver,
					imod_sdp);
			break;

		default:
			break;
		}

		mem_deref((void *)sess);
		mem_deref((void *)sdp);
	}

	virtual void OnFailure(webrtc::RTCError err) {
		warning("pf(5p); SDP-Failure: %s\n", err.message());
	}

private:
	struct peerflow *pf_;
};


class AddDecoderObserver
       :
       public webrtc::SetRemoteDescriptionObserverInterface {
public:
       AddDecoderObserver(struct peerflow *pf)
               :
               pf_(pf) {
       };
       ~AddDecoderObserver() {
       };

       void AddRef() const {
       }

       virtual rtc::RefCountReleaseStatus Release() const {
               return rtc::RefCountReleaseStatus::kDroppedLastRef;
       }
       
       virtual void OnSetRemoteDescriptionComplete(webrtc::RTCError err) {

               info("AddDecoderObserver::OnSetRemote: remote SDP complete: %s\n", err.message());
               if (err.ok()) {
                       pf_->peerConn->CreateAnswer(pf_->addDecoderSdpObserver, *pf_->answerOptions);
               }
       }

private:
       struct peerflow *pf_;
};

class AddDecoderSdpObserver : public webrtc::CreateSessionDescriptionObserver {
public:
	AddDecoderSdpObserver(struct peerflow *pf)
		:
		pf_(pf) {
	};

	~AddDecoderSdpObserver() {
	};

	void AddRef() const {
	}

	virtual rtc::RefCountReleaseStatus Release() const {
		return rtc::RefCountReleaseStatus::kDroppedLastRef;
	}
	virtual void OnSuccess(webrtc::SessionDescriptionInterface *isdp) {
		webrtc::SdpType type;

		type = isdp->GetType();
		info("pf(%p): addDecoder SDP-%s\n",
		     pf_, SdpTypeToString(type));

		pf_->peerConn->SetLocalDescription(pf_->answerObserver,
						   isdp);
	}

	virtual void OnFailure(webrtc::RTCError err) {
		error("pf(%p): SDP-Failure: %s\n", err.message());
	}

private:
	struct peerflow *pf_;
};


static void timer_cbr(void *arg)
{
	struct peerflow *pf = (struct peerflow *)arg;
	bool cbr_detected;

	if (pf && pf->cbr_det_local && pf->cbr_det_remote) {
		cbr_detected = pf->cbr_det_local->Detected()
			&& pf->cbr_det_remote->Detected();

		IFLOW_CALL_CB(pf->iflow, acbr_detecth,
			cbr_detected ? 1 : 0, pf->iflow.arg);
	}
	tmr_start(&pf->tmr_cbr, TMR_CBR_INTERVAL, timer_cbr, pf);
}


static int create_pf(struct peerflow *pf)
{
	cricket::AudioOptions auopts;
	rtc::VideoSinkWants vsw;
	bool recv_video;
	int err = 0;

	recv_video = pf->call_type != ICALL_CALL_TYPE_FORCED_AUDIO;
	
	pf->offerOptions =
		new webrtc::PeerConnectionInterface::RTCOfferAnswerOptions(
				recv_video, 1, false, true, true);
	pf->answerOptions =
		new webrtc::PeerConnectionInterface::RTCOfferAnswerOptions(
				recv_video, 1, false, true, true);
	
	pf->config->tcp_candidate_policy =
		webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled;
	pf->config->bundle_policy =
		webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
	pf->config->sdp_semantics =
		webrtc::SdpSemantics::kUnifiedPlan;

#if DOUBLE_ENCRYPTION
	if (pf->conv_type == ICALL_CONV_TYPE_CONFERENCE) {
		webrtc::CryptoOptions crypto = webrtc::CryptoOptions::NoGcm();
		crypto.sframe.require_frame_encryption = true;
		pf->config->crypto_options = crypto;
	}
#endif

	bool privacy = msystem_get_privacy(msystem_instance());
	if (privacy)
		pf->config->type = webrtc::PeerConnectionInterface::kRelay; 
	else 
		pf->config->type = webrtc::PeerConnectionInterface::kAll; 

	std::unique_ptr<cricket::PortAllocator> port_allocator = nullptr; 
	
	struct msystem_proxy *proxy = msystem_get_proxy();

	if (proxy) {
		rtc::ProxyInfo pri;
		rtc::SocketAddress sa(proxy->host, proxy->port);
		rtc::PhysicalSocketServer socket_server;


		pri.type = rtc::PROXY_HTTPS;
		pri.address = sa;

		port_allocator = absl::make_unique<cricket::BasicPortAllocator>(
					   new rtc::BasicNetworkManager(&socket_server),
					   nullptr);
		
		port_allocator->set_proxy("wire-call", pri);
	}

	webrtc::PeerConnectionDependencies deps(pf->observer);

	deps.allocator = std::move(port_allocator);
	webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::PeerConnectionInterface>> pcorerr;
	pcorerr = g_pf.pc_factory->CreatePeerConnectionOrError(
					*pf->config,
					std::move(deps));
	if (!pcorerr.ok()) {
		warning("peerflow(%p): failed to create PC\n", pf);
		err = ENOENT;
		goto out;
	}

	pf->peerConn = pcorerr.value();
	info("pf(%p): created PC=%p\n", pf, pf->peerConn.get());

	auopts.echo_cancellation = true;
	auopts.auto_gain_control = true;
	auopts.noise_suppression = true;
	
	pf->audio.source = g_pf.pc_factory->CreateAudioSource(auopts);
	pf->audio.track = g_pf.pc_factory->CreateAudioTrack("audio", pf->audio.source.get());
	if (g_pf.audio.muted)
		pf->audio.track->set_enabled(false);

	pf->cbr_det_local = new wire::CbrDetectorLocal();
	pf->cbr_det_remote = new wire::CbrDetectorRemote();

	if (pf->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		tmr_start(&pf->tmr_cbr, TMR_CBR_INTERVAL, timer_cbr, pf);
	}
	pf->rtpSender = pf->peerConn->AddTrack(pf->audio.track,
					       {rtc::CreateRandomUuid()}).value();

#if DOUBLE_ENCRYPTION
	if (pf->conv_type == ICALL_CONV_TYPE_CONFERENCE && pf->keystore) {
		rtc::scoped_refptr<wire::FrameEncryptor> encryptor;
		encryptor = new wire::FrameEncryptor(pf->userid_self,
						     FRAME_MEDIA_AUDIO);
		err = encryptor->SetKeystore(pf->keystore);
		if (err) {
			warning("pf(%p): failed to set keystore for "
				"encryptor (%m)\n",
				pf, err);
			goto out;
		}
		pf->rtpSender->SetFrameEncryptor(encryptor);
	} else
#endif
	if (pf->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		pf->rtpSender->SetFrameEncryptor(pf->cbr_det_local);
	}

	if (pf->call_type == ICALL_CALL_TYPE_VIDEO ||
	    pf->vstate != ICALL_VIDEO_STATE_STOPPED) {

		webrtc::VideoTrackSourceInterface *src = wire::CaptureSource::GetInstance();

		pf->video.track = g_pf.pc_factory->CreateVideoTrack(
			 "foo",
			 src);

		rtc::scoped_refptr<webrtc::RtpSenderInterface> video_track =
			pf->peerConn->AddTrack(pf->video.track,
					       {rtc::CreateRandomUuid()}).value();
		webrtc::RtpParameters params = video_track->GetParameters();
		if (params.encodings.size() > 0) {
			params.encodings[0].max_framerate = 15;
		}
		else {
			webrtc::RtpEncodingParameters enc;
			enc.max_framerate = 15;
			params.encodings.push_back(enc);
		}
		params.degradation_preference = webrtc::DegradationPreference::MAINTAIN_RESOLUTION;

		video_track->SetParameters(params);


#if DOUBLE_ENCRYPTION
		if (pf->conv_type == ICALL_CONV_TYPE_CONFERENCE && pf->keystore) {
			rtc::scoped_refptr<wire::FrameEncryptor> encryptor;
			encryptor = new wire::FrameEncryptor(pf->userid_self,
							     FRAME_MEDIA_VIDEO);
			err = encryptor->SetKeystore(pf->keystore);
			if (err) {
				warning("pf(%p): failed to set keystore for "
					"encryptor (%m)\n",
					pf, err);
				goto out;
			}
			video_track->SetFrameEncryptor(encryptor);
		}
#endif
	}

 out:
	return err;
}


static void pf_destructor(void *arg)
{
	struct peerflow *pf = (struct peerflow *)arg;

	debug("pf(%p): destructor\n", pf);

	pf->netStatsCb->setActive(false);
	tmr_cancel(&pf->tmr_stats);

	delete pf->netStatsCb;
	pf->netStatsCb = NULL;

	tmr_cancel(&pf->tmr_cbr);
	tmr_cancel(&pf->tmr_restart);
	pf->video.track = NULL;
	pf->audio.track = NULL;
	pf->audio.source = NULL;
	pf->dc.ch = NULL;

	run_mq_on_pf(pf, false);
	
	list_unlink(&pf->le);

	delete pf->offerOptions;
	delete pf->answerOptions;
	delete pf->config;
	delete pf->observer;

	pf->peerConn = NULL;
	pf->sdpObserver = NULL;
	pf->sdpRemoteObserver = NULL;
	pf->offerObserver = NULL;
	pf->answerObserver = NULL;
	pf->rtpSender = NULL;

	mem_deref(pf->keystore);
	mem_deref(pf->convid);
	mem_deref(pf->userid_self);
	mem_deref(pf->userid_remote);
	mem_deref(pf->clientid_self);
	mem_deref(pf->clientid_remote);
	mem_deref(pf->cm);

	list_flush(&pf->cml.list);
	mem_deref(pf->cml.lock);

	list_flush(&pf->video.renderl);
}

static void timer_stats(void *arg)
{
	struct peerflow *pf = (struct peerflow *)arg;

	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> trxs;

	if (!pf || !pf->peerConn)
		goto out;

	trxs = pf->peerConn->GetTransceivers();

	for(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> trx: trxs) {
		rtc::scoped_refptr<webrtc::RtpReceiverInterface> rx = trx->receiver();
		std::vector<webrtc::RtpSource> sources;

		if (rx) {
			sources = rx->GetSources();
		}

		lock_write_get(pf->cml.lock);
		for(webrtc::RtpSource src: sources) {
			uint32_t ssrc = src.source_id();
			struct conf_member *cm = conf_member_find_by_ssrca(&pf->cml.list, ssrc);
			uint8_t level = src.audio_level() ? *src.audio_level() : 127;
			if (cm) {
				float flevel = powf(10.0f, -level / 30.0f) * 255.0f;
				conf_member_set_audio_level(cm, (uint8_t)flevel);
			}
		}
		lock_rel(pf->cml.lock);
	}

	pf->peerConn->GetStats(pf->netStatsCb);

 out:
	tmr_start(&pf->tmr_stats, TMR_STATS_INTERVAL, timer_stats, pf);
}

static int get_oneonone_aulevel(struct peerflow *pf,
				struct list *levell)
{
	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> trxs;
	int err = 0;

	if (!pf || !pf->peerConn)
		return ENOENT;

	trxs = pf->peerConn->GetTransceivers();
	for(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> trx: trxs) {
		rtc::scoped_refptr<webrtc::RtpReceiverInterface> rx = trx->receiver();
		std::vector<webrtc::RtpSource> sources;

		if (rx) {
			sources = rx->GetSources();
		}
		if (sources.empty())
			continue;
		
		webrtc::RtpSource src = sources[0]; 
		uint8_t level = src.audio_level() ? *src.audio_level() : 127;
		float flevel = powf(10.0f, -level / 30.0f) * 255.0f;

		if (!pf->cm)
			return ENOENT;

		conf_member_set_audio_level(pf->cm, (uint8_t)flevel);
		if (pf->cm->audio_level > AUDIO_LEVEL_FLOOR
		     || pf->cm->audio_level_smooth > 0) {
			struct audio_level *aulevel;

			err = audio_level_alloc(&aulevel, levell, false,
						pf->cm->userid,
						pf->cm->clientid,
						pf->cm->audio_level,
						pf->cm->audio_level_smooth);
		}
	}
	
	return err;
}

static int peerflow_get_aulevel(struct iflow *iflow,
				struct list *levell)
{
	struct peerflow *pf = (struct peerflow *)iflow;
	struct audio_level *aulevel;
	struct le *le;
	int err = 0;

	if (!levell)
		return EINVAL;	

	if (pf->stats.audio_level > AUDIO_LEVEL_FLOOR ||
	    pf->stats.audio_level_smooth > 0) {
		err = audio_level_alloc(&aulevel, levell, true,
					pf->userid_self, pf->clientid_self,
					pf->stats.audio_level,
					pf->stats.audio_level_smooth);
	}
	if (err)
		goto out;

	if (pf->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		err = get_oneonone_aulevel(pf, levell);
	}
	else {	
		lock_write_get(pf->cml.lock);
		LIST_FOREACH(&pf->cml.list, le) {
			struct conf_member *cm = (struct conf_member *)le->data;

			if (!cm || !cm->active)
				continue;

			if (cm->userid && cm->clientid &&
			    (cm->audio_level > AUDIO_LEVEL_FLOOR
			     || cm->audio_level_smooth > 0)) {
				err = audio_level_alloc(&aulevel, levell, false,
							cm->userid,
							cm->clientid,
							cm->audio_level,
							cm->audio_level_smooth);
				/* Make sure to count down the level if
				 * the source is no longer in the list due to
				 * selective audio.
				 */
				conf_member_set_audio_level(cm, 0);
				if (err) {
					lock_rel(pf->cml.lock);
					goto out;
				}
			}
		}
		lock_rel(pf->cml.lock);
	}
	list_sort(levell, audio_level_list_cmp, pf);

 out:
	if (err)
		list_flush(levell);

	return err;
}

int peerflow_alloc(struct iflow		**flowp,
		   const char		*convid,
		   const char		*userid_self,
		   const char		*clientid_self,
		   enum icall_conv_type	conv_type,
		   enum icall_call_type	call_type,
		   enum icall_vstate	vstate,
		   void			*extarg)
{
	struct peerflow *pf;
	int err = 0;

	info("pf_alloc: initialized=%d call_type=%d vstate=%s\n",
	     g_pf.initialized, call_type, icall_vstate_name(vstate));
	if (!g_pf.initialized) {
		peerflow_init();
		if (!g_pf.initialized)
			return ENOSYS;
	}
	
	pf = (struct peerflow *) mem_zalloc(sizeof(*pf),
						  pf_destructor);
	if (!pf)
		return ENOMEM;
	iflow_set_functions(&pf->iflow,
			    peerflow_set_video_state,
			    peerflow_generate_offer,
			    peerflow_generate_answer,
			    peerflow_handle_offer,
			    peerflow_handle_answer,
			    peerflow_has_video,
			    peerflow_is_gathered,
			    NULL, // peerflow_enable_privacy
			    peerflow_set_call_type,
			    peerflow_get_audio_cbr,
			    peerflow_set_audio_cbr,
			    peerflow_set_remote_userclientid,
			    peerflow_add_turnserver,
			    peerflow_gather_all_turn,
			    peerflow_add_decoders_for_user,
			    peerflow_remove_decoders_for_user,
			    peerflow_sync_decoders,
			    peerflow_set_keystore,
			    peerflow_dce_send,
			    peerflow_stop_media,
			    peerflow_close,
			    peerflow_get_stats,
			    peerflow_get_aulevel,			    
			    peerflow_debug);

	str_dup(&pf->convid, convid);
	str_dup(&pf->userid_self, userid_self);
	str_dup(&pf->clientid_self, clientid_self);
	pf->conv_type = conv_type;
	pf->call_type = call_type;
	pf->vstate = vstate;
	
	pf->config = new webrtc::PeerConnectionInterface::RTCConfiguration();
	pf->observer = new AvsPeerConnectionObserver(pf);

	err = lock_alloc(&pf->cml.lock);
	if (err)
		goto out;

#if 0
	pf->dc.ch = pf->pf->CreateDataChannel("calling-3.0", nullptr);
	if (!pf->dc.ch) {
		warning("peerflow_alloc: no data channel\n");
		err = ENOSYS;
		goto out;
	}

	pf->dc.observer = new DataChanObserver(pf, pf->dc.ch);
	pf->dc.ch->RegisterObserver(pf->dc.observer);
#endif
	pf->sdpObserver = new SdpObserver(pf);
	pf->addDecoderSdpObserver = new AddDecoderSdpObserver(pf);	
	pf->sdpRemoteObserver = new SdpRemoteObserver(pf);
	pf->offerObserver = new OfferObserver(pf);
	pf->answerObserver = new AnswerObserver(pf);
	
	lock_write_get(g_pf.lock);
	list_append(&g_pf.pfl, &pf->le, pf);
	lock_rel(g_pf.lock);

	pf->netStatsCb = new wire::NetStatsCallback(pf);

	tmr_start(&pf->tmr_stats, TMR_STATS_INTERVAL, timer_stats, pf);
 out:
	if (err)
		mem_deref(pf);
	else {
		if (flowp)
			*flowp = (struct iflow*)pf;
	}
		
	return err;
}

int peerflow_add_turnserver(struct iflow *iflow,
			    const char *url,
			    const char *username,
			    const char *password)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	webrtc::PeerConnectionInterface::IceServer server;

	if (!pf || !url)
		return EINVAL;

	if (!pf->config)
		return ENOSYS;

	info("pf_add_turn: %s:%s@%s\n", username, password, url);

	server.urls.push_back(url);
	//server.uri = std::string(url);
	server.username = std::string(username);
	server.password = std::string(password);
	server.tls_cert_policy =
		webrtc::PeerConnectionInterface::kTlsCertPolicyInsecureNoCheck;

	pf->config->servers.push_back(server);
	
	return 0;
}


bool peerflow_is_gathered(const struct iflow *iflow)
{
	const struct peerflow *pf = (const struct peerflow*)iflow;
	webrtc::PeerConnectionInterface::IceGatheringState state;
	
	if (!pf)
		return false;

	if (!pf->peerConn)
		return false;

	state = pf->peerConn->ice_gathering_state();
	info("pf(%p): is_gathered state %d\n", iflow, state);

	return pf->gathered;	
}

#if 0
static void set_group_audio_params(struct peerflow *pf)
{
	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> trxs;

	trxs = pf->peerConn->GetTransceivers();

	for(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> trx: trxs) {
		rtc::scoped_refptr<webrtc::RtpSenderInterface> tx = trx->sender();
		if (tx->media_type() != cricket::MEDIA_TYPE_AUDIO)
			continue;
		
		webrtc::RtpParameters params = tx->GetParameters();
		std::vector<webrtc::RtpEncodingParameters> encs = params.encodings;
		for(webrtc::RtpEncodingParameters enc: encs)  {
			enc.dtx = webrtc::DtxStatus::ENABLED;
			enc.ptime = GROUP_PTIME;
		}
		tx->SetParameters(params);
	}
}
#endif


int peerflow_gather_all_turn(struct iflow *iflow, bool offer)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	webrtc::PeerConnectionInterface::SignalingState state;
	int err = 0;

	if (!pf)
		return EINVAL;

	pf->gathered = false;
	pf->video.negotiated = pf->call_type == ICALL_CALL_TYPE_VIDEO;
	
	if (!pf->peerConn) {
		err = create_pf(pf);
		if (err)
			goto out;
	}
	
	state = pf->peerConn->signaling_state();
	info("pf(%p): gather in signal state: %s \n",
	     pf, signal_state_name(state));

	if (offer) {
		webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::DataChannelInterface>> ch_or_err;
		ch_or_err = pf->peerConn->CreateDataChannelOrError("calling-3.0",
								   nullptr);
		if (ch_or_err.ok()) {
			pf->dc.ch = ch_or_err.value();
			pf->dc.observer = new DataChanObserver(pf, pf->dc.ch);
			pf->dc.ch->RegisterObserver(pf->dc.observer);
		}
		else {
			warning("pf(%p): no data channel\n", pf);
		}

#if 0 /* Don't set audio parameters until they are supported by webrtc */
		if (pc->group_mode != MEDIAFLOW_MODE_ONEONONE) {
			set_group_audio_params(pc);
		}
#endif
		
		pf->peerConn->CreateOffer(pf->sdpObserver, *pf->offerOptions);
	}
	else if (state == webrtc::PeerConnectionInterface::kHaveRemoteOffer)
		pf->peerConn->CreateAnswer(pf->sdpObserver, *pf->answerOptions);

out:
	return err;
}


bool peerflow_is_dcopen(struct peerflow *pf)
{
	if (!pf)
		return false;

	if (!pf->dc.ch)
		return false;
	
	return pf->dc.ch->state() == webrtc::DataChannelInterface::kOpen;
}

int peerflow_dcid(struct peerflow *pf)
{
	if (!pf)
		return -1;
	if (!pf->dc.ch)
		return -1;
	
	return pf->dc.ch->id();
}

int peerflow_dce_send(struct iflow *flow,
		      const uint8_t *data,
		      size_t len)
{
	struct peerflow *pf = (struct peerflow*)flow;
	if (!pf)
		return EINVAL;

	if (!pf->dc.ch) {
		warning("peerflow(%p): no data channel\n", pf);
		return ENOENT;
	}
	
	std::string txt((const char *)data, len);
	pf->dc.ch->Send(webrtc::DataBuffer(txt));

	return 0;
}


static void pf_acbr_handler(bool enabled, bool offer, void *arg)
{
	struct peerflow *pf = (struct peerflow*)arg;
	bool local_cbr;

	local_cbr = pf->audio.local_cbr;
	pf->audio.local_cbr = enabled;
	pf->audio.remote_cbr = enabled;

	if (offer) {
		/* If this is an offer, then we just need to set
		 * the local cbr
		 */
	}		
	else if (enabled && !local_cbr) {
		IFLOW_CALL_CB(pf->iflow, restarth,
			true, pf->iflow.arg);
	}
}

static void pf_norelay_handler(bool local, void *arg)
{
	struct peerflow *pf = (struct peerflow*)arg;

	IFLOW_CALL_CB(pf->iflow, norelayh, local, pf->iflow.arg);
}


static void pf_tool_handler(const char *tool, void *arg)
{
	struct peerflow *pf = (struct peerflow*)arg;
	char *tc = NULL, *v = NULL, *t = NULL;
	int major = 0, minor = 0;
	int err = 0;

	if (!pf || !tool)
		return;

	err = str_dup(&tc, tool);
	if (err) {
		warning("peerflow(%p): tool_handler: failed to copy tool\n", pf);
		return;
	}

	t = tc;
	v = strsep(&t, " ");
	if (v && t && strcmp(v, "sftd") == 0) {
		v = strsep(&t, ".");
		major = v ? atoi(v) : 0;
		if (v && t) {
			v = strsep(&t, ".");
			minor = v ? atoi(v) : 0;
		}
		pf->selective_audio = major >= 2;
		pf->selective_video = major > 2 || (major == 2 && minor >= 1);
		info("peerflow(%p): set_sft_options: ver: %d.%d"
		     " selective_audio: %s selective_video: %s\n",
		     pf,
		     major, minor,
		     pf->selective_audio ? "YES" : "NO",
		     pf->selective_video ? "YES" : "NO");
	}

	mem_deref(tc);
}


int peerflow_handle_offer(struct iflow *iflow,
			  const char *sdp_str)
{
	struct sdp_media *sdpm = NULL;
	struct sdp_media *newm = NULL;
	char cname[16];             /* common for audio+video */
	char msid[36];
	struct mbuf *mb;
	char *grpstr;
	char *gattrstr;
	char *label;	
	uint32_t ssrc;
	struct peerflow *pf = (struct peerflow*)iflow;
	webrtc::SessionDescriptionInterface *isdp;	
	int err = 0;
	
	if (!pf || !sdp_str)
		return EINVAL;

	info("peerflow(%p): handle SDP-offer:\n"
	     "%s\n",
	     pf, sdp_str);
	
	sdp_check(sdp_str, false, true, pf_acbr_handler,
		  pf_norelay_handler, pf_tool_handler, pf);

	webrtc::SdpParseError parse_err;
	std::unique_ptr<webrtc::SessionDescriptionInterface> sdp =
		webrtc::CreateSessionDescription(webrtc::SdpType::kOffer,
						 sdp_str, &parse_err);
	if (sdp == nullptr) {
		warning("peerflow_handle_offer: failed to parse SDP: "
			"line=%s reason=%s\n",
			parse_err.line.c_str(), parse_err.description.c_str());
		err = EPROTO;
	}
	else {
		if (!pf->peerConn) {
			err = create_pf(pf);
			if (err)
				goto out;
		}

		pf->peerConn->SetRemoteDescription(std::move(sdp),
						   pf->sdpRemoteObserver);
						   
						   
		
		const webrtc::SessionDescriptionInterface *rsdp = pf->peerConn->remote_description();
		rsdp->ToString(&pf->remoteSdp);
	}

out:
	return err;
}

int peerflow_handle_answer(struct iflow *iflow,
			   const char *sdp_str)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	char *tool = NULL;
	int err = 0;
	
	if (!pf || !sdp_str)
		return EINVAL;

	info("peerflow(%p): handle SDP-answer:\n"
	     "%s\n",
	     pf, sdp_str);
	
	sdp_check(sdp_str, false, false, pf_acbr_handler,
		  pf_norelay_handler, pf_tool_handler, pf);

	webrtc::SdpParseError parse_err;
	std::unique_ptr<webrtc::SessionDescriptionInterface> sdp =
		webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer,
						 sdp_str, &parse_err);

	if (sdp == nullptr) {
		warning("peerflow_handle_answer: failed to parse SDP: "
			"line=%s reason=%s\n",
			parse_err.line.c_str(), parse_err.description.c_str());
		err = EPROTO;
	}
	else {
		pf->peerConn->SetRemoteDescription(std::move(sdp),
					     pf->sdpRemoteObserver);
		const webrtc::SessionDescriptionInterface *rsdp = pf->peerConn->remote_description();
		rsdp->ToString(&pf->remoteSdp);
	}

	return err;
}


void peerflow_set_call_type(struct iflow *iflow,
			    enum icall_call_type call_type)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	if (!pf)
		return;

	pf->call_type = call_type;
}


bool peerflow_get_audio_cbr(const struct iflow *iflow, bool local)
{
	const struct peerflow *pf = (const struct peerflow*)iflow;
	if (!pf)
		return false;

	return local ? pf->audio.local_cbr : pf->audio.remote_cbr;	
}


void peerflow_set_audio_cbr(struct iflow *iflow, bool enabled)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	debug("peerflow(%p): setting cbr=%d\n", pf, enabled);

	if (!pf)
		return;

	/* If CBR is already set, do not reset mid-call */
	if (!pf->audio.local_cbr)
		pf->audio.local_cbr = enabled;
}


int peerflow_set_remote_userclientid(struct iflow *iflow,
				     const char *userid,
				     const char *clientid)
{
	int err = 0;
	char *label;
	struct conf_member *memb;
	struct peerflow *pf = (struct peerflow*)iflow;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!pf)
		return EINVAL;

	info("peerflow(%p): set remote user %s.%s\n", pf,
		anon_id(userid_anon, userid),
		anon_client(clientid_anon, clientid));

	pf->userid_remote = (char *)mem_deref(pf->userid_remote);
	pf->clientid_remote = (char *)mem_deref(pf->clientid_remote);
	err = str_dup(&pf->userid_remote, userid);
	err |= str_dup(&pf->clientid_remote, clientid);

	if (pf->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		pf->cm = (struct conf_member *)mem_deref(pf->cm);
		conf_member_alloc(&pf->cm, NULL,
				  (struct iflow *)pf,
				  pf->userid_remote,
				  pf->clientid_remote,
				  "_",
				  0,
				  0,
				  "");
	}

	return err;
}


static int peerflow_bundle_update(struct iflow *flow, const char *sdp)
{
	struct peerflow *pf = (struct peerflow *)flow;
	rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface> observer;
	webrtc::SessionDescriptionInterface *isdp;

	isdp = sdp_interface(sdp, webrtc::SdpType::kOffer);

	std::string sdpoffer;
	
	isdp->ToString(&sdpoffer);
	info("peerflow_bundle_update(%p): sdp: %s\n", flow, sdpoffer.c_str());

	observer = new AddDecoderObserver(pf);
	pf->peerConn->SetRemoteDescription(
		std::unique_ptr<webrtc::SessionDescriptionInterface>(isdp),
		observer);

	return 0;
}


int peerflow_add_decoders_for_user(struct iflow *iflow,
				   const char *userid,
				   const char *clientid,
				   const char *userid_hash,
				   uint32_t ssrca,
				   uint32_t ssrcv)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	char *label = NULL;
	struct conf_member *memb;
	int err = 0;

	if (!pf)
		return EINVAL;

	info("pf(%p): add_decoders_for_user: %s.%s ssrca: %u ssrcv: %u\n",
	     pf,
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid),
	     ssrca, ssrcv);
	lock_write_get(pf->cml.lock);
	memb = conf_member_find_by_userclient(&pf->cml.list, userid, clientid);
	/* Only allow the addition if the ssrcs don't match */
	if (memb && memb->ssrca == ssrca && memb->ssrcv == ssrcv)
		return 0;

	if (memb)
		memb->active = false;

	uuid_v4(&label);
	
	err = conf_member_alloc(&memb, &pf->cml.list, (struct iflow *)pf,
				userid, clientid, userid_hash,
				ssrca, ssrcv, label);
	if (err)
		goto out;

 out:
	lock_rel(pf->cml.lock);
	mem_deref(label);

	return err;
}


int peerflow_remove_decoders_for_user(struct iflow *iflow,
				      const char *userid,
				      const char *clientid)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct conf_member *memb;

	if (!pf)
		return EINVAL;

	info("pf(%p): remove_decoders_for_user: %s.%s\n",
	     pf,
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid));

	lock_write_get(pf->cml.lock);
	memb = conf_member_find_by_userclient(&pf->cml.list, userid, clientid);
	if (memb)
		memb->active = false;
	lock_rel(pf->cml.lock);

	return 0;
}

int peerflow_sync_decoders(struct iflow *iflow)
{
	struct peerflow *pf = (struct peerflow *)iflow;
	char *sdp = NULL;
	int err = 0;

	info("pf(%p): sync_decoders\n", pf);

	if (!pf)
		return EINVAL;

	if (!pf->selective_video) {
		str_dup(&sdp, pf->remoteSdp.c_str());
		err = bundle_update((struct iflow *)pf,
				    pf->conv_type,
				    !pf->selective_audio,
				    sdp,
				    &pf->cml.list,
				    peerflow_bundle_update);
		if (err)
			goto out;
	}
 out:
	mem_deref(sdp);

	return err;
}

static int peerflow_generate_offer_answer(struct iflow *iflow,
					  webrtc::SdpType type,
					  char *sdp,
					  size_t sz)
{
	struct sdp_session *sess = NULL;
	char *sdpstr = NULL;
	size_t len;
	int err = 0;
	const webrtc::SessionDescriptionInterface *isdp;

	struct peerflow *pf = (struct peerflow*)iflow;

	if (!pf || !sdp || !sz)
		return EINVAL;

	isdp = pf->peerConn->local_description();
	if (!isdp)
		return ENOENT;

	if (isdp->GetType() != type)
		return EPROTO;

	std::string sdp_str;

	if (isdp->ToString(&sdp_str)) {

		sdp_dup(&sess, pf->conv_type, sdp_str.c_str(), true);
		(void)sdp_session_set_lattr(sess, true, "tool", "%s", avs_version_str());		
		sdpstr = (char *)sdp_sess2str(sess);
		len = sdp_str.length();

		info("peerflow(%p): generate SDP-%s(%zu(%zu) bytes):\n"
		     "%s\n", pf,
		     type == webrtc::SdpType::kOffer ? "offer" : "answer",
		     len,
		     sz,
		     sdpstr);
	
		if (sz < len) {
			err = EIO;
			goto out;
		}

		str_ncpy(sdp, sdpstr, sz);
	}
	else {
		err = EIO;
	}
	
 out:
	mem_deref(sdpstr);
	mem_deref(sess);
	
	return err;	
}

int peerflow_generate_offer(struct iflow *iflow,
			    char *sdp, size_t sz)
{
	return peerflow_generate_offer_answer(iflow,
					      webrtc::SdpType::kOffer,
					      sdp,
					      sz);
}

int peerflow_generate_answer(struct iflow *iflow,
			     char *sdp, size_t sz)
{
	return peerflow_generate_offer_answer(iflow,
					      webrtc::SdpType::kAnswer,
					      sdp,
					      sz);
}

int peerflow_set_video_state(struct iflow *iflow, enum icall_vstate vstate)
{
	bool active;
	struct peerflow *pf = (struct peerflow*)iflow;

	info("pf(%p): set_video_state: %s\n", pf, icall_vstate_name(vstate));
	
	pf->vstate = vstate;
	
	switch (vstate) {
	case ICALL_VIDEO_STATE_STARTED:
	case ICALL_VIDEO_STATE_SCREENSHARE:
		active = true;
		break;

	default:
		active = false;
		break;
	}
	
	if (pf && pf->video.track)
		pf->video.track->set_enabled(active);


	return 0;
}

int peerflow_set_keystore(struct iflow *iflow,
			  struct keystore *keystore)
{
	struct peerflow *pf = (struct peerflow*)iflow;

	info("pf(%p): set_keystore ks:%p\n", pf, keystore);
	mem_deref(pf->keystore);
	pf->keystore = (struct keystore*)mem_ref(keystore);

	return 0;
}

void peerflow_close(struct iflow *iflow)
{
	struct peerflow *pf = (struct peerflow*)iflow;

	debug("pf(%p): close: peerConn=%p tid=%p\n",
	      pf, pf ? pf->peerConn.get() : NULL, pthread_self());
	
	if (!pf)
		return;

	if (pf->peerConn) {
		pf->peerConn->Close();
		debug("pf(%p): close: peerConn=%p closed\n",
		      pf, pf ? pf->peerConn.get() : NULL);
	}

	/* Ensure we empty the mqueue associated with this peerflow,
	 * BEFORE we return. This ensures that all entries in the mqueue,
	 * associated with this peerflow will finish execution
	 */
	run_mq_on_pf(pf, false);

	lock_write_get(g_pf.lock);
	mem_deref(pf);
	lock_rel(g_pf.lock);
}

bool peerflow_has_video(const struct iflow *iflow)
{
	const struct peerflow *pf = (const struct peerflow*)iflow;

	if (!pf || !pf->peerConn) {
		warning("pf(%p): has_video: no peerflow\n");
		return false;
	}

	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> trxs;
	trxs = pf->peerConn->GetTransceivers();
	for(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> trx: trxs) {
		cricket::MediaType mt = trx->media_type();
		debug("pf(%p): has_video: mt=%d\n", pf, mt);
		if (mt == cricket::MEDIA_TYPE_VIDEO) {
			absl::optional<webrtc::RtpTransceiverDirection>	dir;

			dir = trx->current_direction();
			if (dir.has_value()) {
				switch (dir.value()) {
				case webrtc::RtpTransceiverDirection::kSendRecv:
				case webrtc::RtpTransceiverDirection::kSendOnly:
					return true;

				default:
					break;
				}
			}
		}
	}

	return false;
}


void peerflow_stop_media(struct iflow *iflow)
{
	struct peerflow *pf = (struct peerflow*)iflow;
	if (!pf)
		return;

	//peerconnection_audio_set_mute(true);

	IFLOW_CALL_CB(pf->iflow, stoppedh, pf->iflow.arg);
}


int peerflow_get_userid_for_ssrc(struct peerflow* pf,
				 uint32_t csrc,
				 bool video,
				 char **userid_real,
				 char **clientid_real,
				 char **userid_hash)
{
	struct conf_member *cm;
	int err = 0;

	if (!pf)
		return EINVAL;

	lock_write_get(pf->cml.lock);
	if (video)
		cm = conf_member_find_by_ssrcv(&pf->cml.list, csrc);
	else
		cm = conf_member_find_by_ssrca(&pf->cml.list, csrc);

	if (!cm) {
		err = ENOENT;
		goto out;
	}

	if (userid_real)
		err = str_dup(userid_real, cm->userid);
	if (clientid_real)
		err |= str_dup(clientid_real, cm->clientid);
	if (userid_hash)
		err |= str_dup(userid_hash, cm->userid_hash);
	if (err)
		goto out;

out:
	lock_rel(pf->cml.lock);
	return err;
}

int peerflow_inc_frame_count(struct peerflow* pf,
			     uint32_t csrc,
			     bool video,
			     uint32_t frames)
{
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct conf_member *cm;
	int err = 0;

	if (!pf)
		return EINVAL;

	lock_write_get(pf->cml.lock);
	if (video)
		cm = conf_member_find_by_ssrcv(&pf->cml.list, csrc);
	else
		cm = conf_member_find_by_ssrca(&pf->cml.list, csrc);

	if (!cm) {
		err = ENOENT;
		goto out;
	}

	if (video)
		cm->video_frames += frames;
	else
		cm->audio_frames += frames;

	debug("FRAME: %s.%s a: %u v: %u\n",
	      anon_id(userid_anon, cm->userid),
	      anon_client(clientid_anon, cm->clientid),
	      cm->audio_frames,
	      cm->video_frames);
out:
	lock_rel(pf->cml.lock);
	return err;
}


void peerflow_set_stats(struct peerflow* pf,
			int audio_level,
			uint32_t apkts_recv,
			uint32_t vpkts_recv,
			uint32_t apkts_sent,
			uint32_t vpkts_sent,
			float downloss,
			float rtt)
{
	if (!pf) {
		return;
	}

	pf->stats.audio_level = g_pf.audio.muted ? 0 : audio_level;
	if (g_pf.audio.muted) {
		if (pf->stats.audio_level_smooth > 0)
			pf->stats.audio_level_smooth--;
		else
			pf->stats.audio_level_smooth = 0;
	}
	else if (audio_level > AUDIO_LEVEL_FLOOR)
		pf->stats.audio_level_smooth = AUDIO_LEVEL_CEIL;
	else if (pf->stats.audio_level_smooth > 0)
		pf->stats.audio_level_smooth--;
	
	pf->stats.apkts_recv = apkts_recv;
	pf->stats.vpkts_recv = vpkts_recv;
	pf->stats.apkts_sent = apkts_sent;
	pf->stats.vpkts_sent = vpkts_sent;
	pf->stats.dloss = downloss;
	pf->stats.rtt = rtt;
}

int peerflow_get_stats(struct iflow *flow,
		       struct iflow_stats *stats)
{
	struct peerflow *pf = (struct peerflow*)flow;
	if (!pf || !stats) {
		return EINVAL;
	}

	*stats = pf->stats;
	return 0;
}

int peerflow_debug(struct re_printf *pf, const struct iflow *flow)
{
	struct peerflow *peerflow = (struct peerflow*)flow;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	struct le *le;
	int err = 0;

	err = re_hprintf(pf, "\nPEERFLOW SUMMARY %p:\n", peerflow);
	if (err)
		goto out;

	LIST_FOREACH(&peerflow->cml.list, le) {
		struct conf_member *cm = (struct conf_member *)le->data;

		if (cm->active) {
			err = re_hprintf(pf, "stream user: %s.%s ssrca: %u ssrcv: %u aframes: %u vframes: %u\n",
				anon_id(userid_anon, cm->userid),
				anon_client(clientid_anon, cm->clientid),
				cm->ssrca, cm->ssrcv,
				cm->audio_frames, cm->video_frames);
			if (err)
				goto out;
		}
	}

out:
	return err;
}


