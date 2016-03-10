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
//  AVSVideoConverter.m
//  zmm
//

#import <UIKit/UIKit.h>
#import <AVFoundation/AVAsset.h>
#import <AVFoundation/AVAssetTrack.h>
#import <AVFoundation/AVAssetReader.h>
#import <AVFoundation/AVAssetReaderOutput.h>
#import <AVFoundation/AVAssetWriter.h>
#import <AVFoundation/AVAssetWriterInput.h>
#import <AVFoundation/AVMediaFormat.h>
#import <AVFoundation/AVAudioSettings.h>
#import <AVFoundation/AVVideoSettings.h>
#import <AVFoundation/AVAssetImageGenerator.h>
#import <AVFoundation/AVTime.h>
#import <CoreMedia/CoreMedia.h>
#import <AVFoundation/AVMetadataItem.h>

#import "AVSVideoConverter.h"

struct {
    int audio_sample_rate;
    int audio_bit_rate;
    int audio_channels;
    int video_width;
    int video_height;
    int video_bit_rate;
} qualitySettings[] = {
    {    0,     0, 0,    0,    0,       0},
    {44100, 64000, 1,  640,  360,  409600},
    {44100, 64000, 1, 1280,  720,  800000},
    {44100, 64000, 1, 1280,  720, 1000000}
};


@interface RWSampleBufferChannel : NSObject
{
@private
	AVAssetReaderOutput     *assetReaderOutput;
	AVAssetWriterInput      *assetWriterInput;
	
	dispatch_block_t        completionHandler;
	dispatch_queue_t        serializationQueue;
	BOOL                    finished;
}

- (id)initWithAssetReaderOutput:(AVAssetReaderOutput *)assetReaderOutput assetWriterInput:(AVAssetWriterInput *)assetWriterInput;
- (void)startWithDelegate:(id <AVSVideoConverterProgressDelegate>)delegate timeRange:(CMTimeRange)timeRange completionHandler:(dispatch_block_t)completionHandler;
- (void)cancel;

@end


@interface AVSVideoConverter ( )
{
	dispatch_queue_t serializationQueue;
	BOOL cancelled;
	AVSVideoConverterQuality quality;
	
	AVAssetReader     *assetReader;
	AVAssetWriter     *assetWriter;
	RWSampleBufferChannel *audioSampleBufferChannel;
	RWSampleBufferChannel *videoSampleBufferChannel;
}

@property AVURLAsset *asset;
@property CMTimeRange timeRange;
@property NSURL *outputURL;
@property UIProgressView *propgerssView;


// These three methods are always called on the serialization dispatch queue
- (BOOL)setUpReaderAndWriterReturningError:(NSError **)outError;  // make sure "tracks" key of asset is loaded before calling this
- (BOOL)startReadingAndWritingReturningError:(NSError **)outError;
- (void)readingAndWritingDidFinishSuccessfully:(BOOL)success withError:(NSError *)error;

@end

@implementation AVSVideoConverter

+ (NSArray *)readableTypes
{
	return [AVURLAsset audiovisualTypes];;
}

+ (BOOL)canConcurrentlyReadDocumentsOfType:(NSString *)typeName
{
	return YES;
}

- (id)init
{
	self = [super init];
	
	if (self)
	{
		NSString *serializationQueueDescription = [NSString stringWithFormat:@"%@ serialization queue", self];
		serializationQueue = dispatch_queue_create([serializationQueueDescription UTF8String], NULL);
	}
	
	return self;
}

@synthesize asset=asset;
@synthesize timeRange=timeRange;
@synthesize outputURL=outputURL;
@synthesize delegate;

- (void)convertVideo:(NSURL*) inputURL outputURL: (NSURL*) _outputURL quality: (AVSVideoConverterQuality) _quality
{
	self.asset = [AVURLAsset URLAssetWithURL:inputURL options:nil];
	
	cancelled = NO;
	quality = _quality;
    
	[self performSelector:@selector(startWithURL:) withObject:_outputURL afterDelay:0.0];  // avoid starting a new sheet while in
}


