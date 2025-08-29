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

#include "re.h"
#include "avs.h"
#include "avs_peerflow.h"
#include "avs_version.h"
#include "avs_audio_level.h"

#include "jsflow.h"

#include <emscripten.h>
#include <sodium.h>

#define MODIFY_SDP 1

#define DOUBLE_ENCRYPTION 1

#define SDP_TYPE_OFFER  "offer"
#define SDP_TYPE_ANSWER "answer"

#define GATHER_TIMEOUT     10000
#define DISCONNECT_TIMEOUT 10000
#define TMR_STATS_INTERVAL  1000
#define TMR_CBR_INTERVAL    2500

#define PC_INVALID_HANDLE 0

#define DCE_LABEL "calling-3.0"

#define PC_SIG_STATE_UNKNOWN         0
#define PC_SIG_STATE_STABLE          1
#define PC_SIG_STATE_LOCAL_OFFER     2
#define PC_SIG_STATE_LOCAL_PRANSWER  3
#define PC_SIG_STATE_REMOTE_OFFER    4
#define PC_SIG_STATE_REMOTE_PRANSWER 5
#define PC_SIG_STATE_CLOSED          6

#define PC_GATHER_STATE_UNKNOWN   0
#define PC_GATHER_STATE_NEW       1
#define PC_GATHER_STATE_GATHERING 2
#define PC_GATHER_STATE_COMPLETE  3

#define DC_STATE_CONNECTING 0
#define DC_STATE_OPEN       1
#define DC_STATE_CLOSING    2
#define DC_STATE_CLOSED     3
#define DC_STATE_ERROR      4

static const size_t IV_SIZE    = 12;

/* JS functions */

typedef void (pc_SetEnv_t)(int env);
typedef int  (pc_New_t)(int self,
			const char *convid,
			const uint8_t *audio_iv,
			const uint8_t *video_iv,
			int iv_len);
typedef void (pc_Create_t)(int handle, int privacy, int conv_type);
typedef void (pc_Close_t)(int handle);
typedef void (pc_HeapFree_t)(const void *ptr);
typedef void (pc_AddTurnServer_t)(int handle,
				  const char *url,
				  const char *username,
				  const char *password);
typedef int  (pc_IceGatheringState_t)(int handle);
typedef int  (pc_SignallingState_t)(int handle);
typedef int  (pc_ConnectionState_t)(int handle);
typedef int  (pc_CreateDataChannel_t)(int handle, const char *label);
typedef void (pc_CreateOffer_t)(int handle, int call_type, int vstate);
typedef void (pc_CreateAnswer_t)(int handle, int call_type, int vstate);
typedef void (pc_AddDecoderAnswer_t)(int handle);
typedef void (pc_AddUserInfo_t)(int handle, const char *label,
				const char *userid, const char *clientid,
				const char *ssrca, const char *ssrcv,
				const uint8_t *audio_iv,
				const uint8_t *video_iv,
				int iv_len);
typedef void (pc_RemoveUserInfo_t)(int handle, const char *label);

typedef void (pc_SetRemoteDescription_t)(int handle,
					 const char *type,
					 const char *sdp);
typedef void (pc_SetLocalDescription_t)(int handle,
					const char *type,
					const char *sdp);
typedef char *(pc_LocalDescription_t)(int handle, const char *type);

typedef void (pc_SetMute_t)(int handle, int muted);
typedef int  (pc_GetMute_t)(int handle);
typedef void (pc_GetLocalStats_t)(int handle);
typedef void (pc_SetRemoteUserClientId_t)(int handle,
					  const char *userid,
					  const char *clientid);
typedef int (pc_HasVideo_t) (int handle);
typedef void (pc_SetVideoState_t) (int handle, int vstate);

/* DataChannel */
typedef int  (pc_DataChannelId_t)(int handle);
typedef int  (pc_DataChannelState_t)(int handle);
typedef void (pc_DataChannelSend_t)(int handle, const int8_t *data, int len);
typedef void (pc_DataChannelClose_t)(int handle);

/* Frame enc/dec */
typedef void (pc_SetMediaKey_t)(int handle, int index, int current, const uint8_t *key, int len);

typedef void  (pc_UpdateSsrc_t)(int handle, const char *ssrca, const char *ssrcv);

static pc_SetEnv_t *pc_SetEnv = NULL;
static pc_New_t *pc_New = NULL;
static pc_Create_t *pc_Create = NULL;
static pc_Close_t *pc_Close = NULL;
static pc_HeapFree_t *pc_HeapFree = NULL;
static pc_AddTurnServer_t *pc_AddTurnServer = NULL;
static pc_IceGatheringState_t *pc_IceGatheringState = NULL;
static pc_SignallingState_t *pc_SignallingState = NULL;
static pc_ConnectionState_t *pc_ConnectionState = NULL;
static pc_CreateDataChannel_t *pc_CreateDataChannel = NULL;
static pc_CreateOffer_t *pc_CreateOffer = NULL;
static pc_CreateAnswer_t *pc_CreateAnswer = NULL;
static pc_AddDecoderAnswer_t *pc_AddDecoderAnswer = NULL;
static pc_AddUserInfo_t *pc_AddUserInfo = NULL;
static pc_RemoveUserInfo_t *pc_RemoveUserInfo = NULL;
static pc_SetRemoteDescription_t *pc_SetRemoteDescription = NULL;
static pc_SetLocalDescription_t *pc_SetLocalDescription = NULL;
static pc_LocalDescription_t *pc_LocalDescription = NULL;
static pc_SetMute_t *pc_SetMute = NULL;
static pc_GetMute_t *pc_GetMute = NULL;
static pc_GetLocalStats_t *pc_GetLocalStats = NULL;
static pc_SetRemoteUserClientId_t *pc_SetRemoteUserClientId = NULL;
static pc_HasVideo_t *pc_HasVideo = NULL;
static pc_SetVideoState_t *pc_SetVideoState = NULL;

/* DataChannel */
static pc_DataChannelId_t *pc_DataChannelId = NULL;
static pc_DataChannelState_t *pc_DataChannelState = NULL;
static pc_DataChannelSend_t *pc_DataChannelSend = NULL;
static pc_DataChannelClose_t *pc_DataChannelClose = NULL;

/* Frame enc/dec */
static pc_SetMediaKey_t *pc_SetMediaKey = NULL;

static pc_UpdateSsrc_t *pc_UpdateSsrc = NULL;

#define ENV_NONE -1
#define ENV_DEFAULT 0
#define ENV_FIREFOX 1

static struct {
	int env;
	bool initialized;
	bool muted;
	int self_index;
	struct list pcl; /* List of used jsflows */
} g_jf = {
	.env = ENV_NONE,
	.initialized = false,
	.muted = false,
	.self_index = 0,
	.pcl = LIST_INIT,
};

#define PC_MAGIC 0x50430000U

struct jsflow {
	struct iflow iflow;
	uint32_t self;
	int handle;
	bool closed;

	char *convid;
	char *userid_self;
	char *clientid_self;

	struct conf_member *cm;

	enum icall_call_type call_type;
	enum icall_conv_type conv_type;
	enum icall_vstate vstate;

	struct {
		bool req_local_cbr;
		bool local_cbr;
		bool remote_cbr;
	} audio;

	struct {
		int handle;

		int id;
	} dc;

