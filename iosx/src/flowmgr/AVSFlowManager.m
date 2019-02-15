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
#include <pthread.h>

#include "TargetConditionals.h"
#if TARGET_OS_IPHONE
#import <MobileCoreServices/MobileCoreServices.h>
#else
#import <CoreServices/CoreServices.h>
#endif

#import <Foundation/Foundation.h>

#include <dispatch/dispatch.h>

#include <re/re.h>
#include <avs.h>
#include <avs_wcall.h>

#import "AVSMediaManager.h"

#import "AVSFlowManager.h"
#import "AVSCapturer.h"


#if TARGET_OS_IPHONE
#import "AVSVideoView.h"
#else
typedef NSView AVSVideoView;
#endif

#define DISPATCH_Q \
	dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)

static struct {
	bool initialized;
	pthread_t tid;
	enum log_level log_level;
	int err;
	uint64_t avs_flags;
} fmw = {
	.initialized = false,
	.tid = NULL,
	.log_level = LOG_LEVEL_DEBUG,
	.err = 0,
	.avs_flags = 0,
};

static NSComparisonResult (^conferenceComparator)(id, id) =
	^(id obj1, id obj2) {
	        const char *u1 = [(NSString *)obj1 UTF8String];
	        const char *u2 = [(NSString *)obj2 UTF8String];
		uint32_t pos1 = conf_pos_calc(u1);
		uint32_t pos2 = conf_pos_calc(u2);

		if (pos1 > pos2)
			return (NSComparisonResult)NSOrderedDescending;
		else if (pos1 < pos2)
			return (NSComparisonResult)NSOrderedAscending;
		else
			return (NSComparisonResult)NSOrderedSame;	
        };

static int render_frame_h(struct avs_vidframe * frame, const char *userid, void *arg);

static void audio_state_change_h(enum flowmgr_audio_receive_state state, void *arg);

static void video_size_h(int width, int height, const char *userid, void *arg);

@implementation AVSVideoCaptureDevice

@synthesize deviceId = _deviceId;
@synthesize deviceName = _deviceName;

- (id) initWithId: (NSString*)devid name: (NSString*)name
{
	self = [super init];
	if (self) {
		self->_deviceId = devid;
		self->_deviceName = name;
	}
	return self;
}
@end

@implementation AVSVideoStateChangeInfo 

@synthesize state = _state;
@synthesize reason = _reason;

- (id) initWithState:(AVSFlowManagerVideoReceiveState)state reason:(AVSFlowManagerVideoReason)reason
{
	self = [super init];
	if (self) {
		self->_state = state;
		self->_reason = reason;
	}
	return self;
}
@end

@implementation AVSAudioStateChangeInfo

@synthesize state = _state;

- (id) initWithState:(AVSFlowManagerAudioReceiveState)state
{
	self = [super init];
	if (self) {
		self->_state = state;
	}
	return self;
}
@end

@interface AVSFlowManager() <AVSMediaManagerObserver>
{
	AVSCapturer *_capturer;
	NSMutableArray *_videoViews;
	NSLock *_viewLock;
}

- (void)mediaCategoryChanged:(NSString *)convId category:(enum AVSFlowManagerCategory)mcat;

- (void)applicationWillResignActive:(NSNotification *)notification;
- (void)applicationDidBecomeActive:(NSNotification *)notification;

@property (nonatomic, assign) struct flowmgr *flowManager;

@property (nonatomic, weak) id<AVSFlowManagerDelegate> delegate;

@property (nonatomic, strong) NSMutableDictionary *collection;

@end

AVSFlowManager *g_Fm = nil;

static NSString *avsString(const char *s)
{
	NSString *ss = s ? [NSString stringWithUTF8String:s] : [NSString new];
	return ss ? ss : [NSString new];
}

static NSData *avsData(const char *d, size_t dlen)
{
	return d ? [NSData dataWithBytes:d length:(NSUInteger)dlen] : nil;
}


