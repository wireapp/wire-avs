/*
 * Wire
 * Copyright (C) 2016 Wire Swiss GmbH
 *
 * The Wire Software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * The Wire Software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Wire Software. If not, see <http://www.gnu.org/licenses/>.
 *
 * This module of the Wire Software uses software code from
 * WebRTC (https://chromium.googlesource.com/external/webrtc)
 *
 * *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 * *
 * *  Use of the WebRTC source code on a stand-alone basis is governed by a
 * *  BSD-style license that can be found in the LICENSE file in the root of
 * *  the source tree.
 * *  An additional intellectual property rights grant can be found
 * *  in the file PATENTS.  All contributing project authors to Web RTC may
 * *  be found in the AUTHORS file in the root of the source tree.
 */

#import <TargetConditionals.h>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

#import "iosx/include/AVSCapturer.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include <re/re.h>
#include <avs.h>
#include <avs_wcall.h>

#if TARGET_OS_IPHONE

enum AVSDeviceOrientation {
	AVSDeviceOrientationUnknown =            UIDeviceOrientationUnknown,
	AVSDeviceOrientationPortrait =           UIDeviceOrientationPortrait,
	AVSDeviceOrientationPortraitUpsideDown = UIDeviceOrientationPortraitUpsideDown,
	AVSDeviceOrientationLandscapeLeft =      UIDeviceOrientationLandscapeLeft,
	AVSDeviceOrientationLandscapeRight =     UIDeviceOrientationLandscapeRight
};

#endif

@interface AVSCapturer ()
{
	AVSCapturerState _state;
	NSCondition* _stateCondition;

	AVCaptureSession *_captureSession;
	AVCaptureVideoPreviewLayer *_previewLayer;
	AVCaptureConnection* _connection;
	UIView *_preview;
	dispatch_queue_t _cmdQueue;
	dispatch_queue_t _frameQueue;

	uint32_t _width;
	uint32_t _height;
	uint32_t _maxFps;
	CMTime _tsOffset;
	BOOL _firstFrame;
	NSString* _captureDevice;
	BOOL _isFront;
#if TARGET_OS_IPHONE
	AVSDeviceOrientation _orientation;
#endif
}

- (void)startCaptureWithOutput: (AVCaptureVideoDataOutput*)currentOutput;
- (void)stopCapture;

- (void)setState: (AVSCapturerState)state;

- (int)setCaptureDeviceInt:(NSString*)devid;

- (void)captureOutput:(AVCaptureOutput*)captureOutput
	didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
	fromConnection:(AVCaptureConnection*)connection;

@end

@implementation AVSCapturer

- (id)init
{
	self = [super init];
	if (self) {
		//debug("%s\n", __FUNCTION__);
		_state = AVS_CAPTURER_STATE_STOPPED;
		_stateCondition = [[NSCondition alloc] init];
		_cmdQueue = dispatch_queue_create(
			"com.wire.avs_capture_cmd_queue",
			DISPATCH_QUEUE_SERIAL);
		_frameQueue = dispatch_queue_create(
			"com.wire.avs_capture_frame_queue",
			DISPATCH_QUEUE_SERIAL);

		_captureSession = [[AVCaptureSession alloc] init];
		if (!_captureSession) {
			return nil;
		}

		_previewLayer = [AVCaptureVideoPreviewLayer layerWithSession:_captureSession];
		[_previewLayer setVideoGravity:AVLayerVideoGravityResizeAspectFill];
		[_previewLayer setDelegate: self];
		for (CALayer *layer in _previewLayer.sublayers) {
			[layer setDelegate: self];
		}

		NSArray *devArray = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
		if (devArray && [devArray count] > 0) {
			AVCaptureDevice *dev = [devArray objectAtIndex:([devArray count] - 1)];
			_captureDevice = dev ? [NSString stringWithString:dev.uniqueID] : nil;
		}

		_preview = nil;

		AVCaptureVideoDataOutput* captureOutput =
			[[AVCaptureVideoDataOutput alloc] init];
		NSString* key = (NSString*)kCVPixelBufferPixelFormatTypeKey;

		NSNumber* val = [NSNumber
			numberWithUnsignedInt:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange];
		NSDictionary* videoSettings =
			[NSDictionary dictionaryWithObject:val forKey:key];
		captureOutput.videoSettings = videoSettings;

		if ([_captureSession canAddOutput:captureOutput]) {
			[_captureSession addOutput:captureOutput];
		}
		else {
			error("%s: failed to set captureSessions output\n", __FUNCTION__);
			return nil;
		}

		NSNotificationCenter* notify = [NSNotificationCenter defaultCenter];
		[notify addObserver:self
				selector:@selector(onVideoError:)
				name:AVCaptureSessionRuntimeErrorNotification
				object:_captureSession];
#if TARGET_OS_IPHONE
		[notify addObserver:self
				selector:@selector(deviceOrientationDidChange:)
				name:UIDeviceOrientationDidChangeNotification
				object:nil];

		dispatch_async(
			dispatch_get_main_queue(), 
			^(void) { 
				[self deviceOrientationDidChange:nil];
		});
#endif

	}
	return self;
}

