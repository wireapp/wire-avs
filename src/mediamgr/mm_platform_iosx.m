

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include "re.h"
#include "avs.h"
#include "mm_platform.h"
#include "avs_mediamgr.h"
#include "avs_audio_io.h"
#include "../../iosx/include/AVSMedia.h"



struct {
	struct mm *mm;
	bool incall;
	bool interrupted;
	bool route_override;
	NSString *cat;
	bool active;
	bool hs_connected;
	bool bt_connected;
	bool recording;
#if !TARGET_OS_IPHONE
	enum mediamgr_auplay cur_route;
#endif
	struct tmr tmr_play;
	struct tmr tmr_rec;

} mm_ios = {
	.mm = NULL,
	.incall = false,
	.cat = NULL,
	.active = false,
	.interrupted = false,
	.hs_connected = false,
	.bt_connected = false,
	.recording = false,
#if !TARGET_OS_IPHONE
	.cur_route = MEDIAMGR_AUPLAY_EARPIECE,
#endif
};

static NSString *DEFAULT_CATEGORY;

static NSArray *g_bt_routes;

static void set_category(NSString *cat, bool speaker);
static bool set_category_sync(NSString *cat, bool speaker);
#if !TARGET_IPHONE_SIMULATOR
static void default_category(bool sync);
#endif


static bool set_active_sync(bool active)
{
	NSError *err = nil;
	BOOL success;	
	AVAudioSessionSetActiveOptions options = 
		AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation;

	info("mm_ios: set_active_sync: active=%d\n", active);
	
	success = [[AVAudioSession sharedInstance]
				setActive:active ? YES : NO
			      withOptions:options
				    error:&err];

	if (!success) {
		error("mm_platform_ios: could not set active: %s "
		      "err=%ld %s\n",
		      active ? "YES" : "NO", (long)err.code,
		      [err.localizedDescription UTF8String]);
	}

	mm_ios.active = active;

	return success ? true : false;
}


static void set_active(bool active)
{
	info("mm_platform_ios: set_active: active=%s\n",
	     active ? "yes" : "no");

	dispatch_sync(dispatch_get_main_queue(), ^{
		set_active_sync(active);
	});
}


// TODO: remove the need for the playing tracker
@interface PlayingTracker : NSObject <AVSMediaDelegate>
{
	NSMutableDictionary *_playingDict;
	NSLock *_lock;
};

- (instancetype) init;

- (BOOL)canStartPlayingMedia:(id<AVSMedia>)media;
- (void)didStartPlayingMedia:(id<AVSMedia>)media;
- (void)didPausePlayingMedia:(id<AVSMedia>)media;
- (void)didResumePlayingMedia:(id<AVSMedia>)media;
- (void)didFinishPlayingMedia:(id<AVSMedia>)media;

- (BOOL)isPlaying:(id<AVSMedia>)media;

@end

@implementation PlayingTracker : NSObject

- (instancetype) init
{
	_playingDict = [[NSMutableDictionary alloc] init];
	_lock = [[NSLock alloc] init];

	return self;
}

- (BOOL)canStartPlayingMedia:(id<AVSMedia>)media
{
	struct sound *snd = NULL;
	NSString *cat = [AVAudioSession sharedInstance].category;

	if ([media respondsToSelector:@selector(sound)]) {
		snd = (struct sound*)[media sound];
	}

	if (!snd)
		return NO;

	if (snd->is_call_media) {
		if (cat != AVAudioSessionCategoryPlayAndRecord)
			return NO;
	}

	return YES;
}

- (void)didStartPlayingMedia:(id<AVSMedia>)media
{
#if !TARGET_IPHONE_SIMULATOR
	NSString *cname = NSStringFromClass([media class]);

	info("mm_platform_ios: didStartPlaying: class=%s %s\n",
	     [cname UTF8String], mm_ios.recording ? "recording" : "");

	if (![cname isEqualToString:@"AVSSound"] && !mm_ios.recording) {
		mediamgr_override_speaker_mm(mm_ios.mm, true);
		set_category_sync(AVAudioSessionCategoryPlayback, true);
		set_active_sync(true);
	}
#endif
	
	[_lock lock];
	[_playingDict setObject:media forKey:media.name];
	[_lock unlock];
}

