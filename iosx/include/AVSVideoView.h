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

#import <TargetConditionals.h>

#if TARGET_OS_IOS || TARGET_OS_SIMULATOR
    #import <UIKit/UIKit.h>
    typedef UIView *AVSView;
    #import <OpenGLES/ES2/gl.h>
    #import <OpenGLES/ES2/glext.h>
#elif TARGET_OS_OSX
    // macOS uses AppleKit for UI
    #import <AppKit/AppKit.h>
    typedef NSView *AVSView;
    // macOS uses the desktop OpenGL framework instead of OpenGLES
    #import <OpenGL/gl3.h>
#endif

#import <QuartzCore/QuartzCore.h>

#ifndef AVS_EXPORT
#define AVS_EXPORT __attribute__((visibility("default")))
#endif


struct avs_vidframe;

AVS_EXPORT @interface AVSVideoView : UIView

@property (nonatomic) BOOL shouldFill;
@property (nonatomic) CGFloat fillRatio;
@property (copy) NSString *userid;
@property (copy) NSString *clientid;
@property (readonly) CGSize videoSize;

- (BOOL) handleFrame:(struct avs_vidframe*) frame;

@end
