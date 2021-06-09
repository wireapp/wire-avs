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

#import "AVSMediaProtocols.h"
#import "AVSFlowManager.h"


@implementation AVSFlowManager

+ (void)setLogLevel:(AVSFlowManagerLogLevel)logLevel
{
	(void)logLevel;
}

+ (NSComparator)conferenceComparator
{
	return nil;
}

+ (instancetype)getInstance
{
	return nil;
}

- (instancetype)init:(uint64_t)avs_flags
{
	return self;
}


- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate
	mediaManager:(id)mediaManager
{
	return self;
}


- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate
	flowManager:(struct flowmgr *)flowManager mediaManager:(id)mediaManager
{
	return self;
}


- (instancetype)initWithDelegate:(id<AVSFlowManagerDelegate>)delegate
	mediaManager:(id)mediaManager flags:(uint64_t)avs_flags
{
	return self;
}

- (void)dealloc
{
}


- (BOOL)isReady
{
	return YES;
}


- (void)appendLogForConversation:(NSString *)convId message:(NSString *)msg
{
	(void)convId;
	(void)msg;
}


- (void)processResponseWithStatus:(int)status
			   reason:(NSString *)reason
			mediaType:(NSString *)mtype
			  content:(NSData *)content
			  context:(void const *)ctx
{
}


- (BOOL)processEventWithMediaType:(NSString *)mtype
	     content:(NSData *)content
{
	return false;
}


- (BOOL)acquireFlows:(NSString *)convId
{
	return true;
}

- (void)releaseFlows:(NSString *)convId
{
}


- (void)setActive:(NSString *)convId active:(BOOL)active
{
}


- (void)addUser:(NSString *)convId userId:(NSString *)userId
	   name:(NSString *)name
{
}


- (void)setSelfUser:(NSString *)userId
{
}


- (void)refreshAccessToken:(NSString *)token type:(NSString *)type
{
}



/*- (void)mediaCategoryChanged:(NSString *)convId
		    category:(AVSFlowManagerCategory)cat
{
}*/


- (NSArray *)events
{
	return nil;
}


- (void)networkChanged
{
	// XXX Implement a call to flowmgr
}

- (BOOL)isMuted
{
	return NO;
}


- (int)setMute:(BOOL)muted
{
	return 0;
}


- (NSArray *)sortConferenceParticipants:(NSArray *)participants
{
	return nil;
}


- (void)callInterruptionStartInConversation:(NSString *)convId
{
}


- (void)callInterruptionEndInConversation:(NSString *)convId
{
}


- (void)mediaCategoryChanged:(NSString *)convId category:(AVSFlowManagerCategory)category
{
    
}

- (BOOL)isMediaEstablishedInConversation:(NSString *)convId
{
	return NO;
}


- (void)updateModeInConversation:(NSString *)convId withCategory:(AVSFlowManagerCategory)category
{
    
}

- (void)updateVolumeForUser:(NSString *)uid inVol:(float)input outVol:(float)output inConversation:(NSString *)convId
{
    
}


- (void)conferenceParticipants:(NSArray *)participants inConversation:(NSString *)convId
{

}


- (void)handleError:(int)error inConversation:(NSString *)convId
{
    
}

- (void)mediaEstablishedInConversation:(NSString *)convId
{
    
}


- (void)setEnableLogging:(BOOL)enable
{

}


- (void)setEnableMetrics:(BOOL)enable
{

}


- (void)setSessionId:(NSString *)sessId forConversation:(NSString *)convId
{
}


- (BOOL)canSendVideoForConversation:(NSString *)convId
{
	return NO;
}

- (BOOL)isSendingVideoInConversation:(NSString *)convId
		      forParticipant:(NSString *)partId
{
	return NO;
}

- (void)setVideoSendState:(AVSFlowManagerVideoSendState)state
          forConversation:(NSString *)convId
{
}

- (void)attachVideoPreview:(UIView *)view
{
}

- (void)detachVideoPreview:(UIView *)view
{
}

- (void)attachVideoView:(UIView *)view
{
}

- (void)detachVideoView:(UIView *)view
{
}

- (void)startVideoCapture
{
}

- (void)stopVideoCapture
{
}

- (NSArray*)getVideoCaptureDevices
{
	return nil;
}

- (void)setVideoCaptureDevice:(NSString *)deviceId forConversation:(NSString *)convId
{
}

- (int)setAudioEffect:(AVSAudioEffectType) effect
{
    return 0;
}

@end

