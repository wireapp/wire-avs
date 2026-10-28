/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#define CADisplayLink HostDisplayLink

typedef NSInteger UIInterfaceOrientation;
enum
{
    UIViewAutoresizingFlexibleWidth = 1,
    UIViewAutoresizingFlexibleHeight = 2
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
- (void)startRunning;
- (void)stopRunning;
- (void)render:(CADisplayLink *)link;
- (void)layoutSubviews;
@end