- (int)startWithWidth:(uint32_t)width Height:(uint32_t)height MaxFps:(uint32_t)max_fps
{
	info("%s: capsize: %ux%u fps: %u\n", __FUNCTION__, width, height, max_fps);
	if (!_captureSession) {
		error("%s: no capture session\n", __FUNCTION__);
		return ENODEV;
	}

	// check limits of the resolution
	if (max_fps > 60) {
		error("%s: bad parameters\n", __FUNCTION__);
		return EINVAL;
	}

	// TODO: check params are met also
	if (_state == AVS_CAPTURER_STATE_RUNNING) {
		info("%s: already running!\n", __FUNCTION__);
		return 0;
	}

	[self setState:AVS_CAPTURER_STATE_STARTING];

	dispatch_async(
		_cmdQueue,
		^(void) { 
			_width = width;
			_height = height;
			_maxFps = max_fps;
			_firstFrame = YES;
			AVCaptureVideoDataOutput* currentOutput =
				[[_captureSession outputs] firstObject];

			[self directOutputToSelf];
			[self startCaptureWithOutput:currentOutput];

#if TARGET_OS_IPHONE
			dispatch_async(
				dispatch_get_main_queue(), 
				^(void) { 
					[self deviceOrientationDidChange:nil];
			});
#endif
	});
	return 0;
}

- (int)stop
{
	//debug("%s: stopping\n", __FUNCTION__);
	[self directOutputToNil];

	if (!_captureSession) {
		return ENODEV;
	}

	[self setState:AVS_CAPTURER_STATE_STOPPING];
	dispatch_async(
		_cmdQueue,
			 ^(void) { [self stopCapture];
	});

	return 0;
}

- (void)stopCapture
{
	[_captureSession stopRunning];
	[self setState:AVS_CAPTURER_STATE_STOPPED];

#if TARGET_OS_IPHONE
	[[UIDevice currentDevice] endGeneratingDeviceOrientationNotifications];
#endif
}