- (void)didPausePlayingMedia:(id<AVSMedia>)media
{
	NSString *cname = NSStringFromClass([media class]);

	info("mm_platform_ios: didPausePlaying: class=%s\n",
	     [cname UTF8String]);

	if (![cname isEqualToString:@"AVSSound"] && !mm_ios.recording) {
		mediamgr_override_speaker_mm(mm_ios.mm, false);
		mm_platform_enable_earpiece();
	}

	[_lock lock];
	[_playingDict removeObjectForKey:media.name];
	[_lock unlock];
}

- (void)didResumePlayingMedia:(id<AVSMedia>)media
{
	NSString *cname = NSStringFromClass([media class]);

	info("mm_platform_ios: didResumePlaying: class=%s\n",
	     [cname UTF8String]);

	if (![cname isEqualToString:@"AVSSound"] && !mm_ios.recording) {
		mediamgr_override_speaker_mm(mm_ios.mm, true);
		set_category_sync(AVAudioSessionCategoryPlayback, true);
		set_active_sync(true);
	}
	
	[_lock lock];
	[_playingDict setObject:media forKey:media.name];
	[_lock unlock];
}

- (void)didFinishPlayingMedia:(id<AVSMedia>)media
{
	NSUInteger n;
	
	info("mm_platform_ios: finished playing media: %s\n",
	     [media.name UTF8String]);

#if !TARGET_IPHONE_SIMULATOR
	NSString *cname = NSStringFromClass([media class]);

	info("mm_platform_ios: didFinishPlaying: class=%s\n",
	     [cname UTF8String]);
	
	if (![cname isEqualToString:@"AVSSound"] && !mm_ios.incall
	    && !mm_ios.recording) {
		mediamgr_override_speaker_mm(mm_ios.mm, false);
		default_category(true);
	}
		
#endif
	
	[_lock lock];

	BOOL playing = ([_playingDict objectForKey:media.name] != nil);
	if (playing)
		[_playingDict removeObjectForKey:media.name];
	n = [_playingDict count];
	
	[_lock unlock];

	info("mm_ios: still playing: %u\n", n);
	
	if (!mm_ios.incall && !mm_ios.recording && n == 0)
		set_active_sync(false);
}

- (BOOL)isPlaying:(id<AVSMedia>)media
{
	[_lock lock];
	BOOL playing = ([_playingDict objectForKey:media.name] != nil);
	[_lock unlock];

	return  playing;
}

@end

static PlayingTracker *g_playingTracker = NULL;

#if TARGET_OS_IPHONE

static const char *cat_name(NSString *cat)
{
	if (cat == AVAudioSessionCategoryAmbient)
		return "Ambient";
	else if (cat == AVAudioSessionCategorySoloAmbient)
		return "SoloAmbient";
	else if (cat == AVAudioSessionCategoryPlayback)
		return "Playback";
	else if (cat == AVAudioSessionCategoryRecord)
		return "Record";
	else if (cat == AVAudioSessionCategoryPlayAndRecord)
		return "PlayAndRecord";
	else if (cat == AVAudioSessionCategoryMultiRoute)
		return "MultiRoute";
	else
		return "???";
}
#endif

static bool set_category_sync(NSString *cat, bool speaker)
{
	AVAudioSession *sess;
	AVAudioSessionCategoryOptions options = 0;
	BOOL success;
	NSError *err;

	info("mm_platform_ios: set_category_sync: %s\n", cat_name(cat));

	sess = [AVAudioSession sharedInstance];
	
	if (cat == AVAudioSessionCategoryPlayAndRecord) {
		if (mm_ios.recording) {
			options |=
				AVAudioSessionCategoryOptionDefaultToSpeaker;
		}
		else if (speaker) {
			[sess overrideOutputAudioPort:
				      AVAudioSessionPortOverrideSpeaker
						error:nil];
		}
		else {
			[sess overrideOutputAudioPort:
				      AVAudioSessionPortOverrideNone
						error:nil];
		}
		options |= AVAudioSessionCategoryOptionAllowBluetooth;
		options |= AVAudioSessionCategoryOptionAllowBluetoothA2DP;
	}
	
	success = [sess setCategory:cat
			withOptions:options
			      error:&err];
	if (!success) {
		error("mm_platform_ios: set_category: "
		      "failed to set category: %s "
		      "err=%ld %s\n",
		      cat_name(cat),
		      (long)err.code,
		      [err.localizedDescription UTF8String]);
	}

	return success ? true : false;
}