static NSString *mime2uti(const char *ctype)
{
	NSString *mime = avsString(ctype);
	NSString *uti;

	if (mime == nil)
		return nil;

	uti = CFBridgingRelease(
		    UTTypeCreatePreferredIdentifierForTag(kUTTagClassMIMEType,
						   (__bridge CFStringRef)mime,
						    kUTTypeContent));
	if (uti != nil)
		return uti;


	// UTI / Core Services couldn't help us.
	for (NSString *prefix in @[@"application/json", @"text/x-json"]) {
		if ([mime hasPrefix:prefix]) {
			uti = @"public.json";
			break;
		}
	}

	return uti;	
}


static void log_handler(uint32_t lve, const char *msg, void *arg)
{

#ifdef AVS_LOG_DEBUG
	NSLog(@"%s", msg);
#endif

	if (fmw.log_level > lve)
		return;
	
	NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
	NSString *nsmsg = avsString(msg);
	
	if (nsmsg) {
		[nc postNotificationName:@"AVSLogMessageNotification"
				  object:nil
				userInfo:@{@"message": nsmsg}];
	}
}


static struct log log_def = {
	.h = log_handler
};



static void *avs_thread(void *arg)
{
	int err;
    uint64_t* avs_flags = (uint64_t*)arg;
    
	info("avs_thread: starting...\n");

	/* Force the loading of wcall symbols! 
	 * Can't this be done with a linker directive?
	 */
	wcall_get_members(NULL, NULL);
	
	err = libre_init();
	if (err) {
		warning("flowmgr_thread: libre_init failed (%m)\n", err);
		return NULL;
	}

	log_enable_stderr(false);
	log_set_min_level(LOG_LEVEL_DEBUG);
	log_register_handler(&log_def);

	NSLog(@"Calling avs_init");
	
	err = avs_init(*avs_flags);
	if (err) {
		warning("flowmgr_thread: avs_init failed (%m)\n", err);
		return NULL;
	}

	err = flowmgr_init("voe");
	fmw.initialized = err == 0;
	fmw.err = err;
	if (err) {
		error("avs_thread: failed to init flowmgr\n");
		goto out;
	}

	re_main(NULL);

out:
	flowmgr_close();
	avs_close();

	log_unregister_handler(&log_def);

	info("avs_thread: done\n");

	return NULL;
}


static int req_handler(struct rr_resp *ctx,
		       const char *path, const char *method,
		       const char *ctype,
		       const char *content, size_t clen, void *arg)
{
	AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;

	debug("AVSFlowManager::req_handler: fm=%p\n", fm);

	if (!fm)
		return EINVAL;

	if (fm.delegate == nil)
		return ENOSYS;

	NSString *sePath = avsString(path);
	NSString *seMethod = avsString(method);
	NSString *seCtype = mime2uti(ctype);
	NSData   *seData = avsData(content, clen);

	dispatch_async(DISPATCH_Q, ^{
	        BOOL success;
	        success = [fm.delegate
			      requestWithPath:sePath
			      method:seMethod
			      mediaType:seCtype
			      content:seData
			      context:ctx];
		if (!success) {
			// Should we call response handler here with error?
		}
	});

	return 0;
}


#if 0 // Enable when networkQuality is enabled
static void netq_handler(int err, const char *convid, float q, void *arg)
{
    debug("AVSFlowManager::netq_handler: err=%d q=%f\n", err, q);
    
    AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;

    if ( [fm.delegate respondsToSelector:@selector(networkQuality:conversation:)] ) {
        dispatch_async(DISPATCH_Q, ^{
            [fm.delegate networkQuality:q conversation:avsString(convid)];
        });
    }
}

#endif


static void err_handler(int err, const char *convid, void *arg)
{
    debug("AVSFlowManager::err_handler: err=%d\n", err);
    
    AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;
    
    if ( ( err == ETIMEDOUT ) && [fm.delegate respondsToSelector:@selector(mediaWarningOnConversation:)] ) {
        dispatch_async(DISPATCH_Q, ^{
            [fm.delegate mediaWarningOnConversation:avsString(convid)];
        });
    }
    else {
        dispatch_async(DISPATCH_Q, ^{
            [fm.delegate errorHandler:err conversationId:avsString(convid) context:arg];
        });
    }
}

