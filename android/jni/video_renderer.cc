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


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>

#include "re.h"

#include "avs.h"

#include "video_renderer.h"

#ifdef ANDROID

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>



// GL_TRIANGLES
//static const char g_indices[] = { 0, 3, 2, 0, 2, 1 };
static const char g_indices[] = { 0, 1, 2, 3 };
static const GLfloat g_vertices[20] = {
    // X, Y, Z, U, V
    -1, -1, 0, 0, 1, // Bottom Left
     1, -1, 0, 1, 1, // Bottom Right
     1,  1, 0, 1, 0, // Top Right
    -1,  1, 0, 0, 0  // Top Left
};

static const char g_vertex_shader[] = {
	"attribute vec4 aPosition;\n"
	"attribute vec2 aTextureCoord;\n"
	"varying vec2 vTextureCoord;\n"
	"void main() {\n"
	"  gl_Position = aPosition;\n"
	"  vTextureCoord = aTextureCoord;\n"
	"}\n"
};

static const char g_fragment_shader[] = {
	"precision mediump float;\n"
	"uniform sampler2D Ytex;\n"
	"uniform sampler2D Utex,Vtex;\n"
	"varying vec2 vTextureCoord;\n"
	"void main(void) {\n"
	"  float nx,ny,r,g,b,y,u,v;\n"
	"  mediump vec4 txl,ux,vx;"
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
	"}\n"
};

/* Do YUV to RGB565 conversion and apply alpha mask */
static const char g_fragment_shader_masked[] = {
	"precision mediump float;\n"
	"uniform sampler2D Ytex;\n"
	"uniform sampler2D Utex,Vtex;\n"
	"uniform sampler2D Mtex;\n"
	"varying vec2 vTextureCoord;\n"
	"void main(void) {\n"
	"  float nx,ny,r,g,b,y,u,v,m;\n"
	"  mediump vec4 txl,ux,vx;"
	"  nx=vTextureCoord[0];\n"
	"  ny=vTextureCoord[1];\n"
	"  y=texture2D(Ytex,vec2(nx,ny)).r;\n"
	"  u=texture2D(Utex,vec2(nx,ny)).r;\n"
	"  v=texture2D(Vtex,vec2(nx,ny)).r;\n"
	"  m=texture2D(Mtex,vec2(nx,ny)).r;\n"
	"  u=u-0.5;\n"
	"  v=v-0.5;\n"
	"  r=y+1.403*v;\n"
	"  g=y-0.344*u-0.714*v;\n"
	"  b=y+1.770*u;\n"
	"  gl_FragColor=vec4(r,g,b,m);\n"
	"}\n"
};

struct {
	struct lock *lock;
	bool inited;
} vir = {
	.lock = NULL,
	.inited = false,
};

struct video_renderer {
	bool inited;
	bool rounded;
	bool should_fill;
	float fill_ratio;
	bool needs_recalc;
	bool use_mask;
	int w;
	int h;
	GLuint program;
	struct {
		GLuint ids[3];
		GLuint mask_id;
		GLsizei w;
		GLsizei h;
		int rotation;
	} tex;

	GLfloat vertices[20];

	char *userid;

	void *arg;
};


static void check_gl(const char* op) {
	for (GLint error = glGetError(); error; error = glGetError()) {
		//warning("glError(0x%x) after %s()\n", error, op);
	}
}


static void print_gl(const char *name, GLenum s)
{
	const char *v = (const char *) glGetString(s);

	(void)v;
	//debug("avs-video_renderer: GL %s = %s\n", name, v);
}


static void init()
{
	int err;

	err = lock_alloc(&vir.lock);
	if (err)
		return;

	vir.inited = true;
}


static void init_tex(int name, int id, int width, int height)
{
	glActiveTexture(name);
	glBindTexture(GL_TEXTURE_2D, id);
  
	// AVS: change min filter to linear to make smoother
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
}