	struct {
		bool negotiated;
		uint32_t ssrc;
	} video;

	struct iflow_stats stats;

	struct list cml; /* conf members */
	char *remote_userid;
	char *remote_clientid;

	struct zapi_ice_server turnv[16];
	int turnc;

	struct {
		struct keystore *keystore;
	} frame;

	bool gather;
	bool pending_gather;
	uint32_t bundle_update;
	char *bundle_sync;
	struct tmr tmr_gather;
	struct tmr tmr_disconnect;
	struct tmr tmr_stats;
	char *remote_sdp;	
	bool selective_audio;
	bool selective_video;

	struct tmr tmr_cbr;
	
	struct le le;
};


void jsflow_stop_media(struct iflow *iflow)
{
}


static void send_close(struct jsflow *flow, int err)
{
	if (flow->closed)
		return;
	
	IFLOW_CALL_CB(flow->iflow, closeh,
		err, flow->iflow.arg);
}

static void gather_timeout_handler(void *arg)
{
	struct jsflow *flow = arg;

	flow->gather = false;
	flow->pending_gather = false;
	IFLOW_CALL_CB(flow->iflow, gatherh,
		flow->iflow.arg);
}

static void disconnect_timeout_handler(void *arg)
{
	struct jsflow *flow = arg;

	info("jsflow(%p): disconnect_timeout_handler\n", flow);

	IFLOW_CALL_CB(flow->iflow, restarth,
		false, flow->iflow.arg);
}

static void stats_timeout_handler(void *arg)
{
	struct jsflow *flow = arg;

	pc_GetLocalStats(flow->handle);

	tmr_start(&flow->tmr_stats, TMR_STATS_INTERVAL, stats_timeout_handler, flow);
}


int jsflow_dce_send(struct iflow *flow,
		      const uint8_t *data,
		      size_t len)
{
	struct jsflow *jsflow = (struct jsflow *)flow;
       
	if (!jsflow)
		return EINVAL;

	if (jsflow->dc.handle == PC_INVALID_HANDLE) {
		warning("jsflow(%p): dce_send: no valid data channel\n");
		return EINVAL;
	}
	
	pc_DataChannelSend(jsflow->dc.handle, (const int8_t *)data, len);

	return 0;
}

static void keystore_cchanged_handler(struct keystore *keystore,
				      void *arg)
{
#if DOUBLE_ENCRYPTION
	struct jsflow *flow = (struct jsflow *)arg;
	uint8_t media_key[E2EE_SESSIONKEY_SIZE];
	uint32_t current;
	uint64_t ts;

	int err = 0;

	if (!flow)
		return;

	if (flow->handle == PC_INVALID_HANDLE) {
		warning("flow(%p): keystore_cchanged_handler: no flow\n", flow);
		return;
	}

	err = keystore_get_current(flow->frame.keystore, &current, &ts);
	if (err)
		goto out;

	info("jsflow(%p): keystore_cchanged_handler curr: %u\n", flow, current);
	err = keystore_get_media_key(flow->frame.keystore, current,
				     media_key, E2EE_SESSIONKEY_SIZE);
	if (err)
		goto out;

	pc_SetMediaKey(flow->handle,
		       current,
		       current,
		       media_key,
		       E2EE_SESSIONKEY_SIZE);

out:
	sodium_memzero(media_key, E2EE_SESSIONKEY_SIZE);
#endif
}

static int jsflow_set_keystore(struct iflow *iflow,
			       struct keystore *keystore)
{
	struct jsflow *jf = (struct jsflow *)iflow;

	info("jsflow(%p): set_keystore ks:%p\n", jf, keystore);
	jf->frame.keystore = mem_deref(jf->frame.keystore);
	jf->frame.keystore = (struct keystore *)mem_ref(keystore);

	keystore_add_listener(keystore,
			      keystore_cchanged_handler,
			      jf);
	return 0;
}


#if 0
void jsflow_start_log(void)
{
}
#endif

static void set_mute(struct jsflow *flow, bool muted)
{
	pc_SetMute(flow->handle, (int)muted);
}

static void jsflow_set_mute(bool muted)
{
	struct le *le;

	info("jsflow: set_mute: %d->%d\n", g_jf.muted, muted);
	
	if (g_jf.muted == muted)
		return;
	
	g_jf.muted = muted;
	LIST_FOREACH(&g_jf.pcl, le) {
		struct jsflow *flow = le->data;

		set_mute(flow, muted);
	}
}
 
static bool jsflow_get_mute(void)
{
	return g_jf.muted;
}

void jsflow_destroy(void)
{
	if (!g_jf.initialized)
		return;
	
	g_jf.initialized = false;
}

static int jsflow_init(void)
{
	iflow_register_statics(jsflow_destroy,
			       jsflow_set_mute,
			       jsflow_get_mute);
	g_jf.initialized = true;
	
	return 0;
}

static void destructor(void *arg)
{
	struct jsflow *flow = arg;

	keystore_remove_listener(flow->frame.keystore, flow);
	tmr_cancel(&flow->tmr_gather);
	tmr_cancel(&flow->tmr_disconnect);
	tmr_cancel(&flow->tmr_stats);
	tmr_cancel(&flow->tmr_cbr);

	list_unlink(&flow->le);

	list_flush(&flow->cml);

	mem_deref(flow->remote_sdp);
	mem_deref(flow->bundle_sync);

	mem_deref(flow->convid);
	mem_deref(flow->userid_self);
	mem_deref(flow->clientid_self);

	mem_deref(flow->cm);

	// Close?
}

static void timer_cbr(void *arg)
{
	struct jsflow *jf = arg;
	bool cbr_detected;

	if (!jf)
		return;
	
	cbr_detected = jf->audio.local_cbr && jf->audio.remote_cbr;
	IFLOW_CALL_CB(jf->iflow, acbr_detecth,
		      cbr_detected ? 1 : 0, jf->iflow.arg);
	
	tmr_start(&jf->tmr_cbr, TMR_CBR_INTERVAL, timer_cbr, jf);
}


static int create_pc(struct jsflow *flow)
{
	int err = 0;
	int i;
	bool privacy = 0;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];
	uint8_t audio_iv[IV_SIZE];
	uint8_t video_iv[IV_SIZE];

	if (g_jf.env == ENV_NONE) {
		g_jf.env = msystem_get_env();
		pc_SetEnv(g_jf.env);
	}
	
	if (flow->handle != PC_INVALID_HANDLE)
		return EALREADY;
 
	if (flow->frame.keystore) {
		err = keystore_generate_iv(flow->frame.keystore,
					   flow->userid_self,
					   "audio_iv",
					   audio_iv,
					   IV_SIZE);
		if (err)
			goto out;

		err = keystore_generate_iv(flow->frame.keystore,
					   flow->userid_self,
					   "video_iv",
					   video_iv,
					   IV_SIZE);
		if (err)
			goto out;
	}
	else {
		memset(audio_iv, 0, IV_SIZE);
		memset(video_iv, 0, IV_SIZE);
	}

	flow->handle = pc_New(flow->self,
			      flow->convid,
			      audio_iv,
			      video_iv,
			      IV_SIZE);
	if (flow->handle == PC_INVALID_HANDLE) {
		warning("pc: could not create jsflow\n");
		err = ENOENT;
		goto out;
	}

	info("create_pc(%p): new pc created. hnd=%d userid=%s clientid=%s\n",
	     flow, flow->handle,
	     anon_id(userid_anon, flow->remote_userid),
	     anon_client(clientid_anon, flow->remote_clientid));

	pc_SetMute(flow->handle, g_jf.muted);
	pc_SetRemoteUserClientId(flow->handle,
				 flow->remote_userid, flow->remote_clientid);
	
	for (i = 0; i < flow->turnc; ++i) {
		struct zapi_ice_server *turn = &flow->turnv[i];

		pc_AddTurnServer(flow->handle,
				 turn->url, turn->username, turn->credential);
	}

	privacy = msystem_get_privacy(msystem_instance());
	pc_Create(flow->handle, privacy ? 1 : 0, flow->conv_type);

	tmr_start(&flow->tmr_stats, TMR_STATS_INTERVAL,
		  stats_timeout_handler, flow);

	if (flow->conv_type == ICALL_CONV_TYPE_ONEONONE)
		tmr_start(&flow->tmr_cbr, TMR_CBR_INTERVAL, timer_cbr, flow);
	
 out:
	sodium_memzero(audio_iv, IV_SIZE);
	sodium_memzero(video_iv, IV_SIZE);
	return err;
}