- (void)startCaptureWithOutput: (AVCaptureVideoDataOutput*)currentOutput
{
	NSString* captureQuality = [NSString stringWithString:AVCaptureSessionPresetLow];

#if TARGET_OS_IPHONE
	if ((_width > 1280 || _height > 720) &&
		[_captureSession canSetSessionPreset:AVCaptureSessionPreset1920x1080]) {
		captureQuality = [NSString stringWithString:AVCaptureSessionPreset1920x1080];
	}
	else
#endif
	if ((_width > 640 || _height > 480) &&
		[_captureSession canSetSessionPreset:AVCaptureSessionPreset1280x720]) {
		captureQuality = [NSString stringWithString:AVCaptureSessionPreset1280x720];
	}
	else if ((_width > 480 || _height > 360) &&
		[_captureSession canSetSessionPreset:AVCaptureSessionPreset640x480]) {
		captureQuality = [NSString stringWithString:AVCaptureSessionPreset640x480];
	}
	else if ((_width > 352 || _height > 288) &&
		[_captureSession canSetSessionPreset:AVCaptureSessionPresetMedium]) {
		captureQuality = [NSString stringWithString:AVCaptureSessionPresetMedium];
	}
	else if ((_width > 192 || _height > 144) &&
		[_captureSession canSetSessionPreset:AVCaptureSessionPreset352x288]) {
		captureQuality = [NSString stringWithString:AVCaptureSessionPreset352x288];
	}

	info("%s: capture starting with quality: %s\n", __FUNCTION__,
		[captureQuality UTF8String]);

	[_captureSession beginConfiguration];
	[_captureSession setSessionPreset:captureQuality];

	NSArray* sessionInputs = _captureSession.inputs;

	if ([sessionInputs count] < 1) {
		[self setCaptureDeviceInt:_captureDevice];
	}

	_connection = [currentOutput connectionWithMediaType:AVMediaTypeVideo];
	[_captureSession commitConfiguration];

	[_captureSession startRunning];
	[self setState:AVS_CAPTURER_STATE_RUNNING];

#if TARGET_OS_IPHONE
	[[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
#endif
}

- (int)setCaptureDevice:(NSString*)devid
{
	dispatch_async(
		_cmdQueue,
		^(void) {[self setCaptureDeviceInt:devid];
	});

	return 0;
}


- (int)setCaptureDeviceInt:(NSString*)devid
{
	NSArray* currentInputs = [_captureSession inputs];
	if ([currentInputs count] > 0) {
		AVCaptureInput* currentInput = (AVCaptureInput*)[currentInputs objectAtIndex:0];
		[_captureSession removeInput:currentInput];
	}

	AVCaptureDevice* captureDevice = nil;

	if (devid) {
		captureDevice = [AVCaptureDevice deviceWithUniqueID:devid];
	}

	if (!captureDevice) {
		NSArray *devArray = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
		if (devArray && [devArray count] > 0) {
			captureDevice = [devArray objectAtIndex:([devArray count] - 1)];
		}
	}

	if (!captureDevice) {
		error("%s: counldnt find device with id %s\n", __FUNCTION__, [devid UTF8String]);
		return ENODEV;
	}

	NSError* deviceError = nil;
	AVCaptureDeviceInput* newCaptureInput =
		[AVCaptureDeviceInput deviceInputWithDevice:captureDevice
		error:&deviceError];

	if (!newCaptureInput) {
		const char* errorMessage = [[deviceError localizedDescription] UTF8String];
		error("%s: error selecting input: %s\n", __FUNCTION__, errorMessage);
		return EINVAL;
	}

	[_captureSession beginConfiguration];

	int err = 0;
	if ([_captureSession canAddInput:newCaptureInput]) {
		[_captureSession addInput:newCaptureInput];
	}
	else {
		error("%s: couldnt add input\n", __FUNCTION__);
		err = EINVAL;
	}

	NSArray* sessionInputs = _captureSession.inputs;

	AVCaptureDeviceInput* deviceInput = [sessionInputs count] > 0 ?
		sessionInputs[0] : nil;

	AVCaptureDevice* inputDevice = deviceInput.device;
	BOOL fps_in_range = NO;

	if (inputDevice) {
		AVCaptureDeviceFormat* activeFormat = inputDevice.activeFormat;
		NSArray* supportedRanges = activeFormat.videoSupportedFrameRateRanges;
		AVFrameRateRange* targetRange = [supportedRanges count] > 0 ?
			supportedRanges[0] : nil;

		for (AVFrameRateRange* range in supportedRanges) {
			if (range.minFrameRate <= _maxFps && range.maxFrameRate >= _maxFps &&
				targetRange.minFrameRate >= range.minFrameRate) {
				targetRange = range;
				fps_in_range = YES;
			}
		}

		if (targetRange && [inputDevice lockForConfiguration:NULL]) {
			if(fps_in_range == YES){
				inputDevice.activeVideoMinFrameDuration =
					CMTimeMake(1, _maxFps);
				inputDevice.activeVideoMaxFrameDuration =
					CMTimeMake(1, _maxFps);
			}
			else {
				inputDevice.activeVideoMinFrameDuration =
					targetRange.minFrameDuration;
				inputDevice.activeVideoMaxFrameDuration =
					targetRange.minFrameDuration;
			}

			[inputDevice unlockForConfiguration];
		}      
	}
	[_captureSession commitConfiguration];

	_isFront = (captureDevice.position == AVCaptureDevicePositionFront);
	return err;
}

- (void)attachPreviewInt:(UIView*)preview
{
	dispatch_sync(
		dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), 
		^(void) {
			if (_preview) {
#if TARGET_OS_IPHONE
				[_previewLayer removeFromSuperlayer];
#else
				[_preview setWantsLayer:NO];
				[_preview setLayer:nil];
#endif
			}
			_preview = preview;
			if (_preview) {
				_previewLayer.frame = _preview.bounds;
#if TARGET_OS_IPHONE
				[_preview.layer addSublayer:_previewLayer];
				dispatch_async(
					dispatch_get_main_queue(), 
					^(void) { 
						[self deviceOrientationDidChange:nil];
				});
#else
				[_preview setLayer:(CALayer*)_previewLayer];
				[_preview setWantsLayer:YES];
#endif
			}
	});
}

- (void)attachPreview:(UIView*)preview
{
	dispatch_async(
		_cmdQueue, 
			^(void) { [self attachPreviewInt:preview];
	});
}

- (void)detachPreviewInt:(UIView*)preview
{
	dispatch_sync(
		dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), 
		^(void) {
			if (preview == _preview) {
#if TARGET_OS_IPHONE
				[_previewLayer removeFromSuperlayer];
#else
				[_preview setWantsLayer:NO];
				[_preview setLayer:nil];
#endif
				_preview = nil;
			}
	});
}

