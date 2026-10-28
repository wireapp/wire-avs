/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#import "AVSVideoView.h"
@interface AVSFlowManager : NSObject
+ (instancetype)getInstance;
- (void)attachVideoView:(UIView *)view;
- (void)detachVideoView:(UIView *)view;
@end
