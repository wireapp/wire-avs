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
//
//  FlowManager.m
//  zcall-ios
//

#include <pthread.h>

#include "TargetConditionals.h"
#if TARGET_OS_IPHONE
#import <MobileCoreServices/MobileCoreServices.h>
#else
#import <CoreServices/CoreServices.h>
#endif

#include <dispatch/dispatch.h>

#include <re/re.h>
#include <avs.h>

#import "AVSMediaManager.h"

#import "AVSFlowManager.h"


#define DISPATCH_Q \
	dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)

static struct {
	bool initialized;
	pthread_t tid;
	enum log_level log_level;
	int err;
} fmw = {
	.initialized = false,
	.tid = NULL,
	.log_level = LOG_LEVEL_DEBUG,
	.err = 0,
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

#if HAVE_VIDEO

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

static void create_preview_handler(void *arg);
static void release_preview_handler(void *view, void *arg);
static void create_view_handler(const char *convid, const char* partid,
				void *arg);
static void release_view_handler(const char *convid, const char *partid,
				 void *view, void *arg);
#endif

@interface AVSFlowManager() <AVSMediaManagerObserver>

- (void)mediaCategoryChanged:(NSString *)convId category:(enum AVSFlowManagerCategory)mcat;

- (void)playbackRouteDidChangeInMediaManager:(AVSPlaybackRoute)play_back_route;

- (void)applicationWillResignActive:(NSNotification *)notification;
- (void)applicationDidBecomeActive:(NSNotification *)notification;

@property (nonatomic, assign) struct flowmgr *flowManager;

@property (nonatomic, weak) id<AVSFlowManagerDelegate> delegate;

@property (nonatomic, strong) NSMutableDictionary *collection;

@end

AVSFlowManager *g_Fm = nil;

static NSString *avsString(const char *s)
{
	return s ? [NSString stringWithUTF8String:s] : nil;
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


static void log_handler(uint32_t lve, const char *msg)
{	
	NSLog(@"%s", msg);

	if (g_Fm != nil && g_Fm.delegate != nil) {
		if ([g_Fm.delegate respondsToSelector:@selector(logMessage:)])
			[g_Fm.delegate logMessage:avsString(msg)];
	}
}


static struct log log_def = {
	.h = log_handler
};


static void *avs_thread(void *arg)
{
	int err;

	info("avs_thread: starting...\n");

	err = libre_init();
	if (err) {
		warning("flowmgr_thread: libre_init failed (%m)\n", err);
		return NULL;
	}

	log_enable_stderr(false);
	log_set_min_level(fmw.log_level);
	log_register_handler(&log_def);

	NSLog(@"Calling avs_init");
	
	err = avs_init(0);
	if (err) {
		warning("flowmgr_thread: avs_init failed (%m)\n", err);
		return NULL;
	}

#define LOG_URL "https://z-call-logs.s3-eu-west-1.amazonaws.com"
	err = flowmgr_init("voe", LOG_URL);
	fmw.initialized = err == 0;
	fmw.err = err;
	if (err) {
		error("avs_thread: failed to init flowmgr\n");
		goto out;
	}

	info("avs_thread: enable ICE dualstack\n");
	flowmgr_enable_dualstack(true);

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


static void media_estab_handler(const char *convid, bool estab, void *arg)
{
	AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;

	debug("AVSFlowManager::media_estab_handler convid=%s estab=%d\n",
	      convid, estab);

	dispatch_async(DISPATCH_Q, ^{
		[fm mediaEstablishedInConversation:avsString(convid)];
	});
}


static void mcat_handler(const char *convid, enum flowmgr_mcat cat, void *arg)
{
	debug("AVSFlowManager::mcat_handler cat=%d\n", cat);
    
	AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;
    
	bool hasActive;
	bool hasMedia;
	int err;

	err = flowmgr_has_active(fm.flowManager, &hasActive);
	err |= flowmgr_has_media(fm.flowManager, convid, &hasMedia);
	if (err) {
		warning("AVSFlowManager::mcat_handler media query failed\n");
	}
	
	dispatch_async(DISPATCH_Q, ^{
		if ( err ) {
			[fm.delegate setFlowManagerActivityState:AVSFlowActivityStateInvalid];
		}
		else {
			if ( hasActive ) {
				[fm.delegate setFlowManagerActivityState:AVSFlowActivityStateCallActive];
			}
			else {
				[fm.delegate setFlowManagerActivityState:AVSFlowActivityStateNoActivity];
			}
		}

		[fm updateModeInConversation:avsString(convid) withCategory:(AVSFlowManagerCategory)cat];
	 });
}


static void volume_handler(const char *convid, const char *userid,
			   double input, double output, void *arg)
{
	AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;

	dispatch_async(DISPATCH_Q, ^{
			[fm updateVolumeForUser:avsString(userid) inVol:input outVol:output inConversation:avsString(convid)];
	});
}


static void conf_pos_handler(const char *convid, struct list *partl, void *arg)
{
	AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;
	NSMutableArray *arr;
	struct le *le;

	debug("conf_pos_handler: %d parts\n", (int)list_count(partl));
	
	arr = [NSMutableArray arrayWithCapacity:list_count(partl)];
	LIST_FOREACH(partl, le) {
		struct conf_part *cp = le->data;

		[arr addObject:avsString(cp->uid)];
	}

	dispatch_async(DISPATCH_Q, ^{
		debug("conf_pos_handler: dispatching conferenceParticipants\n");
			
		[fm conferenceParticipants:arr inConversation:avsString(convid)];
	});	
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

static void vm_play_status_handler(bool is_playing, unsigned int cur_time_ms, unsigned int file_length_ms, void *arg)
{
    AVSFlowManager *fm = (__bridge AVSFlowManager *)arg;
    
    if([fm.delegate respondsToSelector:@selector(vmStatushandler:current_time:length:)]){
        dispatch_async(DISPATCH_Q, ^{
            [fm.delegate vmStatushandler:is_playing current_time:cur_time_ms length:file_length_ms];
        });
    }
}

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


static inline enum flowmgr_ausrc convert_ausrc(AVSFlowManagerAudioSource ausrc)
{
	switch(ausrc) {
	case FLOMANAGER_AUDIO_SOURCE_INTMIC:
		return FLOWMGR_AUSRC_INTMIC;
		
	case FLOMANAGER_AUDIO_SOURCE_EXTMIC:
		return FLOWMGR_AUSRC_EXTMIC;
		
	case FLOMANAGER_AUDIO_SOURCE_HEADSET:
		return FLOWMGR_AUSRC_HEADSET;
		
	case FLOMANAGER_AUDIO_SOURCE_BT:
		return FLOWMGR_AUSRC_BT;

	case FLOMANAGER_AUDIO_SOURCE_LINEIN:
		return FLOWMGR_AUSRC_LINEIN;

	case FLOMANAGER_AUDIO_SOURCE_SPDIF:
		return FLOWMGR_AUSRC_SPDIF;
	}
}


static inline enum flowmgr_auplay convert_auplay(AVSFlowManagerAudioPlay auplay)
{
	switch(auplay) {
	case FLOWMANAGER_AUDIO_PLAY_EARPIECE:
		return FLOWMGR_AUPLAY_EARPIECE;

	case FLOWMANAGER_AUDIO_PLAY_SPEAKER:
		return FLOWMGR_AUPLAY_SPEAKER;

	case FLOWMANAGER_AUDIO_PLAY_HEADSET:
		return FLOWMGR_AUPLAY_HEADSET;

	case FLOWMANAGER_AUDIO_PLAY_BT:
		return FLOWMGR_AUPLAY_BT;

	case FLOWMANAGER_AUDIO_PLAY_LINEOUT:
		return FLOWMGR_AUPLAY_LINEOUT;

	case FLOWMANAGER_AUDIO_PLAY_SPDIF:
		return FLOWMGR_AUPLAY_SPDIF;
	}
}


@implementation AVSFlowManager


+ (void)setLogLevel:(AVSFlowManagerLogLevel)logLevel
{
	fmw.log_level = convert_logl(logLevel);
	if (fmw.initialized)
		log_set_min_level(fmw.log_level);
}


+ (NSComparator)conferenceComparator
{
	return conferenceComparator;
}


- (void)appendLogForConversation:(NSString *)convId message:(NSString *)msg
{
	const char *cid = [convId UTF8String];
	const char *cmsg = [msg UTF8String];

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_append_convlog,
			     self.flowManager, cid, cmsg);	
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

- (instancetype)init
{
	struct flowmgr *fm;
	int err;

	debug("AVSFlowManager::init");
	
	if (fmw.tid == NULL) {
		err = pthread_create(&fmw.tid, NULL, avs_thread, NULL);
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

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_media_handlers, fm,
			     mcat_handler, volume_handler,
			     (__bridge void *)(self));

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_media_estab_handler, fm,
			     media_estab_handler,
			     (__bridge void *)(self));

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_conf_pos_handler, fm,
			     conf_pos_handler, (__bridge void *)(self));
#if HAVE_VIDEO
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_video_handlers, fm,
			     create_preview_handler,
			     release_preview_handler,
			     create_view_handler,
			     release_view_handler,
			     (__bridge void *)(self));
#endif

	self = [super init];

	if ( self ) {
		self.flowManager = fm;
		self.delegate = nil;

		[AVSMediaManagerChangeNotification addObserver:self];
		_AVSFlowManagerInstance = self;

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

		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_start);
	}

	return self;
}


- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate mediaManager:(id)mediaManager
{
	debug("AVSFlowManager::initWithDelegate:%p mm=%p\n",
	      delegate, mediaManager);
	
	self = [self init];
    
	if ( self ) {
		self.delegate = delegate;
		_AVSFlowManagerInstance = self;
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
	const char **evs;
	int nevs;
	int i;

	evs = flowmgr_events(&nevs);
	
	for(i = 0; i < nevs; i++) {
		NSString *str = avsString(evs[i]);
        
		[eventNames addObject:str];
	}

	return eventNames;
}


- (void)processResponseWithStatus:(int)status
			   reason:(NSString *)reason
			mediaType:(NSString *)mtype
			  content:(NSData *)content
			  context:(void const *)ctx
{
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_resp,
			     self.flowManager,
			     status,
			     [reason UTF8String],
			     [mtype UTF8String],
			     (const char *)[content bytes],
			     (size_t)[content length],
			     (void *)ctx);
}


- (BOOL)processEventWithMediaType:(NSString *)mtype
	     content:(NSData *)content
{
	int err;
	bool handled = false;

	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_process_event,
			    &handled,
			    self.flowManager,
			    [mtype UTF8String],
			    (const char *)[content bytes],
			    (size_t)[content length]);

	return (BOOL)(err == 0 && handled);
}