- (void)startWithURL:(NSURL *)localOutputURL
{
	[self setOutputURL:localOutputURL];
	
	AVAsset *localAsset = [self asset];
	[localAsset loadValuesAsynchronouslyForKeys:[NSArray arrayWithObjects:@"tracks", @"duration", nil] completionHandler:^
	 {
		 // Dispatch the setup work to the serialization queue, to ensure this work is serialized with potential cancellation
		 dispatch_async(serializationQueue, ^{
			 // Since we are doing these things asynchronously, the user may have already cancelled on the main thread.  In that case, simply return from this block
			 if (cancelled)
				 return;
			 
			 BOOL success = YES;
			 NSError *localError = nil;
			 
			 success = ([localAsset statusOfValueForKey:@"tracks" error:&localError] == AVKeyValueStatusLoaded);
			 if (success)
				 success = ([localAsset statusOfValueForKey:@"duration" error:&localError] == AVKeyValueStatusLoaded);
			 
			 if (success)
			 {
				 [self setTimeRange:CMTimeRangeMake(kCMTimeZero, [localAsset duration])];
				 
				 // AVAssetWriter does not overwrite files for us, so remove the destination file if it already exists
				 NSFileManager *fm = [NSFileManager defaultManager];
				 NSString *localOutputPath = [localOutputURL path];
				 if ([fm fileExistsAtPath:localOutputPath])
					 success = [fm removeItemAtPath:localOutputPath error:&localError];
			 }
			 
			 // Set up the AVAssetReader and AVAssetWriter, then begin writing samples or flag an error
			 if (success)
				 success = [self setUpReaderAndWriterReturningError:&localError];
			 if (success)
				 success = [self startReadingAndWritingReturningError:&localError];
			 if (!success)
				 [self readingAndWritingDidFinishSuccessfully:success withError:localError];
		 });
	 }];
}