static void set_category(NSString *cat, bool speaker)
{
	info("mm_platform_ios: set_category: %s\n", cat_name(cat));

	dispatch_sync(dispatch_get_main_queue(), ^{
			set_category_sync(cat, speaker);
	});
}

static void leave_call(void)
{
	mm_ios.incall = false;
	mediamgr_sys_left_call(mm_ios.mm);
}

static bool has_bluetooth(void)
{
	AVAudioSession *sess = [AVAudioSession sharedInstance]; 
	NSArray *routes = [sess availableInputs];

	for (AVAudioSessionPortDescription *route in routes ) {
		if ([g_bt_routes containsObject:route.portType]) {
			return true;
		}
	}
	return false;
}


static bool has_headset(void)
{
	AVAudioSession *sess = [AVAudioSession sharedInstance]; 
	NSArray *outputs = [sess currentRoute].outputs;

	for (AVAudioSessionPortDescription *route in outputs ) {
		if ([route.portType
			isEqualToString:AVAudioSessionPortHeadphones]) {
			return true;
		}
	}
	return false;
}

static void update_device_status(struct mm *mm)
{
	bool bt_available = false;
	bool hs_available = false;
	bool updated = false;

	bt_available = has_bluetooth();
	hs_available = has_headset();

	info("mm_platform_ios: device_status: bt=%d(%d) hs=%d(%d)\n",
	     bt_available, mm_ios.bt_connected,
	     hs_available, mm_ios.hs_connected);

	if (bt_available && !mm_ios.bt_connected){
		mm_ios.bt_connected = true;
		mediamgr_bt_device_connected(mm, true);
		updated = true;
	}
	if (!bt_available && mm_ios.bt_connected){
		mm_ios.bt_connected = false;
		mediamgr_bt_device_connected(mm, false);
		updated = true;
	}
	if (hs_available && !mm_ios.hs_connected){
		mm_ios.hs_connected = true;
		mediamgr_headset_connected(mm, true);
		updated = true;
	}
	if (!hs_available && mm_ios.hs_connected){
		mm_ios.hs_connected = false;
		mediamgr_headset_connected(mm, false);
		updated = true;
	}

	if (!updated)
		mediamgr_device_changed(mm_ios.mm);		
}


#if TARGET_OS_IPHONE

static const char *reason_name(NSInteger reason)
{
	switch (reason) {
	case AVAudioSessionRouteChangeReasonUnknown:
		return "Unknown";
                
	case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
		return "NewDeviceAvailable";

	case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
		return "OldDeviceUnavailable";

	case AVAudioSessionRouteChangeReasonCategoryChange:
		return "CategoryChange";
		
	case AVAudioSessionRouteChangeReasonOverride:
		return "Override";

	case AVAudioSessionRouteChangeReasonWakeFromSleep:
		return "WakeFromSleep";
				
	case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory:
                return "NoSuitableRouteForCategory";
                
	case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
		return "RouteConfigurationChange";
                
	default:
		return "???";
	}	
}

static void interrupt_action(bool interrupted)
{
	if (interrupted) {
		mediamgr_set_call_state_mm(mm_ios.mm, MEDIAMGR_STATE_HOLD);
		leave_call();
		mm_ios.interrupted = true;
		mm_ios.active = false;
	}
	else {
		mm_ios.interrupted = false;
		if (mm_ios.incall) {
			set_category_sync(AVAudioSessionCategoryPlayAndRecord,
					  mediamgr_get_speaker(mm_ios.mm));
			set_active_sync(true);
			mediamgr_set_call_state_mm(mm_ios.mm,
						   MEDIAMGR_STATE_RESUME);
		}
	}
}