#if 0
static inline enum log_level convert_logl(AVSFlowManagerLogLevel logLevel)
{
	switch(logLevel) {
	case FLOWMANAGER_LOGLEVEL_DEBUG:
		return LOG_LEVEL_DEBUG;
		
	case FLOWMANAGER_LOGLEVEL_INFO:
		return LOG_LEVEL_INFO;

	case FLOWMANAGER_LOGLEVEL_WARN:
		return LOG_LEVEL_WARN;
		
	case FLOWMANAGER_LOGLEVEL_ERROR:
		return LOG_LEVEL_ERROR;
		
	default:
		return LOG_LEVEL_ERROR;
	}
}
#endif

@implementation AVSFlowManager


+ (void)setLogLevel:(AVSFlowManagerLogLevel)logLevel
{
#if 0 // Don't set log level, use default
	fmw.log_level = convert_logl(logLevel);
#endif
}


+ (NSComparator)conferenceComparator
{
	return conferenceComparator;
}


- (void)appendLogForConversation:(NSString *)convId message:(NSString *)msg
{

	warning("NOT IMPLEMENTED: appendLogForConversation\n");
}


- (NSMutableDictionary *)collection
{
    if ( !_collection ) {
        _collection = [[NSMutableDictionary alloc] init];
    }
    
    return _collection;
}

static AVSFlowManager *_AVSFlowManagerInstance = nil;

+ (instancetype)getInstance
{
    return _AVSFlowManagerInstance;
}

- (instancetype)init:(uint64_t)avs_flags
{
	struct flowmgr *fm;
	int err;

	debug("AVSFlowManager::init");
	
	if (fmw.tid == NULL) {
        fmw.avs_flags = avs_flags;
		err = pthread_create(&fmw.tid, NULL, avs_thread, &fmw.avs_flags);
		if (err)
			return nil;

		while(!fmw.initialized && fmw.err == 0)
			usleep(100000);
	}
	if (fmw.err) {
		error("AVSFlowManager::init: error initializing subsystems");
		return nil;
	}

	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_alloc, &fm,
			    req_handler, err_handler,
			    (__bridge void *)(self));

	if (err)
		return nil;

	wcall_set_video_handlers(render_frame_h,
				 video_size_h,
				 (__bridge void *)(self));

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_audio_state_handler, fm,
			     audio_state_change_h,
			     (__bridge void *)(self));

	self = [super init];

	if ( self ) {
		self.flowManager = fm;
		self.delegate = nil;

		[AVSMediaManagerChangeNotification addObserver:self];
		_AVSFlowManagerInstance = self;
		_videoViews = [[NSMutableArray alloc] init];
		_viewLock = [[NSLock alloc] init];

#if TARGET_OS_IPHONE
		[[NSNotificationCenter defaultCenter] addObserver:self
		      selector:@selector(applicationWillResignActive:)
		      name:UIApplicationWillResignActiveNotification 
		      object:NULL];
		[[NSNotificationCenter defaultCenter] addObserver:self
		      selector:@selector(applicationDidBecomeActive:)
		      name:UIApplicationDidBecomeActiveNotification
		      object:NULL];
#endif

		g_Fm = self;
	}

#if !(TARGET_IPHONE_SIMULATOR)
	_capturer = [[AVSCapturer alloc] init];
#endif
	
	return self;
}

- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate flowManager:(struct flowmgr *)flowManager mediaManager:(id)mediaManager avs_flags:(uint64_t)flags
{
    debug("AVSFlowManager::initWithDelegate:%p fm=%p mm=%p\n",
          delegate, flowManager, mediaManager);
    
    self = [super init];
    if (self) {
        self.flowManager = flowManager;
        self.delegate = delegate;
        g_Fm = self;
        _AVSFlowManagerInstance = self;

	_videoViews = [[NSMutableArray alloc] init];
	_viewLock = [[NSLock alloc] init];
        
        FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_start);
    }
    
    return self;
}

- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate flowManager:(struct flowmgr *)flowManager mediaManager:(id)mediaManager
{
	debug("AVSFlowManager::initWithDelegate:%p fm=%p mm=%p\n",
	      delegate, flowManager, mediaManager);
	
	self = [super init];
	if (self) {
		self.flowManager = flowManager;
		self.delegate = delegate;
		g_Fm = self;
		_AVSFlowManagerInstance = self;
		_videoViews = [[NSMutableArray alloc] init];
		_viewLock = [[NSLock alloc] init];

		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_start);
	}

	return self;
}


- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate
	mediaManager:(id)mediaManager
{
	return [self initWithDelegate:delegate mediaManager:mediaManager flags:0];
}

- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate
	mediaManager:(id)mediaManager flags:(uint64_t)avs_flags
{
	debug("AVSFlowManager::initWithDelegate:%p mm=%p\n",
	      delegate, mediaManager);
	
	self = [self init:avs_flags];
    
	if ( self ) {
		self.delegate = delegate;
		_AVSFlowManagerInstance = self;
		_videoViews = [[NSMutableArray alloc] init];
		_viewLock = [[NSLock alloc] init];
		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_start);
	}

	return self;
}

- (void)dealloc
{
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_free, self.flowManager);

	if (g_Fm == self)
		g_Fm = nil;
}


- (NSArray *)events 
{
	NSMutableArray *eventNames = [NSMutableArray array];

	return eventNames;
}


- (void)processResponseWithStatus:(int)status
			   reason:(NSString *)reason
			mediaType:(NSString *)mtype
			  content:(NSData *)content
			  context:(void const *)ctx
{
	warning("NOT IMPLEMENTED: processResponseWithStatus\n");
}


- (BOOL)processEventWithMediaType:(NSString *)mtype
	     content:(NSData *)content
{
	warning("NOT IMPLEMENTED: processEventWithMediaType\n");

	return false;
}


- (BOOL)acquireFlows:(NSString *)convId
{
	warning("NOT IMPLEMENTED: acquireFlows\n");

	return false;
}


- (void)releaseFlows:(NSString *)convId
{
	warning("NOT IMPLEMENTED: releaseFlows\n");
}


- (void)setActive:(NSString *)convId active:(BOOL)active
{
	warning("NOT IMPLEMENTED: setActive\n");
}


- (void)addUser:(NSString *)convId userId:(NSString *)userId
	   name:(NSString *)name
{
	warning("NOT IMPLEMENTED: addUser\n");
}


- (void)setSelfUser:(NSString *)userId
{
	warning("NOT IMPLEMENTED: setSelfUser\n");
}


- (void)refreshAccessToken:(NSString *)token type:(NSString *)type
{
	warning("NOT IMPLEMENTED: refreshAccessToken\n");
}


- (void)mediaCategoryChanged:(NSString *)convId category:(AVSFlowManagerCategory)cat
{
	warning("NOT IMPLEMENTED: mediaCategoryChanged\n");
}


- (BOOL)isMediaEstablishedInConversation:(NSString *)convId
{
	warning("NOT IMPLEMENTED: isMediaEstablishedInConversation\n");

	return (BOOL)false;
}	


- (void)networkChanged
{
	warning("NOT IMPLEMENTED: networkChanged\n");
}


- (BOOL)isReady
{
	int err;
        bool is_ready;

	err = flowmgr_is_ready(self.flowManager, &is_ready);

	return err ? NO : (BOOL)is_ready;
}



- (BOOL)isMuted
{
	int err;
        bool muted;

	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_get_mute, self.flowManager, &muted);

	return (BOOL)muted;
}

- (int)setMute:(BOOL)muted
{
	int err;

	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_set_mute, self.flowManager, muted ? true : false);

	return err;	
}



- (NSArray *)sortConferenceParticipants:(NSArray *)participants
{
	struct list partl = LIST_INIT;
	NSMutableArray *sortedArray;
	NSUInteger count = participants.count;
	struct le *le;
	NSUInteger i;
	int err;
	
	sortedArray = [NSMutableArray arrayWithCapacity:count];

	for(i = 0; i < count; ++i) {
		NSString *p = [participants objectAtIndex:i];
		struct conf_part *cp;

		err = conf_part_add(&cp, &partl, [p UTF8String], NULL);
		if (err)
			goto out;
	}	

	err = ENOSYS;//flowmgr_sort_participants(&partl);
	if (err)
		goto out;

	i = 0;
	le = partl.head;
	while (le) {
		struct conf_part *cp = le->data;

		if (cp)
			[sortedArray addObject:avsString(cp->uid)];

		le = le->next;
		list_unlink(&cp->le);
		
		mem_deref(cp);
	}

out:
	if (err)
		return nil;
	else
		return sortedArray;
}