- (BOOL)setUpReaderAndWriterReturningError:(NSError **)outError
{
	BOOL success = YES;
	NSError *localError = nil;
	AVAsset *localAsset = [self asset];
	NSURL *localOutputURL = [self outputURL];
	
	// Create asset reader and asset writer
	assetReader = [[AVAssetReader alloc] initWithAsset:asset error:&localError];
	success = (assetReader != nil);
	if (success)
	{
		assetWriter = [[AVAssetWriter alloc] initWithURL:localOutputURL fileType:AVFileTypeMPEG4 error:&localError];
		
		success = (assetWriter != nil);
	}
	
	// Create asset reader outputs and asset writer inputs for the first audio track and first video track of the asset
	if (success)
	{
		AVAssetTrack *audioTrack = nil, *videoTrack = nil;
		
		// Grab first audio track and first video track, if the asset has them
		NSArray *audioTracks = [localAsset tracksWithMediaType:AVMediaTypeAudio];
		if ([audioTracks count] > 0)
			audioTrack = [audioTracks objectAtIndex:0];
		NSArray *videoTracks = [localAsset tracksWithMediaType:AVMediaTypeVideo];
		if ([videoTracks count] > 0)
			videoTrack = [videoTracks objectAtIndex:0];
		
		if (audioTrack)
		{

			if (quality == AVSVideoConverterDontReencode) {
				AVAssetReaderOutput *output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:audioTrack outputSettings:nil];
				[assetReader addOutput:output];

				CMFormatDescriptionRef formatDescription = NULL;
				NSArray *formatDescriptions = [audioTrack formatDescriptions];
				if ([formatDescriptions count] > 0)
					formatDescription = (__bridge CMFormatDescriptionRef)[formatDescriptions objectAtIndex:0];
			
				AVAssetWriterInput *input = [AVAssetWriterInput assetWriterInputWithMediaType:[audioTrack mediaType] outputSettings:nil sourceFormatHint:formatDescription];
				[assetWriter addInput:input];

				// Create and save an instance of RWSampleBufferChannel, which will coordinate the work of reading and writing sample buffers
				audioSampleBufferChannel = [[RWSampleBufferChannel alloc] initWithAssetReaderOutput:output assetWriterInput:input];
			}
			else {
				// Decompress to Linear PCM with the asset reader
				NSDictionary *decompressionAudioSettings = [NSDictionary dictionaryWithObjectsAndKeys:
															[NSNumber numberWithUnsignedInt:kAudioFormatLinearPCM], AVFormatIDKey,
															nil];
				AVAssetReaderOutput *output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:audioTrack outputSettings:decompressionAudioSettings];
				[assetReader addOutput:output];
			
				AudioChannelLayout stereoChannelLayout = {
                    .mChannelLayoutTag = qualitySettings[quality].audio_channels == 1 ? kAudioChannelLayoutTag_Mono : kAudioChannelLayoutTag_Stereo,
					.mChannelBitmap = 0,
					.mNumberChannelDescriptions = 0
				};
				NSData *channelLayoutAsData = [NSData dataWithBytes:&stereoChannelLayout length:offsetof(AudioChannelLayout, mChannelDescriptions)];
			
				NSDictionary *compressionAudioSettings = [NSDictionary dictionaryWithObjectsAndKeys:
														  [NSNumber numberWithUnsignedInt:kAudioFormatMPEG4AAC], AVFormatIDKey,
														  [NSNumber numberWithInteger:qualitySettings[quality].audio_bit_rate], AVEncoderBitRateKey,
														  [NSNumber numberWithInteger:qualitySettings[quality].audio_sample_rate], AVSampleRateKey,
														  channelLayoutAsData, AVChannelLayoutKey,
														  [NSNumber numberWithUnsignedInteger:qualitySettings[quality].audio_channels], AVNumberOfChannelsKey,
														  nil];
				AVAssetWriterInput *input = [AVAssetWriterInput assetWriterInputWithMediaType:[audioTrack mediaType] outputSettings:compressionAudioSettings  sourceFormatHint:nil];
				[assetWriter addInput:input];
			
				// Create and save an instance of RWSampleBufferChannel, which will coordinate the work of reading and writing sample buffers
				audioSampleBufferChannel = [[RWSampleBufferChannel alloc] initWithAssetReaderOutput:output assetWriterInput:input];
			}
		}
		
		if (videoTrack)
		{
            
            CGAffineTransform trans = [videoTrack preferredTransform];
           
			if (quality == AVSVideoConverterDontReencode) {
				AVAssetReaderOutput *output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack outputSettings:nil];
				[assetReader addOutput:output];

				CMFormatDescriptionRef formatDescription = NULL;
				NSArray *formatDescriptions = [videoTrack formatDescriptions];
				if ([formatDescriptions count] > 0)
					formatDescription = (__bridge CMFormatDescriptionRef)[formatDescriptions objectAtIndex:0];

				AVAssetWriterInput *input = [AVAssetWriterInput assetWriterInputWithMediaType:[videoTrack mediaType] outputSettings:nil sourceFormatHint:formatDescription];
				input.transform = trans;
				[assetWriter addInput:input];
		
				videoSampleBufferChannel = [[RWSampleBufferChannel alloc] initWithAssetReaderOutput:output assetWriterInput:input];
			}
			else {
				// Decompress to ARGB with the asset reader
				NSDictionary *decompressionVideoSettings = [NSDictionary dictionaryWithObjectsAndKeys:
															[NSNumber numberWithUnsignedInt:kCVPixelFormatType_32ARGB], (id)kCVPixelBufferPixelFormatTypeKey,
															[NSDictionary dictionary], (id)kCVPixelBufferIOSurfacePropertiesKey,
															nil];
				AVAssetReaderOutput *output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack outputSettings:decompressionVideoSettings];
				[assetReader addOutput:output];
			
				// Get the format description of the track, to fill in attributes of the video stream that we don't want to change
				CMFormatDescriptionRef formatDescription = NULL;
				NSArray *formatDescriptions = [videoTrack formatDescriptions];
				if ([formatDescriptions count] > 0)
					formatDescription = (__bridge CMFormatDescriptionRef)[formatDescriptions objectAtIndex:0];
			
				// Grab track dimensions from format description
				CGSize trackDimensions = {
					.width = 0.0,
					.height = 0.0,
				};
                
				if (formatDescription)
					trackDimensions = CMVideoFormatDescriptionGetPresentationDimensions(formatDescription, false, false);
				else
					trackDimensions = [videoTrack naturalSize];
                
                // No point in upscaling
                if (trackDimensions.width > qualitySettings[quality].video_width ||
                    trackDimensions.height > qualitySettings[quality].video_height) {
                    trackDimensions.width = qualitySettings[quality].video_width;
                    trackDimensions.height = qualitySettings[quality].video_height;
                }
                
			
				// Grab clean aperture, pixel aspect ratio from format description
				NSMutableDictionary *compressionSettings = [NSMutableDictionary dictionaryWithObjectsAndKeys:
															AVVideoProfileLevelH264Baseline30, AVVideoProfileLevelKey,
				                                            [NSNumber numberWithInt:qualitySettings[quality].video_bit_rate], AVVideoAverageBitRateKey,
				                                            nil ];
				//NSDictionary *videoSettings = nil;
				if (formatDescription)
				{
					NSDictionary *cleanAperture = nil;
					NSDictionary *pixelAspectRatio = nil;
					CFDictionaryRef cleanApertureFromCMFormatDescription = CMFormatDescriptionGetExtension(formatDescription, kCMFormatDescriptionExtension_CleanAperture);
					if (cleanApertureFromCMFormatDescription)
					{
						cleanAperture = [NSDictionary dictionaryWithObjectsAndKeys:
										 CFDictionaryGetValue(cleanApertureFromCMFormatDescription, kCMFormatDescriptionKey_CleanApertureWidth), AVVideoCleanApertureWidthKey,
										 CFDictionaryGetValue(cleanApertureFromCMFormatDescription, kCMFormatDescriptionKey_CleanApertureHeight), AVVideoCleanApertureHeightKey,
										 CFDictionaryGetValue(cleanApertureFromCMFormatDescription, kCMFormatDescriptionKey_CleanApertureHorizontalOffset), AVVideoCleanApertureHorizontalOffsetKey,
										 CFDictionaryGetValue(cleanApertureFromCMFormatDescription, kCMFormatDescriptionKey_CleanApertureVerticalOffset), AVVideoCleanApertureVerticalOffsetKey,
										 nil];
					}
					CFDictionaryRef pixelAspectRatioFromCMFormatDescription = CMFormatDescriptionGetExtension(formatDescription, kCMFormatDescriptionExtension_PixelAspectRatio);
					if (pixelAspectRatioFromCMFormatDescription)
					{
						pixelAspectRatio = [NSDictionary dictionaryWithObjectsAndKeys:
											CFDictionaryGetValue(pixelAspectRatioFromCMFormatDescription, kCMFormatDescriptionKey_PixelAspectRatioHorizontalSpacing), AVVideoPixelAspectRatioHorizontalSpacingKey,
											CFDictionaryGetValue(pixelAspectRatioFromCMFormatDescription, kCMFormatDescriptionKey_PixelAspectRatioVerticalSpacing), AVVideoPixelAspectRatioVerticalSpacingKey,
											nil];
					}
				
                    if (cleanAperture)
                        [compressionSettings setObject:cleanAperture forKey:AVVideoCleanApertureKey];
                    if (pixelAspectRatio)
                        [compressionSettings setObject:pixelAspectRatio forKey:AVVideoPixelAspectRatioKey];
				}
			
				NSMutableDictionary *videoSettings = [NSMutableDictionary dictionaryWithObjectsAndKeys:
													  AVVideoCodecH264, AVVideoCodecKey,
													  [NSNumber numberWithDouble:trackDimensions.width], AVVideoWidthKey,
													  [NSNumber numberWithDouble:trackDimensions.height], AVVideoHeightKey,
													  nil];
				if (compressionSettings)
					[videoSettings setObject:compressionSettings forKey:AVVideoCompressionPropertiesKey];
			
			
			
				AVAssetWriterInput *input = [AVAssetWriterInput assetWriterInputWithMediaType:[videoTrack mediaType] outputSettings:videoSettings];
				[assetWriter addInput:input];
			
				videoSampleBufferChannel = [[RWSampleBufferChannel alloc] initWithAssetReaderOutput:output assetWriterInput:input];
			}		
		}
	}
	
	if (outError)
		*outError = localError;
	
	return success;
}