static uint32_t pc2self(struct jsflow *flow)
{
	uint32_t self;

	self = PC_MAGIC + g_jf.self_index;
	g_jf.self_index++;
	g_jf.self_index &= 0xFFFF; /* wrap */

	flow->self = self;

	return self;
}

static struct jsflow *self2pc(int self)
{
	struct jsflow *flow;	
	struct le *le;
	bool found = false;

	if (((uint32_t)self & PC_MAGIC) != PC_MAGIC) {
		warning("self2pc: bad magic\n");
		return NULL;
	}
	
	for(le = g_jf.pcl.head; !found && le; le = le->next) {
		flow = le->data;

		found = flow->self == (uint32_t)self;
	}

	return found ? flow : NULL;
}

int jsflow_alloc(struct iflow		**flowp,
		 const char		*convid,
		 const char		*userid_self,
		 const char             *clientid_self,
		 enum icall_conv_type	conv_type,
		 enum icall_call_type	call_type,
		 enum icall_vstate	vstate,
		 void			*extarg)
{
	struct jsflow *flow;

	info("jsflow_alloc: initialized=%d call_type=%d vstate=%s\n",
	     g_jf.initialized, call_type, icall_vstate_name(vstate));
	if (!g_jf.initialized) {
		jsflow_init();
		if (!g_jf.initialized)
			return ENOSYS;
	}
	
	if (!flowp)
		return EINVAL;
	
	flow = mem_zalloc(sizeof(*flow), destructor);
	if (!flow)
		return ENOMEM;

	iflow_set_functions(&flow->iflow,
			    jsflow_set_video_state,
			    jsflow_generate_offer,
			    jsflow_generate_answer,
			    jsflow_handle_offer,
			    jsflow_handle_answer,
			    jsflow_has_video,
			    jsflow_is_gathered,
			    NULL, // peerflow_enable_privacy
			    jsflow_set_call_type,
			    jsflow_get_audio_cbr,
			    jsflow_set_audio_cbr,
			    jsflow_set_remote_userclientid,
			    jsflow_add_turnserver,
			    jsflow_gather_all_turn,
			    jsflow_add_decoders_for_user,
			    jsflow_remove_decoders_for_user,
			    jsflow_sync_decoders,
			    jsflow_set_keystore,
			    jsflow_dce_send,
			    jsflow_stop_media,
			    jsflow_close,
			    jsflow_get_stats,
			    jsflow_get_aulevel,
			    jsflow_update_ssrc,
			    NULL); //jsflow_debug);
	pc2self(flow);

	str_dup(&flow->convid, convid);
	str_dup(&flow->userid_self, userid_self);
	str_dup(&flow->clientid_self, clientid_self);

	flow->vstate = vstate;
	flow->handle = PC_INVALID_HANDLE;
	flow->conv_type = conv_type;
	flow->call_type = call_type;

	list_append(&g_jf.pcl, &flow->le, flow);

	tmr_init(&flow->tmr_gather);
	tmr_init(&flow->tmr_disconnect);
	tmr_init(&flow->tmr_stats);
	tmr_init(&flow->tmr_cbr);

	*flowp = (struct iflow *)flow;

	return 0;
}


void jsflow_close(struct iflow *flow)
{
	struct jsflow *jsflow = (struct jsflow*)flow;
	if (!jsflow)
		return;

	jsflow->closed = true;
	tmr_cancel(&jsflow->tmr_disconnect);
	tmr_cancel(&jsflow->tmr_stats);
	tmr_cancel(&jsflow->tmr_cbr);
	tmr_cancel(&jsflow->tmr_gather);	

	if (jsflow->dc.handle != PC_INVALID_HANDLE)
		pc_DataChannelClose(jsflow->dc.handle);
	
	pc_Close(jsflow->handle);

	jsflow->dc.handle = PC_INVALID_HANDLE;
	jsflow->handle = PC_INVALID_HANDLE;

	//list_unlink(&jsflow->le);

	mem_deref(jsflow);
}


int jsflow_add_turnserver(struct iflow *flow,
			  const char *url,
			  const char *username,
			  const char *password)
{
	struct jsflow *jsflow = (struct jsflow*)flow;
	struct zapi_ice_server *turn;

	if (!jsflow || !url)
		return EINVAL;

	turn = &jsflow->turnv[jsflow->turnc];
	++jsflow->turnc;
		
	str_ncpy(turn->url, url, sizeof(turn->url));
	str_ncpy(turn->username, username, sizeof(turn->username));
	str_ncpy(turn->credential, password, sizeof(turn->credential));
	
	return 0;
}

bool jsflow_is_gathered(const struct iflow *flow)
{
	const struct jsflow *jsflow = (const struct jsflow*)flow;
	int state;

	state = pc_IceGatheringState(jsflow->handle);

	return state == PC_GATHER_STATE_COMPLETE;
}

void jsflow_set_call_type(struct iflow *iflow,
			  enum icall_call_type call_type)
{
	struct jsflow *jsflow = (struct jsflow*)iflow;
	if (!jsflow)
		return;

	jsflow->call_type = call_type;
}


bool jsflow_get_audio_cbr(const struct iflow *iflow, bool local)
{
	const struct jsflow *jsflow = (const struct jsflow*)iflow;
	if (!jsflow)
		return false;

	return local ? jsflow->audio.local_cbr : jsflow->audio.remote_cbr;	
}


bool jsflow_has_video(const struct iflow *iflow)
{
	return false;
}


void jsflow_set_audio_cbr(struct iflow *iflow, bool enabled)
{
	struct jsflow *jsflow = (struct jsflow*)iflow;
	debug("jsflow(%p): setting cbr=%d\n", jsflow, enabled);

	if (!jsflow)
		return;

	/* If CBR is already set, do not reset mid-call */
	if (!jsflow->audio.req_local_cbr)
		jsflow->audio.req_local_cbr = enabled;
}