- (void)detachPreview:(UIView*)preview
{
	dispatch_async(
		_cmdQueue, 
			^(void) { [self detachPreviewInt:preview];
	});
}

- (void)setState: (AVSCapturerState)state
{
	//debug(">>%s state %d new %d\n", __FUNCTION__, _state, state);
	[_stateCondition lock];
	_state = state;
	[_stateCondition signal];
	[_stateCondition unlock];
	//debug("<<%s state %d\n", __FUNCTION__, _state);
}

- (void)waitForStableState
{
	//debug(">>%s state %d\n", __FUNCTION__, _state);
	[_stateCondition lock];
	while (_state == AVS_CAPTURER_STATE_STARTING ||
		_state == AVS_CAPTURER_STATE_STOPPING) {
		[_stateCondition wait];
	}
	[_stateCondition unlock];
	//debug("<<%s state %d\n", __FUNCTION__, _state);
}

- (AVSCapturerState)getState
{
	AVSCapturerState	state;

	[_stateCondition lock];
	state = _state;
	[_stateCondition signal];

	return state;
}

- (void)onVideoError:(NSNotification*)notification {
	NSLog(@"onVideoError: %@", notification);
}

- (void)captureOutput:(AVCaptureOutput*)captureOutput
	didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
	fromConnection:(AVCaptureConnection*)connection {

	const int kFlags = 0;
	CVImageBufferRef videoFrame = CMSampleBufferGetImageBuffer(sampleBuffer);

	if (CVPixelBufferLockBaseAddress(videoFrame, kFlags) != kCVReturnSuccess) {
		return;
	}

	const int kYPlaneIndex = 0;
	const int kUVPlaneIndex = 1;

	struct avs_vidframe frame;

	memset(&frame, 0, sizeof(frame));

	frame.type = AVS_VIDFRAME_NV12;
	frame.w = CVPixelBufferGetWidth(videoFrame);
	frame.h = CVPixelBufferGetHeight(videoFrame);

#if TARGET_OS_IPHONE
	switch (_orientation) {
		case AVSDeviceOrientationPortrait:
			frame.rotation = 90;
			break;

		case AVSDeviceOrientationPortraitUpsideDown:
			frame.rotation = 270;
			break;

		case AVSDeviceOrientationLandscapeLeft:
			frame.rotation = _isFront ? 180 : 0;
			break;

		case AVSDeviceOrientationLandscapeRight:
			frame.rotation = _isFront ? 0 : 180;
			break;

		case AVSDeviceOrientationUnknown:
			frame.rotation = 0;
			break;
	}
#endif

	frame.y = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(videoFrame, kYPlaneIndex);
	frame.u = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(videoFrame, kUVPlaneIndex);
	frame.ys = (size_t)CVPixelBufferGetBytesPerRowOfPlane(videoFrame, kYPlaneIndex);
	frame.us = (size_t)CVPixelBufferGetBytesPerRowOfPlane(videoFrame, kUVPlaneIndex);

	CMTime ct = CMSampleBufferGetOutputPresentationTimeStamp(sampleBuffer);
	if (_firstFrame) {
		_firstFrame = NO;
		_tsOffset = ct;
		frame.ts = 0;
	}
	else {
		frame.ts = uint32_t(CMTimeGetSeconds(CMTimeSubtract(ct, _tsOffset)) * 1000);
	}
	
	//printf("%s: sending frame %dx%d ts: %u\n", __FUNCTION__, frame.w, frame.h, frame.ts);
	wcall_handle_frame(&frame);

out:
	CVPixelBufferUnlockBaseAddress(videoFrame, kFlags);
}