- (BOOL)startReadingAndWritingReturningError:(NSError **)outError
{
	BOOL success = YES;
	NSError *localError = nil;
	
	// Instruct the asset reader and asset writer to get ready to do work
	success = [assetReader startReading];
	if (!success)
		localError = [assetReader error];
	if (success)
	{
		success = [assetWriter startWriting];
		if (!success)
			localError = [assetWriter error];
	}
	
	if (success)
	{
		dispatch_group_t dispatchGroup = dispatch_group_create();
		
		// Start a sample-writing session
		[assetWriter startSessionAtSourceTime:[self timeRange].start];
		
		// Start reading and writing samples
		if (audioSampleBufferChannel)
		{
			dispatch_group_enter(dispatchGroup);
            [audioSampleBufferChannel startWithDelegate:[self delegate] timeRange:[self timeRange] completionHandler:^{
				dispatch_group_leave(dispatchGroup);
			}];
		}
		if (videoSampleBufferChannel)
		{
			dispatch_group_enter(dispatchGroup);
			[videoSampleBufferChannel startWithDelegate:nil timeRange:[self timeRange] completionHandler:^{
				dispatch_group_leave(dispatchGroup);
			}];
		}
		
		// Set up a callback for when the sample writing is finished
		dispatch_group_notify(dispatchGroup, serializationQueue, ^{
			BOOL finalSuccess = YES;
			NSError *finalError = nil;
			
			if (cancelled)
			{
				[assetReader cancelReading];
				[assetWriter cancelWriting];
			}
			else
			{
				if ([assetReader status] == AVAssetReaderStatusFailed)
				{
					finalSuccess = NO;
					finalError = [assetReader error];
				}
				
				if (finalSuccess)
				{
                    [assetWriter finishWritingWithCompletionHandler: ^{
              			NSError *completionError = nil;

						if (assetWriter.status == AVAssetWriterStatusFailed) {
							completionError = [assetWriter error];
						}
						[self readingAndWritingDidFinishSuccessfully:finalSuccess withError:completionError];
                  	}];
                    	
				}
			}
			
		});
	}
	
	if (outError)
		*outError = localError;
	
	return success;
}