static void handle_audio_interruption(NSNotification *notification)
{

	NSDictionary *dict;
	NSInteger type;

	dict = notification.userInfo;
	type = [[dict valueForKey:AVAudioSessionInterruptionTypeKey]
		       integerValue];
				
	switch(type) {
	case AVAudioSessionInterruptionTypeBegan:
		info("mm_platform_ios: interruption began\n");
		interrupt_action(true);
		break;

	case AVAudioSessionInterruptionTypeEnded:
		info("mm_platform_ios: interruption ended\n");
		interrupt_action(false);
		break;
					     
	default:
		info("mm_platform_ios: interruption unknown\n");
		break;
	}
}

static void handle_audio_notification(NSNotification *notification)
{
	NSDictionary *dict;
	enum mediamgr_auplay route;
	NSInteger reason;
	AVAudioSession* sess;
	NSString *cat;
	NSArray *input_routes;
	BOOL success;
	NSError *err = nil;

	if (!mm_ios.mm)
		return;

	dict = notification.userInfo;	
	reason = [[dict valueForKey:AVAudioSessionRouteChangeReasonKey]
			 integerValue];
        route = mm_platform_get_route();
	sess = [AVAudioSession sharedInstance];
	cat = [sess category];

	info("mediamgr: audio_notification: reason=%s category=%s(was=%s) "
	     "route=%s route_override=%d rec=%d\n",
	     reason_name(reason), cat_name(cat), cat_name(mm_ios.cat),
	     mediamgr_route_name(route),
	     mm_ios.route_override,
	     mm_ios.recording);

	if (route == MEDIAMGR_AUPLAY_EARPIECE
	    && reason != AVAudioSessionRouteChangeReasonOverride) {
		if (has_bluetooth()) {
			set_category_sync(cat, false);
			mm_platform_enable_bt_sco();
		}
		else if (has_headset())
			mm_platform_enable_headset();
	}

	if (reason != AVAudioSessionRouteChangeReasonRouteConfigurationChange) {
		if (mm_ios.route_override) {
			mediamgr_override_speaker_mm(mm_ios.mm,
					  route == MEDIAMGR_AUPLAY_SPEAKER);
			mm_ios.route_override = false;
		}
		if (reason == AVAudioSessionRouteChangeReasonOverride) {
			mm_ios.route_override = true;
			mediamgr_override_speaker_mm(mm_ios.mm,
					  route == MEDIAMGR_AUPLAY_SPEAKER);
		}

		update_device_status(mm_ios.mm);
	}
	else if (mediamgr_should_reset(mm_ios.mm)) {
		mediamgr_audio_reset_mm(mm_ios.mm);
	}

	input_routes = sess.availableInputs;
	
	if (mm_ios.interrupted) {
		mm_ios.interrupted = false;
		if (mm_ios.incall) {
			/* If we are able to activate,
			 * it means that interruption
			 * has actually ended,
			 * without us being notified,
			 * so transition into RESUME
			 */
			if (set_active_sync(true)) {
				mediamgr_set_call_state_mm(mm_ios.mm,
						   MEDIAMGR_STATE_RESUME);
			}
		}
	}

	debug("mm_platform_ios: sample rate: %dhz\n", (int)sess.sampleRate);
	debug("mm_platform_ios: IOBufferDuration: %dms\n",
	      (int)(sess.IOBufferDuration * 1000.0));
	debug("mm_platform_ios: output channels: %ld\n",
	      sess.outputNumberOfChannels);
	debug("mm_platform_ios: input channels: %ld\n",
	      sess.inputNumberOfChannels);
		
	for (AVAudioSessionPortDescription *ir in input_routes) {
		debug("mediamgr:\tname=%s type=%s\n",
		      [[ir portName] UTF8String],
		      [[ir portType] UTF8String]);
	}

	debug("mediamgr: output latency: %f\n", sess.outputLatency);
	debug("mediamgr: input latency: %f\n", sess.inputLatency);

	switch (reason) {
	case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
		/* Previously this was necessary to reset audio device
		 * after Callkit took over e.g. because of an incoming GSM call
		 * but it seems to cause issues in some scenarios when the app
		 * is stuck in this state ans cannot accept calls.
		 */
#if 0
		if (mm_ios.incall) {
			if (sess.inputNumberOfChannels == 1)
				mediamgr_hold_and_resume(mm_ios.mm);
			break;
		}
#endif
		if (mm_ios.cat == cat)
			break;
		
		/* Deliberate fallthrough,
		 * in case we are in different category,
		 * treat the same as a category change
		 */

	case AVAudioSessionRouteChangeReasonOverride:
	case AVAudioSessionRouteChangeReasonCategoryChange:
		info("mm_platform_ios: cat change: in:%s new=%s rec=%d\n",
		     cat_name(mm_ios.cat), cat_name(cat), mm_ios.recording);
		
		if (mm_ios.cat == cat && !mm_ios.incall)
			break;

		if (mm_ios.recording)
			break;

		if (cat == AVAudioSessionCategoryPlayAndRecord) {
			success = [sess setMode:AVAudioSessionModeVoiceChat
					  error:&err];
			if (!success) {
				error("mm_platform_ios: incall_category: "
				      "could not set VoiceChat mode: %ld\n",
				      (long)err.code);
			}
				
			mediamgr_sys_entered_call(mm_ios.mm);
		}
		else if (cat == AVAudioSessionCategorySoloAmbient) {
			if (mm_ios.cat == AVAudioSessionCategoryPlayAndRecord)
				leave_call();
			else
				mediamgr_sys_incoming(mm_ios.mm);
		}
		else {
			leave_call();
		}
		mm_ios.cat = cat;
		break;
	}
}