- (void)directOutputToSelf {

	AVCaptureVideoDataOutput* currentOutput = [[_captureSession outputs] firstObject];
	[currentOutput setSampleBufferDelegate:self queue:_frameQueue];
}

- (void)directOutputToNil {
	AVCaptureVideoDataOutput* currentOutput = [[_captureSession outputs] firstObject];
	[currentOutput setSampleBufferDelegate:nil queue:NULL];
}

- (id<CAAction>)actionForLayer:(CALayer *)layer 
                        forKey:(NSString *)event
{
	return NSNull.null;
}

#if TARGET_OS_IPHONE

- (void)deviceOrientationDidChange:(NSNotification*)notification
{
	AVSDeviceOrientation newori = AVSDeviceOrientationUnknown;
	UIDeviceOrientation devori = [UIDevice currentDevice].orientation;
	UIInterfaceOrientation uiori = [[UIApplication sharedApplication] statusBarOrientation];

	switch (devori) {
		case UIDeviceOrientationPortrait:
			newori = AVSDeviceOrientationPortrait;
			break;

		case UIDeviceOrientationPortraitUpsideDown:
			newori = AVSDeviceOrientationPortraitUpsideDown;
			break;

		case UIDeviceOrientationLandscapeLeft:
			newori = AVSDeviceOrientationLandscapeLeft;
			break;

		case UIDeviceOrientationLandscapeRight:
			newori = AVSDeviceOrientationLandscapeRight;
			break;

		default:
			break;
	}

	if (newori == AVSDeviceOrientationUnknown) {
		switch (uiori) {
			case UIInterfaceOrientationPortrait:
				newori = AVSDeviceOrientationPortrait;
				break;

			case UIInterfaceOrientationPortraitUpsideDown:
				newori = AVSDeviceOrientationPortraitUpsideDown;
				break;

			case UIInterfaceOrientationLandscapeLeft:
				newori = AVSDeviceOrientationLandscapeLeft;
				break;

			case UIInterfaceOrientationLandscapeRight:
				newori = AVSDeviceOrientationLandscapeRight;
				break;

			default:
				newori = AVSDeviceOrientationPortrait;
				break;
		}
	}

	if (newori != _orientation) {
		info("%s old: %d new: %d dev: %d ui: %d ly: %p pv: %p\n", __FUNCTION__,
			_orientation, newori, devori, uiori, _previewLayer, _preview);
		_orientation = newori;
	}

	if (!_previewLayer || !_preview) {
		return;
	}

	switch (_orientation) {
		case AVSDeviceOrientationPortrait:
			[_previewLayer.connection
				setVideoOrientation:AVCaptureVideoOrientationPortrait];
			break;

		case AVSDeviceOrientationPortraitUpsideDown:
			[_previewLayer.connection
				setVideoOrientation:AVCaptureVideoOrientationPortraitUpsideDown];
			break;

		case AVSDeviceOrientationLandscapeLeft:
			[_previewLayer.connection
				setVideoOrientation:AVCaptureVideoOrientationLandscapeRight];
			break;

		case AVSDeviceOrientationLandscapeRight:
			[_previewLayer.connection
				setVideoOrientation:AVCaptureVideoOrientationLandscapeLeft];
			break;

		default:
			break;
	}
}

#endif

@end