void vr_destructor(void *arg)
{
	struct video_renderer *vr = (struct video_renderer *)arg;

	vr->arg = NULL;
	mem_deref(vr->userid);
}

static GLuint load_shader(GLenum shader_type, const char* src)
{
	GLuint shader = glCreateShader(shader_type);
	GLint compiled = 0;
	GLint infolen = 0;

	if (!shader)
		return 0;
	
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled)
		return shader;

	/* print some useful debug info */
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infolen);
	if (infolen) {
		char* buf = (char*) malloc(infolen);
		if (buf) {
			glGetShaderInfoLog(shader, infolen, NULL, buf);
			//warning("%s: Could not compile shader %d: %s",
			//	__FUNCTION__, shader_type, buf);
			free(buf);
		}
		glDeleteShader(shader);
	}
	return 0;
}


static GLuint create_program(const char *vertex_src,
			     const char *frag_src)
{
	GLuint vertex_shader = load_shader(GL_VERTEX_SHADER, vertex_src);
	GLuint pixel_shader = load_shader(GL_FRAGMENT_SHADER, frag_src);
	GLuint program;
	GLint link_status = GL_FALSE;
	GLint buflen = 0;
	
	if (!vertex_shader)
		return 0;
	if (!pixel_shader)
		return 0;

	program = glCreateProgram();
	if (!program)
		return 0;
	
	glAttachShader(program, vertex_shader);
	check_gl("glAttachShader");
	glAttachShader(program, pixel_shader);
	check_gl("glAttachShader");
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status == GL_TRUE)
		return program;
		
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &buflen);
	if (buflen) {
		char* buf = (char*) malloc(buflen);
		if (buf) {
			glGetProgramInfoLog(program, buflen, NULL, buf);
			//warning("%s: failed link program: %s",
			//	__FUNCTION__, buf);
			free(buf);
		}
	}
	glDeleteProgram(program);
	return 0;
}