- (BOOL)acquireFlows:(NSString *)convId
{
	const char *cid = [convId UTF8String];
	int err;

	debug("AVSFlowManager::acquireFlows: %s\n", cid);
    
	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_acquire_flows, self.flowManager, cid, NULL, NULL, NULL);
	// Use this when we want to use the networkQuality
	//netq_handler, (__bridge void *)(self));

	return err == 0;
}


- (void)releaseFlows:(NSString *)convId
{
	const char *cid = [convId UTF8String];
	
	debug("AVSFlowManager::releaseFlows: %s\n", cid);
	
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_release_flows, self.flowManager, cid);
}


- (void)setActive:(NSString *)convId active:(BOOL)active
{
	const char *cid = [convId UTF8String];

	debug("AVSFlowManager::setActive: %s active=%d\n", cid, active);
    
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_active,
			     self.flowManager, cid, active);
}


- (void)addUser:(NSString *)convId userId:(NSString *)userId
	   name:(NSString *)name
{
	const char *cid = [convId UTF8String];
	const char *uid = [userId UTF8String];
	const char *nm = [name UTF8String];

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_user_add,
			     self.flowManager, cid, uid, nm);
	
}


- (void)setSelfUser:(NSString *)userId
{
	const char *uid = [userId UTF8String];

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_self_userid,
			     self.flowManager, uid);
	
}