int  jsflow_gather_all_turn(struct iflow *flow, bool offer)
{
	struct jsflow *jsflow = (struct jsflow*)flow;
	int state;
	
	if (!jsflow)
		return EINVAL;

	if (jsflow->handle == PC_INVALID_HANDLE)
		create_pc(jsflow);

	
	state = pc_SignallingState(jsflow->handle);

	info("jsflow(%p): gather: offer=%d state=%d\n",
	     jsflow, offer, state);

	jsflow->video.negotiated = jsflow->call_type == ICALL_CALL_TYPE_VIDEO;	
	jsflow->pending_gather = true;

	if (offer) {
		jsflow->dc.handle = pc_CreateDataChannel(jsflow->handle, DCE_LABEL);
		pc_CreateOffer(jsflow->handle, jsflow->call_type, jsflow->vstate);
	}
	else {
		jsflow->gather = true;
		if (PC_SIG_STATE_REMOTE_OFFER == state) {
			pc_CreateAnswer(jsflow->handle, jsflow->call_type, jsflow->vstate);
		}
	}

	return 0;
}

static void jf_norelay_handler(bool local, void *arg)
{
	struct jsflow *jf = (struct jsflow *)arg;

	IFLOW_CALL_CB(jf->iflow, norelayh, local, jf->iflow.arg);
}


static void remote_acbr_handler(bool enabled, bool offer, void *arg)
{
	struct jsflow *jsflow = (struct jsflow*)arg;
	bool local_cbr;
	
	local_cbr = jsflow->audio.local_cbr;
	jsflow->audio.remote_cbr = enabled;
	jsflow->audio.req_local_cbr = enabled;// && g_jf.env != ENV_FIREFOX;

	info("jsflow(%p): remote_acbr: enabled=%d local CBR=%d\n",
	     jsflow, enabled, local_cbr);
	
	if (offer) {
		/* If this is an offer, then we just need to set
		 * the local cbr
		 */
	}		
	else if (enabled && !local_cbr && (g_jf.env != ENV_FIREFOX)) {
		IFLOW_CALL_CB(jsflow->iflow, restarth,
			true, jsflow->iflow.arg);
		return;
	}

	if (enabled && local_cbr) {
		IFLOW_CALL_CB(jsflow->iflow, acbr_detecth,
			      1, jsflow->iflow.arg);
	}
}


static void jsflow_tool_handler(const char *tool, void *arg)
{
	struct jsflow *jsflow = (struct jsflow*)arg;
	char *tc = NULL, *v = NULL, *t = NULL;
	int major = 0, minor = 0;
	int err = 0;

	if (!jsflow || !tool)
		return;

	err = str_dup(&tc, tool);
	if (err) {
		warning("jsflow(%p): tool_handler: failed to copy tool\n", jsflow);
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
		jsflow->selective_audio = major >= 2;
		jsflow->selective_video = major > 2 || (major == 2 && minor >= 1);
		info("jsflow(%p): set_sft_options: ver: %d.%d"
		     " selective_audio: %s selective_video: %s\n",
		     jsflow,
		     major, minor,
		     jsflow->selective_audio ? "YES" : "NO",
		     jsflow->selective_video ? "YES" : "NO");
	}

	mem_deref(tc);
}


int jsflow_handle_offer(struct iflow *flow,
			const char *sdp_str)
{
	struct jsflow *jsflow = (struct jsflow*)flow;

	if (!jsflow)
		return EINVAL;

	info("jsflow(%p): handle_offer: SDP-offer %s\n",
	     jsflow, sdp_str);
	
	if (jsflow->handle == PC_INVALID_HANDLE)
		create_pc(jsflow);


	info("jsflow(%p): handle SDP-offer:\n"
	     "%s\n",
	     jsflow, sdp_str);
	
	sdp_check(sdp_str, false, true,
		  remote_acbr_handler, jf_norelay_handler,
		  jsflow_tool_handler, jsflow);

	jsflow->remote_sdp = mem_deref(jsflow->remote_sdp);
	str_dup(&jsflow->remote_sdp, sdp_str);
	pc_SetRemoteDescription(jsflow->handle, SDP_TYPE_OFFER, sdp_str);

	return 0;
}


int jsflow_handle_answer(struct iflow *flow,
			 const char *sdp_str)
{
	struct jsflow *jsflow = (struct jsflow*)flow;
	bool has_video;
	
	if (!flow)
		return EINVAL;

	sdp_check(sdp_str, false, false,
		  remote_acbr_handler, jf_norelay_handler,
		  jsflow_tool_handler, jsflow);
	
	has_video = pc_HasVideo(jsflow->handle);

	jsflow->remote_sdp = mem_deref(jsflow->remote_sdp);
	if (has_video)
		str_dup(&jsflow->remote_sdp, sdp_str);
	else
		sdp_strip_video(&jsflow->remote_sdp, jsflow->conv_type, sdp_str);

	info("jsflow(%p): handle_answer: has_video=%d SDP-answer %s\n",
	     jsflow, has_video, jsflow->remote_sdp);
	
	pc_SetRemoteDescription(jsflow->handle, SDP_TYPE_ANSWER, jsflow->remote_sdp);

	return 0;
}

static void local_acbr_handler(bool enabled, bool offer, void *arg)
{
	struct jsflow *flow = arg;
	bool is_cbr;

	flow->audio.local_cbr = enabled;

	info("local_acbr_handler: flow(%p): offer=%d local CBR=%d "
	     "remote CBR=%d\n",
	     flow, offer, enabled, flow->audio.remote_cbr);
	
	is_cbr = flow->audio.local_cbr && flow->audio.remote_cbr;
	
	IFLOW_CALL_CB(flow->iflow, acbr_detecth,
		      is_cbr ? 1 : 0, flow->iflow.arg);
}

static int jsflow_generate_offer_answer(struct iflow *flow,
					const char *type,
					char *sdp,
					size_t sz)
{
	struct jsflow *jsflow = (struct jsflow*)flow;
	struct sdp_session *sess;
	char *sdpstr = NULL;
	char *modsdp = NULL;
	size_t len;
	int err = 0;
	bool isoffer;

	if (!jsflow || !sdp || !sz)
		return EINVAL;

	/* string returned by pc_LocalDescription will be
	 * allocated on the emscipten heap,
	 * we need to free it after we've used it
	 */
	sdpstr = pc_LocalDescription(jsflow->handle, type);
	if (!sdpstr)
		return ENOENT;

	isoffer = streq(type, "offer");
	sdp_check(sdpstr, true, isoffer, local_acbr_handler, NULL, NULL, jsflow);
	
	sdp_dup(&sess, jsflow->conv_type, sdpstr, true);

	/* We have converted the string to a session now, 
	 * so we are good to free it
	 */
	pc_HeapFree(sdpstr);

	if (isoffer) {
		sdp_modify_offer(sess,
				 jsflow->conv_type,
				 jsflow->vstate == ICALL_VIDEO_STATE_SCREENSHARE,
				 jsflow->audio.req_local_cbr);
	}
	else {
		sdp_modify_answer(sess,
				  jsflow->conv_type,
				  jsflow->vstate == ICALL_VIDEO_STATE_SCREENSHARE,
				  jsflow->audio.req_local_cbr);
	}
	
	(void)sdp_session_set_lattr(sess, true, "tool", "wasm-%s: %s",
			(g_jf.env == ENV_FIREFOX) ? "firefox" : "default",
			 avs_version_str());	

	modsdp = (char *)sdp_sess2str(sess);
	len = str_len(modsdp);

	info("jsflow(%p): generate SDP-%s(%zu(%zu) bytes):\n"
	     "%s\n", jsflow,
	     type,
	     len,
	     sz,
	     modsdp);

	if (sz < len) {
		err = EIO;
		goto out;
	}

	str_ncpy(sdp, modsdp, sz);

out:
	mem_deref(sess);
	mem_deref(modsdp);

	return err;
}