static int setup_vertices(struct video_renderer *vr, int rotation)
{	
	GLfloat vw = (GLfloat)vr->w;
	GLfloat vh = (GLfloat)vr->h;
	
	GLfloat fw = (GLfloat)vr->tex.w;
	GLfloat fh = (GLfloat)vr->tex.h;

	GLfloat xscale = 1.0f;
	GLfloat yscale = 1.0f;

	int pos;
	int tex;
	
	int err = 0;

	if (vr->w == 0 || vr->h == 0 ||
	    vr->tex.w == 0 || vr->tex.h == 0) {
		return 0;
	}

	GLfloat va = vw / vh;
	GLfloat fa = fw / fh;

	bool fill = vr->should_fill;

	// 180 & 270 are double-flipped 0 & 90
	if (rotation == 180 || rotation == 270) {
		xscale = -1.0f;
		yscale = -1.0f;
	}

	// for 90 & 270 we should match width to height & vice versa
	if (rotation == 90 || rotation == 270) {
		fa = 1.0f / fa;
	}
	
	if (fa / va < vr->fill_ratio &&
	    va / fa < vr->fill_ratio) {
		fill = true;
	}
	
	if (fill == (va > fa)) {
		yscale *= va / fa;
	}
	else {
		xscale *= fa / va;
	}

#if 0
	info("setup_vertices: view(%fx%f)%f frame(%fx%f)%f scale(%fx%f) fill %s\n",
	      vw, vh, va, fw, fh, fa, xscale, yscale, fill ? "YES" : "NO");
#endif


	switch (rotation) {
	case 0:
	case 180:	
		vr->vertices[0]  = -xscale; // Top left
		vr->vertices[1]  =  yscale;
		vr->vertices[5]  =  xscale; // Top right
		vr->vertices[6]  =  yscale;
		vr->vertices[10] = -xscale; // Bottom left
		vr->vertices[11] = -yscale;
		vr->vertices[15] =  xscale; // Bottom right
		vr->vertices[16] = -yscale;
		break;

	case 90:
	case 270:
		vr->vertices[0]  =  xscale; // Top left
		vr->vertices[1]  =  yscale;
		vr->vertices[5]  =  xscale; // Top right
		vr->vertices[6]  = -yscale;
		vr->vertices[10] = -xscale; // Bottom left
		vr->vertices[11] =  yscale;
		vr->vertices[15] = -xscale; // Bottom right
		vr->vertices[16] = -yscale;
		break;
	}

	vr->vertices[2]  = 0.0f;
	vr->vertices[7]  = 0.0f;
	vr->vertices[12] = 0.0f;
	vr->vertices[17] = 0.0f;

	vr->vertices[3]  =  0.0f; // Top left
	vr->vertices[4]  =  0.0f;
	vr->vertices[8]  =  1.0f; // Top right
	vr->vertices[9]  =  0.0f;
	vr->vertices[13] =  0.0f; // Bottom left
	vr->vertices[14] =  1.0f;
	vr->vertices[18] =  1.0f; // Bottom right
	vr->vertices[19] =  1.0f;
	
	pos = glGetAttribLocation(vr->program, "aPosition");
	check_gl("glGetAttribLocation aPosition");
	if (pos == -1) {
		//warning("%s: Could not get aPosition handle", __FUNCTION__);
		err = EBADF;
		goto out;
	}

	tex = glGetAttribLocation(vr->program, "aTextureCoord");
	check_gl("glGetAttribLocation aTextureCoord");
	if (tex == -1) {
		//warning("%s: Could not get aTextureCoord handle",
		//	__FUNCTION__);
		err = EBADF;
		goto out;
	}

	/* set the vertices array in the shader
	 * _vertices contains 4 vertices with 5 coordinates.
	 * 3 for (xyz) for the vertices and 2 for the texture
	 */
	glVertexAttribPointer(pos, 3, GL_FLOAT, false,
			      5 * sizeof(GLfloat), vr->vertices);
	check_gl("glVertexAttribPointer aPosition");

	glEnableVertexAttribArray(pos);
	check_gl("glEnableVertexAttribArray positionHandle");

	/* set the texture coordinate array in the shader
	 * _vertices contains 4 vertices with 5 coordinates.
	 * 3 for (xyz) for the vertices and 2 for the texture
	 */
	glVertexAttribPointer(tex, 2, GL_FLOAT, false, 5
			      * sizeof(GLfloat), &vr->vertices[3]);
	
	check_gl("glVertexAttribPointer maTextureHandle");
	glEnableVertexAttribArray(tex);
	check_gl("glEnableVertexAttribArray textureHandle");

 out:
	return err;
	
#if 0
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
#endif
}


int video_renderer_alloc(struct video_renderer **vrp,  int w, int h,
			 bool rounded,
			 const char *userid, void *arg)
{
	struct video_renderer *vr;
	int err = 0;

#if 0
	debug("%s: width %d, height %d rounded %d",
	      __FUNCTION__, w, h, rounded);
#endif

	if (!vir.inited)
		init();

	vr = (struct video_renderer *)mem_zalloc(sizeof(*vr), vr_destructor);
	if (vr == NULL)
		return ENOMEM;

	vr->tex.w = -1;
	vr->tex.h = -1;

	vr->rounded = rounded;
	vr->should_fill = true;
	vr->fill_ratio = 0.0f;
	vr->needs_recalc = false;
	vr->inited = false;
	vr->w = w;
	vr->h = h;
	str_dup(&vr->userid, userid);
	vr->arg = arg;

	if (err)
		mem_deref(vr);
	else {
		if (vrp)
			*vrp = vr;
	}

	return err;
}

void video_renderer_detach(struct video_renderer *vr)
{
	if (!vr)
		return;

	vr->arg = NULL;
}


static int renderer_init(struct video_renderer *vr)
{
	int max_tex_units[2];
	int max_tex_sizes[2];
	int ret = 0;
	int i;
	int err = 0;
  	
	//lock_write_get(vir.lock);
  
	print_gl("Version", GL_VERSION);
	print_gl("Vendor", GL_VENDOR);
	print_gl("Renderer", GL_RENDERER);
	print_gl("Extensions", GL_EXTENSIONS);
  
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, max_tex_units);
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, max_tex_sizes);

