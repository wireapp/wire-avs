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


#include <re.h>
#include <avs.h>

#import "AVSVideoView.h"
#import "AVSFlowManager.h"

#define DBG_BGCOLOR 0

const char vertShader[] = {
	"attribute vec4 aPosition;\n"
	"attribute vec2 aTextureCoord;\n"
	"varying vec2 vTextureCoord;\n"
	"void main() {\n"
	"  gl_Position = aPosition;\n"
	"  vTextureCoord = aTextureCoord;\n"
	"}\n"};

const char fragShader[] = {
	"precision mediump float;\n"
	"uniform sampler2D Ytex;\n"
	"uniform sampler2D Utex,Vtex;\n"
	"varying vec2 vTextureCoord;\n"
	"void main(void) {\n"
	"  float nx,ny,r,g,b,y,u,v;\n"
	"  nx=vTextureCoord[0];\n"
	"  ny=vTextureCoord[1];\n"
	"  y=texture2D(Ytex,vec2(nx,ny)).r;\n"
	"  u=texture2D(Utex,vec2(nx,ny)).r;\n"
	"  v=texture2D(Vtex,vec2(nx,ny)).r;\n"
	"  u=u-0.5;\n"
	"  v=v-0.5;\n"
	"  r=y+1.403*v;\n"
	"  g=y-0.344*u-0.714*v;\n"
	"  b=y+1.770*u;\n"
	"  gl_FragColor=vec4(r,g,b,1.0);\n"
	"}\n"};

const char indices[] = {0, 1, 2, 3};


@implementation AVSVideoView
{
	CAEAGLLayer* _eaglLayer;
	EAGLContext* _context;
	GLuint _colorRenderBuffer;
	GLfloat _vertices[20];
	GLuint _texIds[3];
	GLuint _program;
	GLsizei _texWidth;
	GLsizei _texHeight;
	int _rotation;
	BOOL _forceRecalc;
	BOOL _running;
	BOOL _backgrounded;
	BOOL _shouldFill;
	NSLock *_lock;
	CADisplayLink* _displayLink;
	BOOL _newFrame;
	BOOL _firstFrame;
#if DBG_BGCOLOR
	GLfloat _clearClr[4];
#endif
}

@synthesize videoSize = _videoSize;
@synthesize fillRatio = _fillRatio;

+ (Class)layerClass
{
	return [CAEAGLLayer class];
}

- (void)setupLayer
{
	_eaglLayer = (CAEAGLLayer*) self.layer;
	_eaglLayer.opaque = YES;
}

- (void)setupContext
{
	EAGLRenderingAPI api = kEAGLRenderingAPIOpenGLES2;
	_context = [[EAGLContext alloc] initWithAPI:api];
	if (!_context) {
		NSLog(@"Failed to initialize OpenGLES 2.0 context");
		exit(1);
	}
 
	if (![EAGLContext setCurrentContext:_context]) {
		NSLog(@"Failed to set current OpenGL context");
		exit(1);
	}
}

- (void)setupRenderBuffer
{
	if (_colorRenderBuffer == 0) {
		glGenRenderbuffers(1, &_colorRenderBuffer);
	}
	glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);
	[_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:_eaglLayer];
}

- (void)setupFrameBuffer
{
	GLuint framebuffer;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		  GL_RENDERBUFFER, _colorRenderBuffer);
}

- (void)render:(CADisplayLink*)displayLink
{
	[_lock lock];
	if (!_running || _backgrounded) {
		[_lock unlock];
		return;
	}

	if (!_newFrame && !_forceRecalc) {
		[_lock unlock];
		return;
	}

	[EAGLContext setCurrentContext:_context];
#if DBG_BGCOLOR
	glClearColor(_clearClr[0], _clearClr[1], _clearClr[2], _clearClr[3]);
#else
	glClearColor(0.0, 0.0, 0.0, 1.0);
#endif
	glClear(GL_COLOR_BUFFER_BIT);

	//if (_forceRecalc)
	{
		int w = self.frame.size.width;
		int h = self.frame.size.height;

		glViewport(0, 0, w, h);
		[self setupVertices];
		_forceRecalc = NO;
	}

	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);

	[_context presentRenderbuffer:GL_RENDERBUFFER];
	_newFrame = NO;
	[_lock unlock];
}