int jsflow_generate_offer(struct iflow *iflow,
			  char *sdp,
			  size_t sz)
{
	return jsflow_generate_offer_answer(iflow,
					    SDP_TYPE_OFFER,
					    sdp,
					    sz);
}

int jsflow_generate_answer(struct iflow *iflow,
			   char *sdp,
			   size_t sz)
{
	return jsflow_generate_offer_answer(iflow,
					    SDP_TYPE_ANSWER,
					    sdp,
					    sz);
}
static int jsflow_bundle_update(struct iflow *flow, const char *sdp)
{
	struct jsflow *jsflow = (struct jsflow *)flow;
	int state;

	state = pc_SignallingState(jsflow->handle);
	if (state == PC_SIG_STATE_STABLE) {
		jsflow->bundle_update++;
		pc_SetRemoteDescription(jsflow->handle, SDP_TYPE_OFFER, sdp);
	}
	else {
		/* Save off the SDP so we can sync it when we transition to
		 * stable signalling state.
		 */
		info("jsflow(%p): bundle_update in non-stable state sync=%p\n",
		     jsflow, jsflow->bundle_sync);
		jsflow->bundle_sync = mem_deref(jsflow->bundle_sync);
		str_dup(&jsflow->bundle_sync, sdp);
	}

	return 0;
}

int jsflow_add_decoders_for_user(struct iflow *iflow,
				 const char *userid,
				 const char *clientid,
				 const char *userid_hash,
				 uint32_t ssrca,
				 uint32_t ssrcv)
{
	struct jsflow *jf = (struct jsflow*)iflow;
	char *label = NULL;
	struct conf_member *memb;
	int err = 0;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	char ssrca_str[16];
	char ssrcv_str[16];
	uint8_t audio_iv[IV_SIZE];
	uint8_t video_iv[IV_SIZE];

	if (!jf || !userid || !clientid || !userid_hash)
		return EINVAL;

	info("jf(%p): add_decoders_for_user: %s.%s ssrca: %u ssrcv: %u\n",
	     jf,
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid),
	     ssrca, ssrcv);

	memb = conf_member_find_by_userclient(&jf->cml, userid, clientid);
	/* Only allow the addition if the ssrcs don't match */
	if (memb && memb->ssrca == ssrca && memb->ssrcv == ssrcv)
		return 0;

	if (memb) {
		memb->active = false;
	}

	uuid_v4(&label);

	err = conf_member_alloc(&memb, &jf->cml, (struct iflow *)jf,
				userid, clientid, userid_hash,
				ssrca, ssrcv, label);
	if (err)
		goto out;

	if (jf->frame.keystore) {
		err = keystore_generate_iv(jf->frame.keystore,
					   userid_hash,
					   "audio_iv",
					   audio_iv,
					   IV_SIZE);
		if (err)
			goto out;

		err = keystore_generate_iv(jf->frame.keystore,
					   userid_hash,
					   "video_iv",
					   video_iv,
					   IV_SIZE);
		if (err)
			goto out;
	}
	else {
		memset(audio_iv, 0, IV_SIZE);
		memset(video_iv, 0, IV_SIZE);
	}

	re_snprintf(ssrca_str, sizeof(ssrca_str), "%u", ssrca);
	re_snprintf(ssrcv_str, sizeof(ssrcv_str), "%u", ssrcv);

	pc_AddUserInfo(jf->handle, label, userid, clientid,
		       ssrca_str, ssrcv_str,
		       audio_iv, video_iv, IV_SIZE);

out:
	sodium_memzero(audio_iv, IV_SIZE);
	sodium_memzero(video_iv, IV_SIZE);
	mem_deref(label);

	return err;
}

int jsflow_remove_decoders_for_user(struct iflow *iflow,
				    const char *userid,
				    const char *clientid)
{
	struct jsflow *jf = (struct jsflow *)iflow;
	struct conf_member *memb;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!jf)
		return EINVAL;

	info("jsflow(%p): remove_decoders_for_user: %s.%s\n",
	     jf,
	     anon_id(userid_anon, userid),
	     anon_client(clientid_anon, clientid));

	memb = conf_member_find_active_by_userclient(&jf->cml, userid, clientid);
	if (memb) {
		memb->active = false;

		pc_RemoveUserInfo(jf->handle, memb->label);
	}
	return 0;
}

int jsflow_sync_decoders(struct iflow *iflow)
{
	struct jsflow *jf = (struct jsflow *)iflow;
	int err = 0;

	info("jsflow(%p): sync_decoders\n", jf);
	
	if (!jf)
		return EINVAL;
	
	if (!jf->selective_video) {
		err = bundle_update((struct iflow *)jf,
				    jf->conv_type,
				    !jf->selective_audio,
				    jf->remote_sdp,
				    &jf->cml,
				    jsflow_bundle_update);
		if (err)
			goto out;
	}

 out:
	return err;
}

int jsflow_set_remote_userclientid(struct iflow *iflow,
				   const char *userid,
				   const char *clientid)
{
	struct jsflow *jsflow = (struct jsflow*)iflow;
	int err = 0;
	char userid_anon[ANON_ID_LEN];
	char clientid_anon[ANON_CLIENT_LEN];

	if (!jsflow)
		return EINVAL;

	jsflow->remote_userid = (char *)mem_deref(jsflow->remote_userid);
	jsflow->remote_clientid = (char *)mem_deref(jsflow->remote_clientid);
	err = str_dup(&jsflow->remote_userid, userid);
	err |= str_dup(&jsflow->remote_clientid, clientid);

	if (jsflow->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		jsflow->cm = mem_deref(jsflow->cm);
		conf_member_alloc(&jsflow->cm, NULL,
				  (struct iflow *)jsflow,
				  jsflow->remote_userid,
				  jsflow->remote_clientid,
				  "_",
				  0,
				  0,
				  "");
	}
	
	info("jsflow: set_remote_userclientid: handle=%d userid: %s clientid: %s\n",
	     jsflow->handle,
	     anon_id(userid_anon, jsflow->remote_userid),
	     anon_client(clientid_anon, jsflow->remote_clientid));

	if (!err && jsflow->handle) {
		pc_SetRemoteUserClientId(jsflow->handle,
					 jsflow->remote_userid,
					 jsflow->remote_clientid);
	}

	return err;
}