- (AVSFlowManager *)getFlowManager
{
    return self;
}

- (AVSMediaManager *)getMediaManager
{
    return [AVSMediaManager defaultMediaManager];
}


- (void)callInterruptionStartInConversation:(NSString *)convId
{
	warning("NOT IMPLEMENTED: callInterruptionStartInConversation\n");
}


- (void)callInterruptionEndInConversation:(NSString *)convId
{
	warning("NOT IMPLEMENTED: callInterruptionEndInConversation\n");
}


- (void)updateModeInConversation:(NSString *)convId withCategory:(AVSFlowManagerCategory)category
{
	AVSMediaManager *mm = [self getMediaManager];
    
	if ( category == FLOWMGR_MCAT_CALL ) {
		if ( convId ) {
			if ( mm.isInterrupted == NO ) {
				[mm setCallState:YES forConversation:convId];
			}
			else {
				debug("IGNORING REQUEST TO SET MCAT TO CALL DURING AN INTERRUPTION");
			}
		}
		//debug("Update Mode in Conversation - PLAY DONE");
    }

	if ( category == FLOWMGR_MCAT_CALL_VIDEO ) {
		if ( convId ) {
			if ( mm.isInterrupted == NO ) {
				[mm setVideoCallState: convId];
			}
			else {
				debug("IGNORING REQUEST TO SET MCAT TO CALL DURING AN INTERRUPTION");
			}
		}
	}
    
	if ( category == FLOWMGR_MCAT_NORMAL ) {
		if ( convId ) {
			[mm setCallState:NO forConversation:convId];
		}
		//debug("Update Mode in Conversation - STOP DONE");
	}
}

- (void)updateVolumeForUser:(NSString *)uid inVol:(float)input outVol:(float)output inConversation:(NSString *)convId
{
    if ( [self.delegate respondsToSelector:@selector(didUpdateVolume:conversationId:participantId:)] ) {
        [self.delegate didUpdateVolume:input conversationId:convId participantId:FlowManagerSelfUserParticipantIdentifier];
        [self.delegate didUpdateVolume:output conversationId:convId participantId:uid];
    }	
}


- (void)handleError:(int)error inConversation:(NSString *)convId
{
    [self.delegate errorHandler:error conversationId:convId context:nil];
}

- (void)mediaEstablishedInConversation:(NSString *)convId
{
    [self.delegate didEstablishMediaInConversation:convId];    
}

- (void)mediaEstablishedInConversation:(NSString *)convId forUser:(NSString *)userId
{
	if ([self.delegate respondsToSelector:@selector(didEstablishMediaInConversation:forUser:)]) {
		[self.delegate didEstablishMediaInConversation:convId forUser:userId];
	}
}

- (void)conferenceParticipants:(NSArray *)participants inConversation:(NSString *)convId
{
	debug("conferenceParticipants: %d\n", (int)participants.count);
	if ( [self.delegate respondsToSelector:@selector(conferenceParticipantsDidChange:inConversation:)] ) {
		debug("conferenceParticipants: calling didChange...\n");
		
		[self.delegate conferenceParticipantsDidChange:participants inConversation:convId];
	}
}

- (void)avsMediaManagerDidChange:(AVSMediaManagerChangeNotification *)notification
{

}

- (void)setEnableLogging:(BOOL)enable
{
	warning("NOT IMPLEMENTED: setEnableLogging\n");
}


- (void)setEnableMetrics:(BOOL)enable
{
	warning("NOT IMPLEMENTED: setEnableMetrics\n");
}


- (void)setSessionId:(NSString *)sessId forConversation:(NSString *)convId
{
	warning("NOT IMPLEMENTED: setSessionId\n");
}


- (void)applicationWillResignActive:(NSNotification *)notification
{
}

- (void)applicationDidBecomeActive:(NSNotification *)notification;
{
}


- (BOOL)canSendVideoForConversation:(NSString *)convId
{
	int can_send = 0;
	const char *cid = [convId UTF8String];

	FLOWMGR_MARSHAL_RET(fmw.tid, can_send, flowmgr_can_send_video, self.flowManager, cid);

	return can_send > 0 ? YES : NO;
}


