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

#import "AVSVideoPreview.h"
#import "AVSFlowManager.h"

#include <re/re.h>
#include <avs.h>

@implementation AVSVideoPreview
{
	BOOL _attached;
}

- (void)didMoveToWindow
{
	info("Preview(%p) didMoveToWindow %p attached %s\n", self, self.window,
		_attached ? "TRUE" : "FALSE");
	AVSFlowManager *fm = [AVSFlowManager getInstance];
	if (self.window) {
		if (!_attached) {
			if (fm) {
				debug("attachPreviewView %p\n", self);
				[fm attachVideoPreview: self];
				_attached = YES;
			}
		}
	}
	else {
		if (fm) {
			debug("detachPreviewView %p\n", self);
			[fm detachVideoPreview: self];
		}
		_attached = NO;
	}
}

- (void)layoutSubviews
{
	CGRect frame = self.bounds;

	for (CALayer *l in self.layer.sublayers) {
		l.frame = frame;
	} 
}

@end