/* Data Channel related */
/*
bool jsflow_is_dcopen(struct jsflow *flow)
{
	int state;
	
	if (!flow)
		return false;

	if (flow->dc.handle == PC_INVALID_HANDLE)
		return false;

	state = pc_DataChannelState(flow->dc.handle);

	return state == DC_STATE_OPEN;
}


int jsflow_dcid(struct jsflow *flow)
{
	if (!flow)
		return -1;

	if (flow->dc.handle == PC_INVALID_HANDLE)
		return -1; 
	
	return pc_DataChannelId(flow->dc.handle);
}

int jsflow_dcsend(struct jsflow *flow,
			  const uint8_t *data, size_t len)
{
	if (!flow)
		return EINVAL;

	if (flow->dc.handle == PC_INVALID_HANDLE) {
		warning("jsflow(%p): dcsend: no valid data channel\n");
		return EINVAL;
	}
	
	pc_DataChannelSend(flow->dc.handle, (const int8_t *)data, len);

	return 0;
}
*/
/*
typedef void (jsflow_video_size_h)(int w,
					   int h,
					   const char *userid,
					   void *arg);
typedef int (jsflow_render_frame_h)(struct avs_vidframe *frame,
					    const char *userid,
					    void *arg);

void jsflow_set_video_handlers(jsflow_render_frame_h *render_frame_h,
				       jsflow_video_size_h *size_h,
				       void *arg)
{
}
*/
int jsflow_set_video_state(struct iflow *iflow,
			   enum icall_vstate vstate)
{
	struct jsflow *jsflow = (struct jsflow*)iflow;
	if (!jsflow)
		return EINVAL;

	info("jsflow(%p): set_video_state: %s\n",
	     jsflow, icall_vstate_name(vstate));

	pc_SetVideoState(jsflow->handle, vstate);
	jsflow->vstate = vstate;

	return 0;
}



/*
bool jsflow_has_video(struct jsflow *flow)
{
	int ret;
	
	if (!flow || flow->handle == PC_INVALID_HANDLE) {
		return false;
	}

	//if (flow->video.negotiated)
	//	return true;
	
	ret = pc_HasVideo(flow->handle);
	
	return ret != 0;
}

const char *jsflow_get_stats(struct jsflow *flow)
{
	return NULL;
}
*/

void jsflow_set_stats(struct jsflow* flow, float downloss, float rtt)
{
	if (!flow) {
		return;
	}

	flow->stats.dloss = downloss;
	flow->stats.rtt = rtt;
}

int jsflow_get_stats(struct iflow *flow,
		     struct iflow_stats *stats)
{
	struct jsflow *jsflow = (struct jsflow*)flow;
	if (!jsflow || !stats) {
		return EINVAL;
	}

	*stats = jsflow->stats;
	return 0;
}

static int set_aulevel(struct conf_member *cm, struct list *levell)
{
	struct audio_level *aulevel;
	int err = 0;
	
	if (!cm || !cm->active)
		return EINVAL;
		
	if (cm->audio_level_smooth > 0
	    || cm->audio_level > AUDIO_LEVEL_FLOOR) {

		err = audio_level_alloc(&aulevel, levell, false,
					cm->userid, cm->clientid,
					cm->audio_level,
					cm->audio_level_smooth);
		/* Make sure to count down the level if
		 * the source is no longer in the list due to
		 * selective audio.
		 */
		conf_member_set_audio_level(cm, 0);
	}

	return err;
}

int jsflow_get_aulevel(struct iflow *iflow,
		       struct list *levell)
{
	struct jsflow *jf = (struct jsflow *)iflow;
	struct audio_level *aulevel;
	struct le *le;
	int err;

	if (!levell)
		return EINVAL;	

	if (jf->stats.audio_level_smooth > 0
	    || jf->stats.audio_level > AUDIO_LEVEL_FLOOR) {
		err = audio_level_alloc(&aulevel, levell, true,
					jf->userid_self, jf->clientid_self,
					jf->stats.audio_level,
					jf->stats.audio_level_smooth);
		if (err)
			goto out;
	}

	if (jf->conv_type == ICALL_CONV_TYPE_ONEONONE) {
		if (!jf->cm)
			return ENOENT;

		set_aulevel(jf->cm, levell);
	}
	else {
		LIST_FOREACH(&jf->cml, le) {
			struct conf_member *cm = le->data;

			set_aulevel(cm, levell);
		}
	}
	list_sort(levell, audio_level_list_cmp, jf);

 out:
	if (err)
		list_flush(levell);

	return err;
}

int jsflow_update_ssrc(struct iflow *iflow, uint32_t ssrca, uint32_t ssrcv)
{
	struct jsflow *jf = (struct jsflow *)iflow;
	char ssrca_str[16];
	char ssrcv_str[16];

	if (!iflow)
		return EINVAL;

	re_snprintf(ssrca_str, sizeof(ssrca_str), "%u", ssrca);
	re_snprintf(ssrcv_str, sizeof(ssrcv_str), "%u", ssrcv);

	pc_UpdateSsrc(jf->handle, ssrca_str, ssrcv_str);

	return 0;
}


void pc_log(int level, const char *msg);

EMSCRIPTEN_KEEPALIVE
void pc_log(int level, const char *msg)
{
	loglv((enum log_level)level, msg);
}


void pc_local_sdp_handler(int self, int err,
			  const char *creator, /* e.g. firefox, chrome... */
			  const char *type, const char *sdp);

EMSCRIPTEN_KEEPALIVE
void pc_local_sdp_handler(int self, int err,
			  const char *creator, /* e.g. firefox, chrome... */
			  const char *type, const char *sdp)
{
	struct jsflow *flow = self2pc(self);
	struct sdp_session *sess = NULL;
	const char *modsdp = NULL;
	bool isoffer;

	if (!flow) {
		warning("jsflow: local_sdp_handler: instance: 0x%08X not found\n",
			(uint32_t)self);
		return;
	}

	if (err) {
		warning("pc_local_sdp_handler: error: %s -> %s\n", type, sdp);
		return;
	}


	isoffer = streq(type, "offer");
	sdp_check(sdp, true, isoffer, NULL, jf_norelay_handler, NULL, flow);
	
#if MODIFY_SDP
	/* Modify SDP here */
	if (isoffer) {
		sdp_dup(&sess, flow->conv_type, sdp, true);
		modsdp = sdp_modify_offer(sess,
					  flow->conv_type,
					  flow->vstate == ICALL_VIDEO_STATE_SCREENSHARE,
					  flow->audio.req_local_cbr);
	}
	else if (streq(type, "answer")) {
		sdp_dup(&sess, flow->conv_type, sdp, false);		
		modsdp = sdp_modify_answer(sess,
					   flow->conv_type,
					   flow->vstate == ICALL_VIDEO_STATE_SCREENSHARE,
					   flow->audio.req_local_cbr);
	}
	else {
		str_dup((char **)&modsdp, sdp);
	}
#else
	str_dup((char **)&modsdp, sdp);
#endif
	
	pc_SetLocalDescription(flow->handle, type, modsdp);

	mem_deref(sess);
	mem_deref((void *)modsdp);
}

void pc_start_gather_handler(int self);

EMSCRIPTEN_KEEPALIVE
void pc_start_gather_handler(int self)
{
	struct jsflow *flow = self2pc(self);

	debug("jsflow(%p): start_gather_handler\n", flow);
	
	if (!flow) {
		warning("pc_start_gather_handler: pc=%08X not found\n", self);
		return;
	}

	if (!flow->pending_gather) {
		warning("pc_start_gather_handler(%p): no pending gather\n",
			self);
		return;
	}

	flow->pending_gather = false;
	tmr_start(&flow->tmr_gather, GATHER_TIMEOUT, gather_timeout_handler, flow);	
}


void pc_signalling_handler(int self, int state);