- (BOOL)isSendingVideoInConversation:(NSString *)convId
		      forParticipant:(NSString *)partId
{
	const char *cid = [convId UTF8String];
	const char *pid = [partId UTF8String];
	int ret;

	FLOWMGR_MARSHAL_RET(fmw.tid, ret, flowmgr_is_sending_video,
			    self.flowManager, cid, pid);

	return ret != 0;
}


- (void)setVideoSendState:(AVSFlowManagerVideoSendState)state
          forConversation:(NSString *)convId
{
	dispatch_async(DISPATCH_Q, ^{
		const char *cid = convId ? [convId UTF8String] : NULL;
		enum flowmgr_video_send_state fmstate =
			state == FLOWMANAGER_VIDEO_SEND_NONE ? FLOWMGR_VIDEO_SEND_NONE :
			FLOWMGR_VIDEO_SEND;

		debug("%s: conv=%s state=%d\n", __FUNCTION__, cid ? cid : "NULL", state);
		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_video_send_state, self.flowManager,
			cid, fmstate);
	});
}

- (void)attachVideoPreview:(UIView *)view
{
	[_capturer attachPreview: view];
}

- (void)detachVideoPreview:(UIView *)view
{
	[_capturer detachPreview: view];
}

- (void)startVideoCapture
{
	[_capturer startWithWidth: 640 Height: 480 MaxFps: 15];
}

- (void)stopVideoCapture
{
	[_capturer stop];
}

- (void)attachVideoView:(UIView *)view
{
	[_viewLock lock];
	[_videoViews addObject: view];
	[_viewLock unlock];
}

- (void)detachVideoView:(UIView *)view
{
	[_viewLock lock];
	[_videoViews removeObject: view];
	[_viewLock unlock];
}

- (BOOL)renderFrame:(struct avs_vidframe *)frame forUser:(NSString *)userid
{
	BOOL sizeChanged = NO;
	
#if TARGET_OS_IPHONE
	char userid_anon[ANON_ID_LEN];
	BOOL found = NO;

	[_viewLock lock];
	for (unsigned int v = 0; v < _videoViews.count; v++) {
		AVSVideoView *view = [_videoViews objectAtIndex: v];

		if ([view.userid isEqualToString: userid]) {
			sizeChanged |= [view handleFrame:frame];
			found = YES;
		}
	}
	[_viewLock unlock];

	if (!found) {
		warning("flowmgr: render_frame couldnt find renderer for frame "
			"belonging to %s\n",
			anon_id(userid_anon, [userid UTF8String]));
	}

#endif

	return sizeChanged;
}

- (NSArray*)getVideoCaptureDevices
{
#if 0
	struct list *device_list = NULL;
	struct le *le;

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_get_video_capture_devices, self.flowManager, &device_list);
	NSMutableArray *devArray = [[NSMutableArray alloc] initWithCapacity: list_count(device_list)];
	if(device_list) {
		LIST_FOREACH(device_list, le) {
			struct videnc_capture_device *dev = (struct videnc_capture_device*)le->data;

			NSString *dev_id = [NSString stringWithUTF8String: dev->dev_id];
			NSString *dev_name = [NSString stringWithUTF8String: dev->dev_name];
			AVSVideoCaptureDevice *nsDev = [[AVSVideoCaptureDevice alloc] initWithId: dev_id
									    name: dev_name];
			[devArray addObject:nsDev];
		}
		mem_deref(device_list);
	}
	return devArray;
#else
	return nil;
#endif
}


- (void)setVideoCaptureDevice:(NSString *)deviceId forConversation:(NSString *)convId
{
	if (_capturer) {
		[_capturer setCaptureDevice:deviceId];
	}
}