- (void)startRunning
{
	[_lock lock];
	_running = YES;
	_firstFrame = NO;
	_forceRecalc = YES;
	[_lock unlock];

	_displayLink =
		[CADisplayLink displayLinkWithTarget:self selector:@selector(render:)];
	[_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
}

- (void)stopRunning
{
	_running = NO;
	[_displayLink removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
	_displayLink = nil;
}

- (GLuint) loadShader:(GLenum) shader_type fromSource:(const char*) shader_source
{
	GLuint shader = glCreateShader(shader_type);
	if (shader) {
		glShaderSource(shader, 1, &shader_source, NULL);
		glCompileShader(shader);
		
		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled) {
			GLint info_len = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
			if (info_len) {
				char* buf = (char*)malloc(info_len);
				glGetShaderInfoLog(shader, info_len, NULL, buf);
				error("%s: Could not compile shader %d: %s",
					__FUNCTION__, shader_type, buf);
				free(buf);
			}
			glDeleteShader(shader);
			shader = 0;
		}
	}
	return shader;
}

- (GLuint) createProgramFromVertex:(const char*)vertex_source
	fragment:(const char*)fragment_source
{
	GLuint vert = [self loadShader:GL_VERTEX_SHADER fromSource:vertex_source];
	if (!vert) {
		return -1;
	}
	
	GLuint frag = [self loadShader:GL_FRAGMENT_SHADER fromSource:fragment_source];
	if (!frag) {
		return -1;
	}
	
	GLuint program = glCreateProgram();
	if (program) {
		glAttachShader(program, vert);
		glAttachShader(program, frag);
		glLinkProgram(program);
		GLint link_status = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &link_status);
		if (link_status != GL_TRUE) {
			GLint info_len = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
			if (info_len) {
				char* buf = (char*)malloc(info_len);
				glGetProgramInfoLog(program, info_len, NULL, buf);
				error("%s: Could not link program: %s", __FUNCTION__, buf);
				free(buf);
			}
			glDeleteProgram(program);
			program = 0;
		}
	}
	
	if (vert) {
		glDeleteShader(vert);
	}
	
	if (frag) {
		glDeleteShader(frag);
	}
	
	return program;
}

- (void) setupProgram
{
	_program = [self createProgramFromVertex:vertShader fragment:fragShader];
	if (!_program) {
		return;
	}
	
	glUseProgram(_program);
	int i = glGetUniformLocation(_program, "Ytex");
	glUniform1i(i, 0);
	i = glGetUniformLocation(_program, "Utex");
	glUniform1i(i, 1);
	i = glGetUniformLocation(_program, "Vtex");
	glUniform1i(i, 2);
	
	return;
}

- (void) setupVertices
{
	CGRect frame = self.frame;
	GLfloat dw = (GLfloat)frame.size.width;
	GLfloat dh = (GLfloat)frame.size.height;
	
	GLfloat vw = (GLfloat)_texWidth;
	GLfloat vh = (GLfloat)_texHeight;

	GLfloat xscale = 1.0f;
	GLfloat yscale = 1.0f;

	if (dw == 0.0f || dh == 0.0f ||
	    _texWidth == 0 || _texHeight == 0) {
		return;
	}

	GLfloat da = dw / dh;
	GLfloat va = vw / vh;

	BOOL fill = _shouldFill;

	// 180 & 270 are double-flipped 0 & 90
	if (_rotation == 180 || _rotation == 270) {
		xscale = -1.0f;
		yscale = -1.0f;
	}

	// for 90 & 270 we should match width to height & vice versa
	if (_rotation == 90 || _rotation == 270) {
		va = 1.0f / va;
	}

	if (da / va < _fillRatio &&
	    va / da < _fillRatio) {
		fill = YES;
	}
	
	if (fill == (da > va)) {
		yscale *= da / va;
	}
	else {
		xscale *= va / da;
	}

	switch (_rotation) {
		case 0:
		case 180:
			_vertices[0]  = -xscale; // Top left
			_vertices[1]  =  yscale;
			_vertices[5]  =  xscale; // Top right
			_vertices[6]  =  yscale;
			_vertices[10] = -xscale; // Bottom left
			_vertices[11] = -yscale;
			_vertices[15] =  xscale; // Bottom right
			_vertices[16] = -yscale;
			break;
		case 90:
		case 270:
			_vertices[0]  =  xscale; // Top left
			_vertices[1]  =  yscale;
			_vertices[5]  =  xscale; // Top right
			_vertices[6]  = -yscale;
			_vertices[10] = -xscale; // Bottom left
			_vertices[11] =  yscale;
			_vertices[15] = -xscale; // Bottom right
			_vertices[16] = -yscale;
			break;
	}
	
	// Z values
	_vertices[2]  =  _vertices[7]  =  _vertices[12]  =  _vertices[17]  =  0.0f;
	
	// Texture coords
	_vertices[3]  =  0.0f; // Top left
	_vertices[4]  =  0.0f;
	_vertices[8]  =  1.0f; // Top right
	_vertices[9]  =  0.0f;
	_vertices[13] =  0.0f; // Bottom left
	_vertices[14] =  1.0f;
	_vertices[18] =  1.0f; // Bottom right
	_vertices[19] =  1.0f;

	int position_handle = glGetAttribLocation(_program, "aPosition");
	int texture_handle = glGetAttribLocation(_program, "aTextureCoord");
	int posStride = 5 * sizeof(GLfloat);
	
	glVertexAttribPointer(position_handle, 3, GL_FLOAT, false, posStride, _vertices);
	glEnableVertexAttribArray(position_handle);
	
	glVertexAttribPointer(texture_handle, 2, GL_FLOAT, false, posStride, &_vertices[3]);
	glEnableVertexAttribArray(texture_handle);
}

- (void) setupTexture:(int)name id:(int)id width:(int)width height:(int) height
{
	glActiveTexture(name);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
		width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
}