EMSCRIPTEN_KEEPALIVE
void pc_signalling_handler(int self, int state)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("flow(%p): pc_signalling_handler: pc=%08X not found\n", flow, self);
		return;
	}

	info("flow(%p): signalling_handler state=%d call_type=%d\n", flow, state, flow->call_type);

	switch(state) {
	case PC_SIG_STATE_REMOTE_OFFER:
		if (flow->gather) {
			flow->gather = false;
			pc_CreateAnswer(flow->handle, flow->call_type, flow->vstate);
		}
		if (flow->bundle_update > 0) {
			--flow->bundle_update;
			info("jsflow(%p): createAnswer for bundle_update\n", flow);
			pc_AddDecoderAnswer(flow->handle);
		}
		break;

	case PC_SIG_STATE_STABLE:
		if (flow->bundle_sync) {
			info("jsflow(%p): sync bundle on stable state\n", flow);

			flow->bundle_update++;
			pc_SetRemoteDescription(flow->handle,
						SDP_TYPE_OFFER,
						flow->bundle_sync);
			flow->bundle_sync = mem_deref(flow->bundle_sync);
		}
		break;

	default:
		break;
	}
	
}

void pc_gather_handler(int self, const char *type, const char *sdp);

EMSCRIPTEN_KEEPALIVE
void pc_gather_handler(int self, const char *type, const char *sdp)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("pc_gather_handler: pc=%08X not found\n", self);
		return;
	}

	info("flow(%p): gather_handler\n", flow);

	flow->gather = false;
	flow->pending_gather = false;

	tmr_cancel(&flow->tmr_gather);
	IFLOW_CALL_CB(flow->iflow, gatherh,
		flow->iflow.arg);
}

void pc_connection_handler(int self, const char *state);

EMSCRIPTEN_KEEPALIVE
void pc_connection_handler(int self, const char *state)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("pc_connection_handler: invalid instance: %d\n", self);
		return;
	}

	info("flow(%p): connection_handler: state=%s\n", flow, state);

	if (streq(state, "connected")) {
		if (tmr_isrunning(&flow->tmr_disconnect))
			tmr_cancel(&flow->tmr_disconnect);
		else {
			IFLOW_CALL_CB(flow->iflow, estabh,
				"dtls", "opus", flow->iflow.arg);
			IFLOW_CALL_CB(flow->iflow, rtp_stateh,
				true, true, flow->iflow.arg);
		}
	}
	else if (streq(state, "failed")) {
		if (tmr_isrunning(&flow->tmr_disconnect))
			tmr_cancel(&flow->tmr_disconnect);
		IFLOW_CALL_CB(flow->iflow, restarth,
			false, flow->iflow.arg);
	}
	else if (streq(state, "closed")) {
		send_close(flow, EINTR);
	}
}

void pc_set_stats(int self,
		  int audio_level,
		  int apkts_recv,
		  int vpkts_recv,
		  int apkts_sent,
		  int vpkts_sent,
		  int downloss,
		  int rtt);

EMSCRIPTEN_KEEPALIVE
void pc_set_stats(int self,
		  int audio_level,
		  int apkts_recv,
		  int vpkts_recv,
		  int apkts_sent,
		  int vpkts_sent,
		  int downloss,
		  int rtt)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("pc: set_stats: invalid handle: 0x%08X\n", self);
		return;
	}

	info("pc_set_stats: level: %d ar: %d vr: %d as: %d vs: %d rtt=%d dloss=%d\n",
	     audio_level, apkts_recv, vpkts_recv, apkts_sent, vpkts_sent, rtt, downloss);


	flow->stats.audio_level = g_jf.muted ? 0 : audio_level;
	
	if (g_jf.muted) {
		if (flow->stats.audio_level_smooth > 0)
			flow->stats.audio_level_smooth--;
		else
			flow->stats.audio_level_smooth = 0;
	}
	else if (audio_level > AUDIO_LEVEL_FLOOR)
		flow->stats.audio_level_smooth = AUDIO_LEVEL_CEIL;
	else if (flow->stats.audio_level_smooth > 0)
		flow->stats.audio_level_smooth--;
	
	flow->stats.apkts_recv = apkts_recv;
	flow->stats.vpkts_recv = vpkts_recv;
	flow->stats.apkts_sent = apkts_sent;
	flow->stats.vpkts_sent = vpkts_sent;
	flow->stats.dloss = (float)downloss;
	flow->stats.rtt = (float)rtt;	
}



void pc_set_audio_level(int self,
			uint32_t ssrc,
			int audio_level);

EMSCRIPTEN_KEEPALIVE
void pc_set_audio_level(int self,
			uint32_t ssrc,
			int audio_level)
{
	struct jsflow *jf = self2pc(self);
	struct conf_member *cm;

	if (!jf) {
		warning("pc: set_audio_level: invalid handle: 0x%08X\n", self);
		return;
	}

	if (jf->conv_type == ICALL_CONV_TYPE_ONEONONE)
		cm = jf->cm;
	else
		cm = conf_member_find_by_ssrca(&jf->cml, ssrc);
	if (cm) {
		conf_member_set_audio_level(cm, audio_level);
	}
	else {
		if (ssrc != 0) {
			warning("jsflow(%p): no conf member for ssrc: %u\n", jf, ssrc);
		}
	}
}


void dc_estab_handler(int self, int dc);

EMSCRIPTEN_KEEPALIVE
void dc_estab_handler(int self, int dc)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("pc: dc_estab_handler: invalid handle: 0x%08X\n", self);
		return;
	}

	if (flow->dc.handle == PC_INVALID_HANDLE) {
		flow->dc.handle = dc;
	}

	info("flow(%p): dc_estab_handler\n", flow);
	/* dce_estabh should be called only when channel is actually opened
	IFLOW_CALL_CB(flow->iflow, dce_estabh,
		flow->iflow.arg);
	*/
}


void dc_state_handler(int self, int state);

EMSCRIPTEN_KEEPALIVE
void dc_state_handler(int self, int state)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("flow: dc_state_handler: invalid handle: 0x%08X\n", self);
		return;
	}

	if (flow->dc.handle == PC_INVALID_HANDLE) {
		warning("flow(%p): dc_state_handler: no data channel\n", flow);
		return;
	}
	

	info("flow(%p): dc-state: %d\n", flow, state);
	
	
	switch(state) {
	case DC_STATE_CONNECTING:
	case DC_STATE_CLOSING:
		break;
		
	case DC_STATE_OPEN:
		IFLOW_CALL_CB(flow->iflow, dce_estabh,
			flow->iflow.arg);
		break;

	case DC_STATE_ERROR:
	case DC_STATE_CLOSED:
		if (flow->closed) {
			IFLOW_CALL_CB(flow->iflow, dce_closeh,
				      flow->iflow.arg);
		}
		else {
			IFLOW_CALL_CB(flow->iflow, restarth,
				      false, flow->iflow.arg);
		}
		break;

	default:
		break;
	}
}


void dc_data_handler(int self, const uint8_t *data, int len);

EMSCRIPTEN_KEEPALIVE
void dc_data_handler(int self, const uint8_t *data, int len)
{
	struct jsflow *flow = self2pc(self);

	if (!flow) {
		warning("flow: dc_data_handler: invalid handle: 0x%08X\n", self);
		return;
	}

	if (flow->dc.handle == PC_INVALID_HANDLE) {
		warning("flow(%p): dc_data_handler: no data channel\n", flow);
		return;
	}
	
	info("flow(%p): dc_data_handler: length=%d(%p)\n", flow, len, data);
	IFLOW_CALL_CB(flow->iflow, dce_recvh,
		data, len, flow->iflow.arg);
}


