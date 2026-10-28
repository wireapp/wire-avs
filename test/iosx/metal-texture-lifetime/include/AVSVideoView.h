/*
* Wire
* Copyright (C) 2026 Wire Swiss GmbH
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

/* Host stand-in for the UIKit parts AVSVideoView_Metal.mm uses. */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

/* Keep clear of the macOS QuartzCore CADisplayLink. */
#define CADisplayLink TestDisplayLink

typedef NSInteger UIInterfaceOrientation;

enum {
	UIViewAutoresizingFlexibleWidth = 1 << 1,
	UIViewAutoresizingFlexibleHeight = 1 << 4
};

@interface UIView : NSObject
@property CGRect bounds;
@property CGRect frame;
@property NSUInteger autoresizingMask;
@property (strong) CALayer *layer;
@property (strong) id window;
@end

@interface CADisplayLink : NSObject
+ (instancetype)displayLinkWithTarget:(id)target selector:(SEL)selector;
- (void)addToRunLoop:(NSRunLoop *)loop forMode:(NSRunLoopMode)mode;
- (void)invalidate;
@end

@interface UIScreen : NSObject
+ (instancetype)mainScreen;
@property (readonly) CGFloat scale;
@end

struct avs_vidframe;

@interface AVSVideoView : UIView

@property (nonatomic) BOOL shouldFill;
@property (nonatomic) CGFloat fillRatio;
@property (copy) NSString *userid;
@property (copy) NSString *clientid;
@property (readonly) CGSize videoSize;

- (BOOL)handleFrame:(struct avs_vidframe *)frame;

/* Private to AVSVideoView_Metal.mm, driven directly by the test. */
- (void)startRunning;
- (void)stopRunning;
- (void)render:(CADisplayLink *)displayLink;
- (void)layoutSubviews;

@end