- (void) setupTextures:(struct avs_vidframe*) frame
{
	const GLsizei width = (GLsizei)frame->ys;
	const GLsizei height = frame->h;
	
	if (!_texIds[0]) {
		glGenTextures(3, _texIds);
	}
	
	[self setupTexture:GL_TEXTURE0 id:_texIds[0] width:width height:height];
	[self setupTexture:GL_TEXTURE1 id:_texIds[1] width:width/2 height:height/2];
	[self setupTexture:GL_TEXTURE2 id:_texIds[2] width:width/2 height:height/2];
	
	_texWidth = width;
	_texHeight = height;

}

- (BOOL) handleFrame:(struct avs_vidframe*) frame
{
	BOOL sizeChanged = NO;

	[_lock lock];
	if (_firstFrame) {
		info("handleFrame firstFrame run: %s bg: %s sz: %dx%d\n", _running ? "yes" : "no",
			_backgrounded ? "yes" : "no", frame->w, frame->h);
		_firstFrame = NO;
	}

	[EAGLContext setCurrentContext:_context];
	if (!_running || _backgrounded) {
		goto out;
	}
	
	if (_texWidth != (GLsizei)frame->ys || _texHeight != frame->h) {
		[self setupTextures:frame];
		_forceRecalc = YES;
		sizeChanged = YES;
	}
	
	if (_rotation != frame->rotation) {
		_rotation = frame->rotation;
		_forceRecalc = YES;
		sizeChanged = YES;
	}

	if (sizeChanged) {
		switch(frame->rotation) {
		case 90:
		case 270:
			_videoSize = CGSizeMake(frame->h, frame->w);
			break;
		case 0:
		case 180:
		default:
			_videoSize = CGSizeMake(frame->w, frame->h);
			break;
		}
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _texIds[0]);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
		(GLsizei)frame->ys, frame->h, GL_LUMINANCE, GL_UNSIGNED_BYTE,
		(const GLvoid*)frame->y);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, _texIds[1]);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
		(GLsizei)frame->us, frame->h / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
		(const GLvoid*)frame->u);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, _texIds[2]);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
		(GLsizei)frame->vs, frame->h / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
		(const GLvoid*)frame->v);

	_newFrame = YES;

out:
	[_lock unlock];

	return sizeChanged;
}

- (void)layoutSubviews
{
	[self setupRenderBuffer];

	[_lock lock];
	_forceRecalc = YES;
	[_lock unlock];
	
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)interfaceOrientation
{
	return YES;
}

- (void) setShouldFill:(BOOL)fill
{
	[_lock lock];
	_shouldFill = fill;
	_forceRecalc = YES;
	[_lock unlock];
}

- (BOOL) shouldFill
{
	return _shouldFill;
}

- (id)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (self) {
		GLfloat scale = [UIScreen mainScreen].scale;
		glViewport(0, 0, frame.size.width * scale, frame.size.height * scale);
	}
	return self;
}

- (id)init
{
	self = [super init];
	if (self) {
		_lock = [[NSLock alloc] init];
		[_lock lock];
		[self setupLayer];
		[self setupContext];
		[self setupRenderBuffer];
		[self setupFrameBuffer];
		[self setupProgram];
		
		self.autoresizingMask =
			UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
		_firstFrame = YES;

		_fillRatio = 0.0f;
#if DBG_BGCOLOR
		static int cnt = 0;
		GLfloat clrs[][4] = {
			{1.0, 0.0, 0.0, 1.0},
			{0.0, 1.0, 0.0, 1.0},
			{0.0, 0.0, 1.0, 1.0},
			{1.0, 1.0, 0.0, 1.0},
			{1.0, 0.0, 1.0, 1.0},
			{0.0, 1.0, 1.0, 1.0},
			{1.0, 1.0, 1.0, 1.0}};

		memcpy(_clearClr, &clrs[cnt % 7], 4 * sizeof(GLfloat));
		cnt++;
#endif
		[_lock unlock];

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
	}
	return self;
}

-(void)dealloc
{
#if TARGET_OS_IPHONE
	[[NSNotificationCenter defaultCenter] removeObserver:self
	      name:UIApplicationWillResignActiveNotification 
	      object:NULL];
	[[NSNotificationCenter defaultCenter] removeObserver:self
	      name:UIApplicationDidBecomeActiveNotification
	      object:NULL];
#endif
}

- (void)didMoveToWindow
{
	AVSFlowManager *fm = [AVSFlowManager getInstance];

	info("didMoveToWindow self: %p window: %p flowmgr %p\n", self, self.window, fm);
	if (fm) {
		if (self.window) {
			[fm attachVideoView: self];
		}
		else {
			[fm detachVideoView: self];
		}
	}

	if (self.window) {
		[self startRunning];
	}
	else {
		[self stopRunning];
	}
}

- (void)applicationWillResignActive:(NSNotification *)notification
{
	[_lock lock];
	_backgrounded = YES;
	[_lock unlock];
}

- (void)applicationDidBecomeActive:(NSNotification *)notification;
{
	[_lock lock];
	_backgrounded = NO;
	[_lock unlock];
}

@end