void pc_get_media_key(int self, int index);

EMSCRIPTEN_KEEPALIVE
void pc_get_media_key(int self, int index)
{
#if DOUBLE_ENCRYPTION
	struct jsflow *flow = self2pc(self);
	uint8_t media_key[E2EE_SESSIONKEY_SIZE];
	uint32_t current;
	uint64_t ts;

	int err = 0;

	if (!flow) {
		warning("jsflow: pc_get_key: invalid handle: 0x%08X\n",
			self);
		return;
	}

	if (flow->handle == PC_INVALID_HANDLE) {
		warning("flow(%p): pc_get_key: no flow\n", flow);
		return;
	}

	//info("pc_get_media_key idx %d\n", index);

	err = keystore_get_media_key(flow->frame.keystore, index,
				     media_key, E2EE_SESSIONKEY_SIZE);
	if (err)
		goto out;

	err = keystore_get_current(flow->frame.keystore, &current, &ts);
	if (err)
		goto out;

	pc_SetMediaKey(flow->handle,
		       index,
		       current,
		       media_key,
		       E2EE_SESSIONKEY_SIZE);

out:
	sodium_memzero(media_key, E2EE_SESSIONKEY_SIZE);
#endif
}


void pc_get_current_media_key(int self);

EMSCRIPTEN_KEEPALIVE
void pc_get_current_media_key(int self)
{
#if DOUBLE_ENCRYPTION
	struct jsflow *flow = self2pc(self);
	uint8_t media_key[E2EE_SESSIONKEY_SIZE];
	uint32_t current;
	uint64_t ts;

	int err = 0;

	if (!flow) {
		warning("jsflow: pc_get_current_media_key: invalid handle: 0x%08X\n",
			self);
		return;
	}

	if (flow->handle == PC_INVALID_HANDLE) {
		warning("flow(%p): pc_get_current_media_key: no flow\n", flow);
		return;
	}

	err = keystore_get_current(flow->frame.keystore, &current, &ts);
	if (err)
		goto out;

	err = keystore_get_media_key(flow->frame.keystore, current,
				     media_key, E2EE_SESSIONKEY_SIZE);
	if (err)
		goto out;

	pc_SetMediaKey(flow->handle,
		       current,
		       current,
		       media_key,
		       E2EE_SESSIONKEY_SIZE);

out:
	sodium_memzero(media_key, E2EE_SESSIONKEY_SIZE);
#if 0
	if (err) {
		warning("pc_get_current_media_key err=%d\n", err);
	}
#endif
#endif
}


void pc_set_callbacks(
	pc_SetEnv_t *setEnv,
	pc_New_t *new,
	pc_Create_t *create,
	pc_Close_t *close,
	pc_HeapFree_t *heapFree,
	pc_AddTurnServer_t *addTurnServer,
	pc_IceGatheringState_t *iceGatheringState,
	pc_SignallingState_t *signallingState,
	pc_ConnectionState_t *connectionState,
	pc_CreateDataChannel_t *createDataChannel,
	pc_CreateOffer_t *createOffer,
	pc_CreateAnswer_t *createAnswer,
	pc_AddDecoderAnswer_t *addDecoderAnswer,
	pc_AddUserInfo_t *addUserInfo,
	pc_RemoveUserInfo_t *removeUserInfo,
	pc_SetRemoteDescription_t *setRemoteDescription,
	pc_SetLocalDescription_t *setLocalDescription,
	pc_LocalDescription_t *localDescription,
	pc_SetMute_t *setMute,
	pc_GetMute_t *getMute,
	pc_GetLocalStats_t *getLocalStats,
	pc_SetRemoteUserClientId_t *setRemoteUserClientId,
	pc_HasVideo_t *hasVideo,
	pc_SetVideoState_t *setVideoState,

	pc_DataChannelId_t *dataChannelId,
	pc_DataChannelState_t *dataChannelState,
	pc_DataChannelSend_t *dataChannelSend,
	pc_DataChannelClose_t *dataChannelClose,

	pc_SetMediaKey_t *setMediaKey,

	pc_UpdateSsrc_t *updateSsrc);

EMSCRIPTEN_KEEPALIVE
void pc_set_callbacks(
	pc_SetEnv_t *setEnv,
	pc_New_t *new,
	pc_Create_t *create,
	pc_Close_t *close,
	pc_HeapFree_t *heapFree,
	pc_AddTurnServer_t *addTurnServer,
	pc_IceGatheringState_t *iceGatheringState,
	pc_SignallingState_t *signallingState,
	pc_ConnectionState_t *connectionState,
	pc_CreateDataChannel_t *createDataChannel,
	pc_CreateOffer_t *createOffer,
	pc_CreateAnswer_t *createAnswer,
	pc_AddDecoderAnswer_t *addDecoderAnswer,
	pc_AddUserInfo_t *addUserInfo,
	pc_RemoveUserInfo_t *removeUserInfo,
	pc_SetRemoteDescription_t *setRemoteDescription,
	pc_SetLocalDescription_t *setLocalDescription,
	pc_LocalDescription_t *localDescription,
	pc_SetMute_t *setMute,
	pc_GetMute_t *getMute,
	pc_GetLocalStats_t *getLocalStats,
	pc_SetRemoteUserClientId_t *setRemoteUserClientId,
	pc_HasVideo_t *hasVideo,
	pc_SetVideoState_t *setVideoState,

	pc_DataChannelId_t *dataChannelId,
	pc_DataChannelState_t *dataChannelState,
	pc_DataChannelSend_t *dataChannelSend,
	pc_DataChannelClose_t *dataChannelClose,

	pc_SetMediaKey_t *setMediaKey,

	pc_UpdateSsrc_t *updateSsrc)
{
	pc_SetEnv = setEnv;
	pc_New = new;
	pc_Create = create;
	pc_Close = close;
	pc_HeapFree = heapFree;
	pc_AddTurnServer = addTurnServer;
	pc_IceGatheringState = iceGatheringState;
	pc_SignallingState = signallingState;
	pc_ConnectionState = connectionState;
	pc_CreateDataChannel = createDataChannel;
	pc_CreateOffer = createOffer;
	pc_CreateAnswer = createAnswer;
	pc_AddDecoderAnswer = addDecoderAnswer;
	pc_AddUserInfo = addUserInfo;
	pc_RemoveUserInfo = removeUserInfo;
	pc_SetRemoteDescription = setRemoteDescription;
	pc_SetLocalDescription = setLocalDescription;
	pc_LocalDescription = localDescription;
	pc_SetMute = setMute;
	pc_GetMute = getMute;
	pc_GetLocalStats = getLocalStats;
	pc_SetRemoteUserClientId = setRemoteUserClientId;
	pc_HasVideo = hasVideo;
	pc_SetVideoState = setVideoState;
	
	pc_DataChannelId = dataChannelId;
	pc_DataChannelState = dataChannelState;
	pc_DataChannelSend = dataChannelSend;
	pc_DataChannelClose = dataChannelClose;

	pc_SetMediaKey = setMediaKey;
	pc_UpdateSsrc = updateSsrc;

	jsflow_init();
}