- (void)refreshAccessToken:(NSString *)token type:(NSString *)type
{
	const char *tok = [token UTF8String];
	const char *typ = [type UTF8String];

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_refresh_access_token,
			     self.flowManager, tok, typ);
}


- (void)mediaCategoryChanged:(NSString *)convId category:(AVSFlowManagerCategory)cat
{
	const char *cid = [convId UTF8String];

	debug("AVSFlowManager::mediaCategoryChanged: %s cat=%d\n", cid, cat);
    
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_mcat_changed, self.flowManager, cid, (enum flowmgr_mcat)cat);
}


- (BOOL)isMediaEstablishedInConversation:(NSString *)convId
{
	const char *cid = [convId UTF8String];
	int err;
	bool estab;
	
	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_has_media,
			    self.flowManager, cid, &estab);

	debug("AVSFlowManager::isMediaEstablished %s estab=%d\n", cid, estab);
    
	
	return (BOOL)estab;
}	


- (void)networkChanged
{
	info("AVSFlowManager::networkChanged\n");
	
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_network_changed,
			     self.flowManager);
}


- (int)ausrcChanged:(enum AVSFlowManagerAudioSource)audioSource
{
	enum flowmgr_ausrc ausrc = convert_ausrc(audioSource);
	int err;

	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_ausrc_changed, self.flowManager, ausrc);

	return err;
}


- (int)auplayChanged:(enum AVSFlowManagerAudioPlay)audioPlay
{
	enum flowmgr_auplay auplay = convert_auplay(audioPlay);
	int err;

	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_auplay_changed, self.flowManager, auplay);

	return err;
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

	err = flowmgr_sort_participants(&partl);
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
	const char *cid = [convId UTF8String];
	int err;
		
	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_interruption,
			    self.flowManager, cid, true);	
}


- (void)callInterruptionEndInConversation:(NSString *)convId
{
	const char *cid = [convId UTF8String];
	int err;
		
	FLOWMGR_MARSHAL_RET(fmw.tid, err, flowmgr_interruption,
			    self.flowManager, cid, false);	
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

- (void)playbackRouteDidChangeInMediaManager:(AVSPlaybackRoute)play_back_route
{
    AVSFlowManagerAudioPlay route = FLOWMANAGER_AUDIO_PLAY_SPEAKER;
    
    switch ( play_back_route ) {
        case AVSPlaybackRouteBuiltIn:
            route = FLOWMANAGER_AUDIO_PLAY_EARPIECE;
            break;
            
        case AVSPlaybackRouteHeadset:
            route = FLOWMANAGER_AUDIO_PLAY_HEADSET;
            break;
            
        case AVSPlaybackRouteSpeaker:
            route = FLOWMANAGER_AUDIO_PLAY_SPEAKER;
            break;
            
        default:
            break;
    }
    
    //if ( self.collection.count ) { // SSJ think this is the only place the collection is used maybe remove
        [self auplayChanged:route];
    //}
}

- (void)setEnableLogging:(BOOL)enable
{
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_enable_logging,
			     self.flowManager, (bool)enable);
}


- (void)setEnableMetrics:(BOOL)enable
{
	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_enable_metrics,
			     self.flowManager, (bool)enable);
}



- (void)setSessionId:(NSString *)sessId forConversation:(NSString *)convId
{
	const char *sid = [sessId UTF8String];
	const char *cid = [convId UTF8String];

	FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_sessid, self.flowManager, cid, sid);
}


- (void)applicationWillResignActive:(NSNotification *)notification
{
	dispatch_async(DISPATCH_Q, ^{
		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_background, self.flowManager,
			MEDIA_BG_STATE_ENTER);
	});
}

- (void)applicationDidBecomeActive:(NSNotification *)notification;
{
	dispatch_async(DISPATCH_Q, ^{
		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_background, self.flowManager,
			MEDIA_BG_STATE_EXIT);
	});
}

#if HAVE_VIDEO

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
			(enum flowmgr_video_send_state)state;

		debug("%s: conv=%s state=%d\n", __FUNCTION__, cid ? cid : "NULL", state);
		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_video_send_state, self.flowManager,
			cid, fmstate);
	});
}