#if 0
	debug("%s: number of textures %d, size %d", __FUNCTION__,
	      (int)max_tex_units[0], (int)max_tex_sizes[0]);
#endif
	
	if (vr->rounded) {
		vr->use_mask = true;
		vr->program = create_program(g_vertex_shader,
					     g_fragment_shader_masked);
	}
	else {
		vr->use_mask = false;
		vr->program = create_program(g_vertex_shader,
					     g_fragment_shader);
	}
  
	if (!vr->program) {
		//warning("%s: Could not create program", __FUNCTION__);
		err = ENOSYS;
		goto out;
	}

	glUseProgram(vr->program);
	i = glGetUniformLocation(vr->program, "Ytex");
	check_gl("glGetUniformLocation");
	glUniform1i(i, 0); /* Bind Ytex to texture unit 0 */
	check_gl("glUniform1i Ytex");

	i = glGetUniformLocation(vr->program, "Utex");
	check_gl("glGetUniformLocation Utex");
	
	glUniform1i(i, 1); /* Bind Utex to texture unit 1 */
	check_gl("glUniform1i Utex");

	i = glGetUniformLocation(vr->program, "Vtex");
	check_gl("glGetUniformLocation");
	
	glUniform1i(i, 2); /* Bind Vtex to texture unit 2 */
	check_gl("glUniform1i");
	
	if (vr->use_mask) {
		i = glGetUniformLocation(vr->program, "Mtex");
		check_gl("glGetUniformLocation");
		glUniform1i(i, 3); /* Bind Mtex to texture unit 2 */
		check_gl("glUniform1i");
	}
  
	glViewport(0, 0, vr->w, vr->h);
	check_gl("glViewport");

 out:
	//lock_rel(vir.lock);

	if (!err)
		vr->inited = true;
  
	return err;	
}

void *video_renderer_arg(struct video_renderer *vr)
{
	return vr ? vr->arg : NULL;
}


/* Uploads a plane of pixel data, accounting for stride != width*bpp. */
static void copy_tex(GLsizei width, GLsizei height, int stride,
		     const uint8_t *plane)
{
	if (stride == width) {
		/* We can upload the entire plane in a single GL call. */
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
				GL_LUMINANCE,
				GL_UNSIGNED_BYTE,
				(GLvoid *)plane);
	} else {
		/* GLES2 doesn't have GL_UNPACK_ROW_LENGTH and Android doesn't
		 * have GL_EXT_unpack_subimage we have to upload
		 * a row at a time.
		 */
		for (int row = 0; row < height; ++row) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, width,
					1, GL_LUMINANCE,
					GL_UNSIGNED_BYTE,
					(GLvoid *)(plane + (row * stride)));
		}
	}
}


static void update_textures(struct video_renderer *vr, struct avs_vidframe *vf)
{
	const GLsizei width = vf->w;
	const GLsizei height = vf->h;

#if 0
	debug("update_textures: view:%dx%d frame:%dx%d\n",
	      vr->w, vr->h, vf->w, vf->h);
#endif
	
	/* Y plane */
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, vr->tex.ids[0]);
	copy_tex(width, height, vf->ys, vf->y);
	check_gl("UpdateTextures Y");

	/* U plane */
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, vr->tex.ids[1]);
	copy_tex(width/2, height/2, vf->us, vf->u);
	check_gl("UpdateTextures U");

	/* V plane */
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, vr->tex.ids[2]);
	copy_tex(width/2, height/2, vf->vs, vf->v);
	check_gl("UpdateTextures V");
}