#if !TARGET_IPHONE_SIMULATOR
static void default_category(bool sync)
{
	info("mm_platform_ios: default_category: %s recording: %s\n",
	     cat_name(DEFAULT_CATEGORY), mm_ios.recording ? "yes" : "no");
		
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	
	if (sess.category == DEFAULT_CATEGORY) {
		info("mm_platform_ios: already in: %s\n",
		     cat_name(DEFAULT_CATEGORY));
	}

	if (sync) {
		set_category_sync(DEFAULT_CATEGORY, true);
		set_active_sync(false);
	}
	else {
		set_category(DEFAULT_CATEGORY, true);
		set_active(false);
	}
}

static void incoming_category()
{
	info("mm_platform_ios: incoming_category\n");
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	
	if (sess.category == AVAudioSessionCategorySoloAmbient)
		info("mm_platform_ios: incoming: already in SoloAmbient\n");
	else 
		set_category(AVAudioSessionCategorySoloAmbient, true);
	set_active(true);
	
	mediamgr_sys_incoming(mm_ios.mm);
}

static void incall_category(void)
{
	set_category(AVAudioSessionCategoryPlayAndRecord,
		     mediamgr_get_speaker(mm_ios.mm));
	set_active(true);
}
#endif /* #if !TARGET_IPHONE_SIMULATOR */

#endif