- (void)setVideoPreview:(UIView *)view forConversation:(NSString *)convId
{
	dispatch_async(DISPATCH_Q, ^{
		const char *cid = convId ? [convId UTF8String] : NULL;
		void *v = (__bridge_retained void*)view;

		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_video_preview, 
			self.flowManager, cid, v);
	});
}


- (void)setVideoView:(UIView *)view forConversation:(NSString *)convId forParticipant:(NSString *)partId
{
	dispatch_async(DISPATCH_Q, ^{
		void *v = (__bridge_retained void*)view;
		const char *cid = convId ? [convId UTF8String] : NULL;
		const char *pid = partId ? [partId UTF8String] : NULL;

		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_video_view, self.flowManager,
			cid, pid, v);
	});
}


- (NSArray*)getVideoCaptureDevices
{
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
}


- (void)setVideoCaptureDevice:(NSString *)deviceId forConversation:(NSString *)convId
{
	dispatch_async(DISPATCH_Q, ^{
		const char *cid = convId ? [convId UTF8String] : NULL;
		const char *devid = deviceId ? [deviceId UTF8String] : NULL;

		FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_set_video_capture_device,
			self.flowManager, cid, devid);
	});
}

#else
- (BOOL)canSendVideoForConversation:(NSString *)convId { return NO; }
- (void)setVideoSendActive:(BOOL)active forConversation:(NSString *)convId{}
- (void)setVideoPreview:(void *)view forConversation:(NSString *)convId {}
- (void)setVideoView:(void *)view forConversation:(NSString *)convId forParticipant:(NSString *)participantId {}
- (NSArray*)getVideoCaptureDevices { return nil;}
- (void)setVideoCaptureDevice:(NSString *)deviceId forConversation:(NSString *)convId {}

#endif

- (void)vmStartRecord:(NSString *)fileName
{
    const char *file_name = [fileName UTF8String];

    FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_vm_start_record, self.flowManager, file_name);
}

- (void)vmStopRecord
{
    FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_vm_stop_record, self.flowManager);
}

- (int)vmGetLength:(NSString *)fileName
{
    const char *file_name = [fileName UTF8String];
    int length_ms;
    
    int ret = flowmgr_vm_get_length(self.flowManager, file_name, &length_ms);
    
    if(ret < 0){
        return ret;
    } else {
        return length_ms;
    }
}

- (void)vmStartPlay:(NSString *)fileName toStart:(int)startpos
{
    const char *file_name = [fileName UTF8String];
    
    FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_vm_start_play, self.flowManager,
                         file_name, startpos, vm_play_status_handler, (__bridge void *)(self));
}

- (void)vmStopPlay
{
    FLOWMGR_MARSHAL_VOID(fmw.tid, flowmgr_vm_stop_play, self.flowManager);
}

@end


#if HAVE_VIDEO
static void avs_flow_manager_send_notification(NSString *name, id object)
{
	dispatch_async(dispatch_get_main_queue(),^{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];

		NSNotification *notification = [NSNotification notificationWithName: name
						object: object];

		[center postNotification: notification];
	});
}

static void create_preview_handler(void *arg)
{
	(void)arg;

	avs_flow_manager_send_notification(FlowManagerCreatePreviewNotification, nil);
}

static void release_preview_handler(void *view, void *arg)
{
	(void)arg;
	id viewobj = view ? (__bridge_transfer id)(view) : nil;

	avs_flow_manager_send_notification(FlowManagerReleasePreviewNotification, viewobj);
}

static void create_view_handler(const char *convid, const char *partid,
				void *arg)
{
	NSString *partStr = partid ? [NSString stringWithUTF8String:partid] : nil;

	(void)arg;
	(void)convid;
	
	avs_flow_manager_send_notification(FlowManagerCreateViewNotification, partStr);
}

static void release_view_handler(const char *convid, const char *partid,
				 void *view, void *arg)
{
	id viewobj = view ? (__bridge_transfer id)(view) : nil;

	(void)convid;
	(void)partid;
	(void)arg;

	avs_flow_manager_send_notification(FlowManagerReleaseViewNotification, viewobj);
}
#endif