- (int)setAudioEffect:(AVSAudioEffectType) effect
{
    int ret=0;
    
    enum audio_effect effect_type = AUDIO_EFFECT_CHORUS_MIN;
    if (effect == AVSAudioEffectTypeChorusMin) {
        effect_type = AUDIO_EFFECT_CHORUS_MIN;
    } else if(effect == AVSAudioEffectTypeChorusMax){
        effect_type = AUDIO_EFFECT_CHORUS_MAX;
    }else if(effect == AVSAudioEffectTypeReverbMin){
        effect_type = AUDIO_EFFECT_REVERB_MIN;
    }else if(effect == AVSAudioEffectTypeReverbMed){
        effect_type = AUDIO_EFFECT_REVERB_MID;
    }else if(effect == AVSAudioEffectTypeReverbMax){
        effect_type = AUDIO_EFFECT_REVERB_MAX;
    }else if(effect == AVSAudioEffectTypePitchupMin){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePitchupMed){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePitchupMax){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypePitchupInsane){
        effect_type = AUDIO_EFFECT_PITCH_UP_SHIFT_INSANE;
    }else if(effect == AVSAudioEffectTypePitchdownMin){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePitchdownMed){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePitchdownMax){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypePitchdownInsane){
        effect_type = AUDIO_EFFECT_PITCH_DOWN_SHIFT_INSANE;
    }else if(effect == AVSAudioEffectTypePaceupMin){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePaceupMed){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePaceupMax){
        effect_type = AUDIO_EFFECT_PACE_UP_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypePacedownMin){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MIN;
    }else if(effect == AVSAudioEffectTypePacedownMed){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MED;
    }else if(effect == AVSAudioEffectTypePacedownMax){
        effect_type = AUDIO_EFFECT_PACE_DOWN_SHIFT_MAX;
    }else if(effect == AVSAudioEffectTypeReverse){
        effect_type = AUDIO_EFFECT_REVERSE;
    }else if(effect == AVSAudioEffectTypeVocoderMed){
        effect_type = AUDIO_EFFECT_VOCODER_MED;
    }else if(effect == AVSAudioEffectTypeAutoTuneMin){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MIN;
    }else if(effect == AVSAudioEffectTypeAutoTuneMed){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MED;
    }else if(effect == AVSAudioEffectTypeAutoTuneMax){
        effect_type = AUDIO_EFFECT_AUTO_TUNE_MAX;
    }else if(effect == AVSAudioEffectTypePitchUpDownMin){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MIN;
    }else if(effect == AVSAudioEffectTypePitchUpDownMed){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MED;
    }else if(effect == AVSAudioEffectTypePitchUpDownMax){
        effect_type = AUDIO_EFFECT_PITCH_UP_DOWN_MAX;
    }else if(effect == AVSAudioEffectTypeNone){
        effect_type = AUDIO_EFFECT_NONE;
    }
    
    //    FLOWMGR_MARSHAL_RET(fmw.tid, ret, flowmgr_set_audio_effect, self.flowManager, effect_type);
    
    return ret;
}

@end


static void avs_flow_manager_send_notification(NSString *name, id object)
{
	dispatch_async(dispatch_get_main_queue(),^{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];

		NSNotification *notification = [NSNotification notificationWithName: name
						object: object];

		[center postNotification: notification];
	});
}

static void audio_state_change_h(enum flowmgr_audio_receive_state state,
                                 void *arg)
{
	(void)arg;
    
	AVSFlowManagerAudioReceiveState st;
    
	switch(state) {
		case FLOWMGR_AUDIO_INTERRUPTION_STOPPED:
			st = FLOWMANAGER_AUDIO_INTERRUPTION_STOPPED;
			break;
            
		case FLOWMGR_AUDIO_INTERRUPTION_STARTED:
			st = FLOWMANAGER_AUDIO_INTERRUPTION_STARTED;
			break;
	}
    
	AVSAudioStateChangeInfo *info = [[AVSAudioStateChangeInfo alloc]
										initWithState:st];
	avs_flow_manager_send_notification(FlowManagerAudioReceiveStateNotification, info);
}

static void video_size_h(int width, int height, const char *userid, void *arg)
{
	/* Send notification about video size change here ... */
}


static int render_frame_h(struct avs_vidframe * frame, const char *userid, void *arg)
{
	BOOL sizeChanged;
	NSString *uid = [NSString stringWithUTF8String: userid];

	sizeChanged = [[AVSFlowManager getInstance] renderFrame:frame forUser:uid];

	return sizeChanged ? ERANGE : 0;
}