static void setup_mask_tex(struct video_renderer *vr, int width, int height)
{
	int hw = width / 2;
	int hh = height / 2;
	int r2 = hw < hh ? hw : hh;
	uint8_t *buf;
	uint8_t *p;
	
	r2 = r2 * r2; 
 
	buf = (uint8_t*)malloc(width * height);
	p = buf;
 
	for (int y = 0; y < height; y++) {
		int dy2 = (y - hh) * (y - hh);
		for (int x = 0; x < width; x++) {
			int dx2 = (x - hw) * (x - hw);
			/* TODO: anti-alias this */
			*p++ = (dx2 + dy2 < r2) ? 255 : 0;
		}   
	}

	/* generate  the mask texture */
	glGenTextures(1, &vr->tex.mask_id); 
	init_tex(GL_TEXTURE3, vr->tex.mask_id, width, height);
 
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, vr->tex.mask_id);
	copy_tex(width, height, width, buf);

	free(buf);
}	


static void setup_textures(struct video_renderer *vr, struct avs_vidframe *vf)
{
	const GLsizei w = (GLsizei)vf->w;
	const GLsizei h = (GLsizei)vf->h;

#if 0
	debug("%s: width %d, height %d rot=%d",
	      __FUNCTION__, w, h, vf->rotation);
#endif

	vr->tex.w = vf->w;
	vr->tex.h = vf->h;
	vr->tex.rotation = vf->rotation;
	
	setup_vertices(vr, vf->rotation);
	
	/* generate  the Y, U and V textures */
	glGenTextures(3, vr->tex.ids);
	init_tex(GL_TEXTURE0, vr->tex.ids[0], w, h);
	init_tex(GL_TEXTURE1, vr->tex.ids[1], w/2, h/2);
	init_tex(GL_TEXTURE2, vr->tex.ids[2], w/2, h/2);

	check_gl("setup_textures");

	if (vr->use_mask)
		setup_mask_tex(vr, vr->tex.w, vr->tex.h);
}


int video_renderer_handle_frame(struct video_renderer *vr,
				struct avs_vidframe *vf)
{
	if (!vr || !vf)
		return EINVAL;

	if (!vr->inited)
		renderer_init(vr);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (vr->tex.w != vf->w
	    || vr->tex.h != vf->h
	    || vr->tex.rotation != vf->rotation
	    || vr->needs_recalc){
		vr->needs_recalc = false;
		setup_textures(vr, vf);
	}
	
	update_textures(vr, vf);

	if (vr->use_mask) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, g_indices);

	check_gl("glDrawArrays");

	if (vr->use_mask)
		glDisable(GL_BLEND);

	return 0;
}

void video_renderer_set_should_fill(struct video_renderer *vr,
				    bool should_fill)
{
	if (vr) {
		vr->should_fill = should_fill;
		vr->needs_recalc = true;
	}
}

void video_renderer_set_fill_ratio(struct video_renderer *vr,
				   float fill_ratio)
{
	if (vr) {
		vr->fill_ratio = fill_ratio;
		vr->needs_recalc = true;
	}
}

const char *video_renderer_userid(struct video_renderer *vr)
{
	return vr ? vr->userid : NULL;
}

#else


struct video_renderer {
	void *arg;
};


int video_renderer_alloc(struct video_renderer **vrp,  int w, int h,
			 bool rounded, void *arg)
{
	struct video_renderer *vr;

	vr = (struct video_renderer *)mem_zalloc(sizeof(*vr), NULL);
	if (!vr)
		return ENOMEM;

	vr->arg = arg;
	
	if (vrp)
		*vrp = vr;
	
	return 0;
}

int video_renderer_handle_frame(struct video_renderer *vr,
				struct avs_vidframe *vf)
{
	return 0;
}

void *video_renderer_arg(struct video_renderer *vr)
{
	return vr ? vr->arg : NULL;
}

void video_renderer_set_should_fill(struct video_renderer *vr,
				    bool should_fill)
{
}

void video_renderer_set_fill_ratio(struct video_renderer *vr,
				   float fill_ratio)
{
}

const char *video_renderer_userid(struct video_renderer *vr)
{
	return NULL;
}

#endif