int mm_platform_init(struct mm *mm, struct dict *sounds)
{
	info("mm_platform_ios: init for mm=%p\n", mm);
	NSLog(@"mm_platform_ios: init for mm=%p\n", mm);
	
	mm_ios.mm = mm;
	tmr_init(&mm_ios.tmr_play);
	g_playingTracker = [[PlayingTracker alloc] init];

	g_bt_routes = @[AVAudioSessionPortBluetoothA2DP,
			AVAudioSessionPortBluetoothLE,
			AVAudioSessionPortBluetoothHFP];

	DEFAULT_CATEGORY = AVAudioSessionCategoryAmbient;

#if TARGET_OS_IPHONE
	NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];

	[nc addObserverForName:AVAudioSessionInterruptionNotification
			object:nil
			 queue:[NSOperationQueue mainQueue]
		    usingBlock: ^(NSNotification *notification) {
			handle_audio_interruption(notification);
		}];

	
	[nc addObserverForName:AVAudioSessionRouteChangeNotification
			object:nil
			 queue:[NSOperationQueue mainQueue]
		    usingBlock: ^(NSNotification *notification) {

			handle_audio_notification(notification);
		    }];
	
	[nc addObserverForName:AVAudioSessionMediaServicesWereLostNotification
			 object:nil
			  queue:[NSOperationQueue mainQueue]
		     usingBlock: ^(NSNotification *notification) {

			info("mediamgr: AVAudioSessionMediaServices"
			     "WereLostNotification received \n");

		     }];
	
	[nc addObserverForName:AVAudioSessionMediaServicesWereResetNotification
			object:nil
			 queue:[NSOperationQueue mainQueue]
		    usingBlock: ^(NSNotification *notification) {

			/* According to Apple documentation we MUST
			 * re-start all audio players/recordes, and also 
			 * re-init session
			 */
			info("mediamgr: AVAudioSessionMediaServices"
			     "WereResetNotification received\n");

			mm_ios.route_override = false;
			interrupt_action(true);
			mediamgr_reset_sounds(mm_ios.mm);
			interrupt_action(false);
			mediamgr_audio_reset_mm(mm_ios.mm);
		}];

	[nc addObserverForName:UIApplicationDidBecomeActiveNotification
			object:nil
			 queue:[NSOperationQueue mainQueue]
		    usingBlock: ^(NSNotification *notification) {
			
			info("mm_platform_ios: didBecomeActive\n");

			if (mm_ios.incall)
				mediamgr_audio_reset_mm(mm_ios.mm);
		}];
	    
#endif
	set_category(AVAudioSessionCategoryAmbient, true);
	mm_ios.cat = [AVAudioSession sharedInstance].category;
	set_active_sync(false);

	update_device_status(mm_ios.mm);
    
	return 0;
}


int mm_platform_free(struct mm *mm)
{
	mm_ios.mm = NULL;

	set_active(false);

	return 0;
}

void mm_platform_reset_sound(struct sound *snd)
{
	NSString *cname;
	id<AVSMedia> media;
	
	if (!snd || !snd->arg)
		return;

	media = snd->arg;
	cname = NSStringFromClass([media class]);

	info("mm_platform_ios: reset_sound: class=%s\n", [cname UTF8String]);

	if ([cname isEqualToString:@"AVSSound"])	
		[media reset];
}

static void play_handler(void *arg)
{
	struct sound *snd = arg;
	id<AVSMedia> media = snd->arg;
	int n = 10;

	[media play];
	if (snd->sync) {
		while(mm_platform_is_sound_playing(snd) && n-- > 0) {
			usleep(200000);
		}
	}
	
}

void mm_platform_play_sound(struct sound *snd, bool sync, bool delayed)
{

	info("mm_platform_ios: play_sound: %s %s\n",
	     snd->path, snd->is_call_media ? "(call)" : "");

	if (mm_ios.recording)
		return;

	if (!snd->arg)
		return;

	if (!mm_ios.active && !mm_ios.incall) {
		if (snd->mixing)
			set_category_sync(AVAudioSessionCategoryAmbient, true);
		else {
			set_category_sync(AVAudioSessionCategorySoloAmbient,
					  true);
		}
		
		set_active_sync(true);
	}

	snd->sync = sync;
	if (delayed)
		tmr_start(&mm_ios.tmr_play, 1000, play_handler, snd);
	else
		play_handler(snd);
}

void mm_platform_pause_sound(struct sound *snd)
{

	if (snd->arg == NULL)
		return;
	else {	
		id<AVSMedia> media = snd->arg;

		info("mm_platform_ios: pause_sound: %s %s\n",
		     snd->path, snd->is_call_media ? "(call)" : "");

		[media pause];
	}
}

void mm_platform_resume_sound(struct sound *snd)
{
	if (snd->arg == NULL)
		return;
	else {
		id<AVSMedia> media = snd->arg;

		info("mm_platform_ios: resume_sound: %s %s\n",
		     snd->path, snd->is_call_media ? "(call)" : "");

		[media resume];
	}
}

void mm_platform_stop_sound(struct sound *snd)
{
	info("mm_platform_ios: stop_sound: %s %s\n",
	     snd->path, snd->is_call_media ? "(call)" : "");

	tmr_cancel(&mm_ios.tmr_play);
	if (snd->arg != NULL) {
		id<AVSMedia> media = snd->arg;
		
		[media stop];
	}
}