- (void)cancel
{
	// Dispatch cancellation tasks to the serialization queue to avoid races with setup and teardown
	dispatch_async(serializationQueue, ^{
		[audioSampleBufferChannel cancel];
		[videoSampleBufferChannel cancel];
		cancelled = YES;
	});
}

- (void)readingAndWritingDidFinishSuccessfully:(BOOL)success withError:(NSError *)error
{
    if (!success)
	{
		[assetReader cancelReading];
		[assetWriter cancelWriting];
	}
	
	// Tear down
	assetReader = nil;
	assetWriter = nil;
	audioSampleBufferChannel = nil;
	videoSampleBufferChannel = nil;
	cancelled = NO;
    
    if ([delegate respondsToSelector:@selector(didCompleteSuccessfully:withError:)]) {
        [delegate didCompleteSuccessfully:success withError:error];
    }

}


static double progressOfSampleBufferInTimeRange(CMSampleBufferRef sampleBuffer, CMTimeRange timeRange)
{
	CMTime progressTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
	progressTime = CMTimeSubtract(progressTime, timeRange.start);
	CMTime sampleDuration = CMSampleBufferGetDuration(sampleBuffer);
	if (CMTIME_IS_NUMERIC(sampleDuration))
		progressTime= CMTimeAdd(progressTime, sampleDuration);
	return CMTimeGetSeconds(progressTime) / CMTimeGetSeconds(timeRange.duration);
}

@end


@interface RWSampleBufferChannel ()
- (void)callCompletionHandlerIfNecessary;  // always called on the serialization queue
@end

@implementation RWSampleBufferChannel

- (id)initWithAssetReaderOutput:(AVAssetReaderOutput *)localAssetReaderOutput assetWriterInput:(AVAssetWriterInput *)localAssetWriterInput
{
	self = [super init];
	
	if (self)
	{
		assetReaderOutput = localAssetReaderOutput;
		assetWriterInput = localAssetWriterInput;
		
		finished = NO;
		NSString *serializationQueueDescription = [NSString stringWithFormat:@"%@ serialization queue", self];
		serializationQueue = dispatch_queue_create([serializationQueueDescription UTF8String], NULL);
	}
	
	return self;
}


- (void)startWithDelegate:(id <AVSVideoConverterProgressDelegate>)delegate timeRange:(CMTimeRange) timeRange completionHandler:(dispatch_block_t)localCompletionHandler
{
	completionHandler = [localCompletionHandler copy];
	
	[assetWriterInput requestMediaDataWhenReadyOnQueue:serializationQueue usingBlock:^{
		if (finished)
			return;
		
		BOOL completedOrFailed = NO;
		
		// Read samples in a loop as long as the asset writer input is ready
		while ([assetWriterInput isReadyForMoreMediaData] && !completedOrFailed)
		{
			CMSampleBufferRef sampleBuffer = [assetReaderOutput copyNextSampleBuffer];
			if (sampleBuffer != NULL)
			{
                if ([delegate respondsToSelector:@selector(updateProgress:)]) {
                    double progress = progressOfSampleBufferInTimeRange(sampleBuffer, timeRange);

					[delegate updateProgress:progress];
                }
				
				BOOL success = [assetWriterInput appendSampleBuffer:sampleBuffer];
				CFRelease(sampleBuffer);
				sampleBuffer = NULL;
				
				completedOrFailed = !success;
			}
			else
			{
				completedOrFailed = YES;
			}
		}
		
		if (completedOrFailed)
			[self callCompletionHandlerIfNecessary];
	}];
}


- (void)cancel
{
	dispatch_async(serializationQueue, ^{
		[self callCompletionHandlerIfNecessary];
	});
}


- (void)callCompletionHandlerIfNecessary
{
	// Set state to mark that we no longer need to call the completion handler, grab the completion handler, and clear out the ivar
	BOOL oldFinished = finished;
	finished = YES;
	
	if (oldFinished == NO)
	{
		[assetWriterInput markAsFinished];  // let the asset writer know that we will not be appending any more samples to this input
		
		dispatch_block_t localCompletionHandler = completionHandler;
		completionHandler = nil;
		
		if (localCompletionHandler)
		{
			localCompletionHandler();
		}
	}
}



@end

