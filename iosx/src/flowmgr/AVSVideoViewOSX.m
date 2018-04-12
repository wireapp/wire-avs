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

#include <re/re.h>
#include <avs.h>

#import "iosx/include/AVSVideoViewOSX.h"
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

const char vertShader[] = {
	"attribute vec4 aPosition;\n"
	"attribute vec2 aTextureCoord;\n"
	"varying vec2 vTextureCoord;\n"
	"void main() {\n"
	"  gl_Position = aPosition;\n"
	"  vTextureCoord = aTextureCoord;\n"
	"}\n"};

const char fragShader[] = {
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

@implementation AVSVideoViewOSX
{
	NSOpenGLContext* _context;
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
	BOOL _newFrame;	
	BOOL _firstFrame;
	CVDisplayLinkRef _displayLink;
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
}

- (void) setupVertices
{
	CGRect frame = self.frame;
	GLfloat dw = (GLfloat)frame.size.width;
	GLfloat dh = (GLfloat)frame.size.height;
	GLfloat da = dw / dh;
	
	GLfloat vw = (GLfloat)_texWidth;
	GLfloat vh = (GLfloat)_texHeight;
	GLfloat va = vw / vh;

	GLfloat xscale = 1.0f;
	GLfloat yscale = 1.0f;

	// 180 & 270 are double-flipped 0 & 90
	if (_rotation == 180 || _rotation == 270) {
		xscale = -1.0f;
		yscale = -1.0f;
	}

	// for 90 & 270 we should match width to height & vice versa
	if (_rotation == 90 || _rotation == 270) {
		va = 1.0f / va;
	}

	if (_shouldFill == (da > va)) {
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
		warning("AVSVideoView handleFrame firstFrame run: %s bg: %s sz: %dx%d\n",
			_running ? "yes" : "no", _backgrounded ? "yes" : "no",
			frame->w, frame->h);
		_firstFrame = NO;
	}
	[_context makeCurrentContext];

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

	_rotation = frame->rotation;
	_newFrame = YES;
	[_lock unlock];

	return sizeChanged;
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

-(void)render:(NSTimer *)timer {

	if (_newFrame) {
		[self setNeedsDisplay:YES];
	}
}

- (void)startRunning
{
	CVDisplayLinkStart(_displayLink);
	_running = YES;
	_firstFrame = NO;
}

- (void)stopRunning
{
	_running = NO;
	CVDisplayLinkStop(_displayLink);
}

static CVReturn displayLinkCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* now,
	const CVTimeStamp* outputTime, CVOptionFlags flagsIn, CVOptionFlags* flagsOut, void* displayLinkContext)
{
	CVReturn result = [(__bridge AVSVideoViewOSX*)displayLinkContext getFrameForTime:outputTime];
	return result;
}

- (CVReturn)getFrameForTime:(const CVTimeStamp*)outputTime
{
	[_lock lock];

	if (!_running || !_newFrame) {
		goto out;
	}

	[_context makeCurrentContext];

	if (_forceRecalc) {
		NSRect frameRect = self.frame;
		glViewport(0, 0, frameRect.size.width, frameRect.size.height);
		[self setupVertices];
		_forceRecalc = NO;
	}

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);

	glSwapAPPLE();
	_newFrame = NO;

out:
	[_lock unlock];

	return kCVReturnSuccess;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
	NSOpenGLPixelFormatAttribute attrs[] = {
		NSOpenGLPFADoubleBuffer,
		NSOpenGLPFADepthSize, 24,
		NSOpenGLPFAOpenGLProfile,
		NSOpenGLProfileVersion3_2Core,
		0
	};

	NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];

	if (!pf)
	{
		error("%s: unable to create GL pixel format\n", __FUNCTION__);
	}

	_lock = [[NSLock alloc] init];
	[_lock lock];
	_context = self.openGLContext;
	[_context makeCurrentContext];
	[self setupProgram];
	glViewport(0, 0, frameRect.size.width, frameRect.size.height);
	_firstFrame = YES;

	CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
	CVDisplayLinkSetOutputCallback(_displayLink, &displayLinkCallback, (__bridge void *)self);

	[_lock unlock];
	return [super initWithFrame:frameRect pixelFormat:pf];
}

- (void)reshape
{
	_forceRecalc = YES;
}

- (void)viewDidMoveToWindow
{
	if (self.window) {
		[self startRunning];
	}
	else {
		[self stopRunning];
	}

	[super viewDidMoveToWindow];
}
- (void)dealloc
{
	CVDisplayLinkStop(_displayLink);
	CVDisplayLinkRelease(_displayLink);
#if !__has_feature(objc_arc)
	[super dealloc];
#endif
}

@end