bool mm_platform_is_sound_playing(struct sound *snd)
{
	if (snd->arg == NULL)
		return false;
	else {
		id<AVSMedia> media = snd->arg;
		return [g_playingTracker isPlaying:media];
	}
}

int mm_platform_enable_speaker(void)
{
	info("mm_platform_ios: enable_speaker\n");	
#if TARGET_OS_IPHONE
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	
        [sess overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker
				error:nil];
#else
	mm_ios.cur_route = MEDIAMGR_AUPLAY_SPEAKER;
#endif
	return 0;
}

int mm_platform_enable_bt_sco(void)
{
	info("mm_platform_ios: enable_bt_sco\n");	
#if TARGET_OS_IPHONE
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	NSArray *inputRoutes = [sess availableInputs];

	for (AVAudioSessionPortDescription *route in inputRoutes) {
		if ([g_bt_routes containsObject:route.portType]) {
			[sess setPreferredInput:route error:nil];
		}
	}
#else
	mm_ios.cur_route = MEDIAMGR_AUPLAY_BT;
#endif
	return 0;
}

int mm_platform_enable_earpiece(void)
{
	info("mm_platform_ios: enable_earpiece\n");

#if TARGET_OS_IPHONE
	AVAudioSession *sess = [AVAudioSession sharedInstance];
        [sess overrideOutputAudioPort:AVAudioSessionPortOverrideNone
				error:nil];
#else
	mm_ios.cur_route = MEDIAMGR_AUPLAY_EARPIECE;
#endif
	return 0;
}

int mm_platform_enable_headset(void)
{
	info("mm_platform_ios: enable_headset\n");
	
#if TARGET_OS_IPHONE
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	
        [sess overrideOutputAudioPort:AVAudioSessionPortOverrideNone
				error:nil];
#else
	mm_ios.cur_route = MEDIAMGR_AUPLAY_HEADSET;
#endif
	return 0;
}

static enum mediamgr_auplay port2route(NSString *pt)
{
	if ([pt isEqualToString:AVAudioSessionPortBuiltInReceiver])
		return MEDIAMGR_AUPLAY_EARPIECE;
	else if ([pt isEqualToString:AVAudioSessionPortHeadphones])
		return MEDIAMGR_AUPLAY_HEADSET;
	else if ([pt isEqualToString:AVAudioSessionPortBuiltInSpeaker])
		return MEDIAMGR_AUPLAY_SPEAKER;
	else if ([g_bt_routes containsObject:pt])
		return MEDIAMGR_AUPLAY_BT;
	else
		return MEDIAMGR_AUPLAY_UNKNOWN;
}

enum mediamgr_auplay mm_platform_get_route(void)
{
#if TARGET_OS_IPHONE
	enum mediamgr_auplay route = MEDIAMGR_AUPLAY_EARPIECE;
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	NSArray *outputs = nil;

	if (sess == nil)
		return route;

	outputs = [[sess currentRoute] outputs];

	for (AVAudioSessionPortDescription *output in outputs) {
		NSString *pt = output.portType;
		route = port2route(pt);
		info("mm_platform_ios: get_route: %s(%s)\n",
		     [pt UTF8String], mediamgr_route_name(route));
		//if (route != MEDIAMGR_AUPLAY_UNKNOWN)
		//	return route;
	}

	return route;
#else
	return mm_ios.cur_route;
#endif
}


void mm_platform_incoming(void)
{
#if TARGET_IPHONE_SIMULATOR
	mediamgr_sys_incoming(mm_ios.mm);
#elif TARGET_OS_IPHONE
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	NSString *cat = [sess category];

	mm_ios.incall = true;

	if (mm_ios.active || cat == AVAudioSessionCategorySoloAmbient)
		mediamgr_sys_incoming(mm_ios.mm);
	else
		incoming_category();
#endif
}


void mm_platform_enter_call(void)
{
#if TARGET_IPHONE_SIMULATOR
	mediamgr_sys_entered_call(mm_ios.mm);
#elif TARGET_OS_IPHONE	
	AVAudioSession *sess = [AVAudioSession sharedInstance];
 
	info("mm_platform_ios: enter_call: incall=%s\n",
	     mm_ios.incall ? "yes" : "no");

	if (mm_ios.interrupted) {
		set_active(true);
		mm_ios.interrupted = false;
	}
	
	mm_ios.incall = true;
	if ([sess category] == AVAudioSessionCategoryPlayAndRecord)
		mediamgr_sys_entered_call(mm_ios.mm);
	else 
		incall_category();
#endif	
}

void mm_platform_exit_call(void)
{
#if TARGET_IPHONE_SIMULATOR
	leave_call();
#elif TARGET_OS_IPHONE		
	AVAudioSession *sess = [AVAudioSession sharedInstance];
	NSString *cat;

	info("mm_platform_ios: exit_call: incall=%s\n",
	     mm_ios.incall ? "yes" : "no");

	mm_ios.incall = false;
	
	cat = [sess category];
	if (cat == AVAudioSessionCategoryAmbient ||
	    cat == AVAudioSessionCategorySoloAmbient) {
		leave_call();
	}
	else {
		default_category(false);
	}
#endif
}

void mm_platform_set_active(void)
{
	info("mm_platform_ios: set_active\n");

	set_active(true);
}

static void snd_destructor(void *arg)
{
	struct sound *snd = arg;

	mem_deref((void *)snd->path);
}

void mm_platform_registerMedia(struct dict *sounds,
				const char *name,
				void *mediaObj,
				bool mixing,
				bool incall,
				int intensity,
				int priority,
				bool is_call_media)
{
	struct sound *snd;

	debug("mm_platform_ios: registerMedia name = %s obj = %p\n",
	      name, mediaObj);
	
	snd = mem_zalloc(sizeof(struct sound), snd_destructor);
	
	str_dup((char **)&snd->path, name);
	snd->arg = mediaObj;
	snd->mixing = mixing;
	snd->incall = incall;
	snd->intensity = intensity;
	snd->priority = priority;
	snd->is_call_media = is_call_media;

	info("mm_platform_ios: registerMedia: %s "
	     "mixing=%d incall=%d int=%d prio=%d is_call=%d\n",
	     name, mixing, incall, intensity, priority, is_call_media);
	
	dict_add(sounds, name, (void*)snd);
	/* snd is now owned by dictionary */
	mem_deref(snd);

	id<AVSMedia> avs_media = mediaObj;

	avs_media.delegate = g_playingTracker;
	if ([avs_media respondsToSelector:@selector(setSound:)]) {
		[avs_media setSound:snd];
	}
}

void mm_platform_unregisterMedia(struct dict *sounds, const char *name){
	struct sound *snd = dict_lookup(sounds, name);
	if (snd) {
		if (snd->arg) {
			id media = (id)snd->arg;
			CFRelease(media);
		}
	}
	dict_remove(sounds, name);
}


static void rec_start_handler(void *arg)
{
	struct mm_platform_start_rec *rec_elem = arg;

	if (rec_elem->rech)
		rec_elem->rech(rec_elem->arg);

	mem_deref(rec_elem);
}

void mm_platform_start_recording(struct mm_platform_start_rec *rec_elem)
{
	info("mm_platform_ios: start_recording incall=%d\n", mm_ios.incall);

	if (mm_ios.incall) {
		mem_deref(rec_elem);
		return;
	}
	
	mm_ios.recording = true;
#if !TARGET_IPHONE_SIMULATOR
	set_category(AVAudioSessionCategoryPlayAndRecord, false);
	set_active(true);
	
	tmr_start(&mm_ios.tmr_rec, 1000, rec_start_handler, rec_elem);	
#else
	tmr_start(&mm_ios.tmr_rec, 1, rec_start_handler, rec_elem);	
#endif
}

void mm_platform_stop_recording(void)
{
	info("mm_platform_ios: stop_recording incall=%d\n", mm_ios.incall);

	mm_ios.recording = false;
#if !TARGET_IPHONE_SIMULATOR
	if (!mm_ios.incall) {
		default_category(true);
		set_active_sync(false);
	}		
#endif
}

void mm_platform_confirm_route(enum mediamgr_auplay route)
{
}
