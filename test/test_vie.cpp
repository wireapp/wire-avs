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
#include <re.h>
#include <avs.h>
#include <avs_vie.h>
#include <gtest/gtest.h>
#include "ztest.h"
#include "webrtc/base/logging.h"


TEST(vie, basic_init_close)
{
	struct list vidcodecl = LIST_INIT;
	int err;

	err = vie_init(&vidcodecl);
	ASSERT_EQ(0, err);
	
	ASSERT_GE(list_count(&vidcodecl), 1);

	vie_close();

	ASSERT_EQ(0, list_count(&vidcodecl));
}


TEST(vie, extra_close)
{
	vie_close();
}


TEST(vie, close_twice)
{
	struct list vidcodecl = LIST_INIT;
	int err;

	err = vie_init(&vidcodecl);
	ASSERT_EQ(0, err);

	vie_close();
	vie_close();
}


#define WIDTH  180
#define HEIGHT 240
#define FPS     15


#define NUM_FRAMES 5


class Vie : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		int err;

#if 1
		rtc::LogMessage::SetLogToStderr(false);
#endif

		tmr_init(&tmr);

		err = vie_init(&vidcodecl);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		mem_deref(vds);

		tmr_cancel(&tmr);
		vie_close();
	}

	/* NOTE: called from Webrtc worker thread */
	static int videnc_rtp_handler(const uint8_t *pkt, size_t len, void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);
		const struct vidcodec *vc = viddec_get(test->vds);
		struct mbuf *mb = mbuf_alloc(len);
		struct rtp_header hdr;
		int err;

		mbuf_write_mem(mb, pkt, len);
		mb->pos = 0;

		err = rtp_hdr_decode(&hdr, mb);
		if (err) {
			warning("rtp decode failed (%m)\n", err);
		}
		else {
#if 0
			re_printf("rtp: ext=%d m=%d pt=%u"
				  " seq=%u ts=%u ssrc=%u\n",
				  hdr.ext, hdr.m, hdr.pt,
				  hdr.seq, hdr.ts, hdr.ssrc);
#endif

			if (hdr.m)
				++test->n_rtp_marker;
		}

		++test->n_rtp;
		test->n_rtp_bytes += len;

		vc->dec_rtph(test->vds, pkt, len);

		if (!test->ts_rtp_first)
			test->ts_rtp_first = tmr_jiffies();
		test->ts_rtp_last = tmr_jiffies();

		mem_deref(mb);

		return 0;
	}

	/* NOTE: called from Webrtc worker thread */
	static int videnc_rtcp_handler(const uint8_t *pkt,
				       size_t len, void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);
		const struct vidcodec *vc = viddec_get(test->vds);

		++test->n_rtcp;

		vc->dec_rtcph(test->vds, pkt, len);

		return 0;
	}

	static void videnc_err_handler(int err, const char *msg, void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);
		++test->n_enc_err;
	}

	static void viddec_err_handler(int err, const char *msg, void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);
		++test->n_dec_err;
	}

	/* NOTE: Called from RE-thread */
	static void frame_handler(void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);

		tmr_start(&test->tmr, 1000/FPS, frame_handler, test);

		test->send_frame();
	}

	void send_frame()
	{
		static uint8_t black[WIDTH * HEIGHT] = {0};

		struct avs_vidframe frame = {
			.type = AVS_VIDFRAME_I420,
			.y = black,
			.u = black,
			.v = black,
			.ys = WIDTH,
			.us = WIDTH/2,
			.vs = WIDTH/2,
			.w = WIDTH,
			.h = HEIGHT,
			.rotation = 0,
			.ts = 0       /* ignored by encoder */
		};

		if (!ts_send_first)
			ts_send_first = tmr_jiffies();
		ts_send_last = tmr_jiffies();

		vie_capture_router_handle_frame(&frame);

		++n_frame_sent;
	}

	static void video_state_change_handler(const char *userid,
					       enum vie_renderer_state state,
					       void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);

		test->last_state = state;
	}

	static void render_frame(struct avs_vidframe *frame, void *arg)
	{
		Vie *test = static_cast<Vie *>(arg);

		if (!test->ts_recv_first)
			test->ts_recv_first = tmr_jiffies();
		test->ts_recv_last = tmr_jiffies();

		static uint64_t ts_prev;
		int delta = 0;

		if (ts_prev)
			delta = tmr_jiffies()-ts_prev;

#if 0
		re_printf("[%4d] render_frame: %d x %d\n",
			  delta, frame->w, frame->h);
#endif

		ASSERT_EQ(AVS_VIDFRAME_I420, frame->type);
		ASSERT_TRUE(frame->y != NULL);
		ASSERT_TRUE(frame->u != NULL);
		ASSERT_TRUE(frame->v != NULL);
		ASSERT_EQ(WIDTH, frame->ys);
		ASSERT_EQ(WIDTH/2, frame->us);
		ASSERT_EQ(WIDTH/2, frame->vs);
		ASSERT_EQ(WIDTH, frame->w);
		ASSERT_EQ(HEIGHT, frame->h);
		ASSERT_EQ(0, frame->rotation);
		ASSERT_TRUE(frame->ts > 0);

		++test->n_frame_recv;

		ts_prev = tmr_jiffies();

		/* Exit criteria for test */
		if (test->n_frame_recv >= NUM_FRAMES) {
			re_cancel();
		}
	}

	static int render_frame_handler(struct avs_vidframe *frame, const char *userid, void *arg)
	{
		render_frame(frame, arg);

		return 0;
	}
	

protected:
	struct list vidcodecl = LIST_INIT;
	struct tmr tmr;
	struct viddec_state *vds = nullptr;
	enum vie_renderer_state last_state =
		VIE_RENDERER_STATE_STOPPED;

	unsigned n_rtp = 0;
	size_t n_rtp_bytes = 0;
	unsigned n_rtcp = 0;
	unsigned n_enc_err = 0;
	unsigned n_dec_err = 0;
	unsigned n_frame_sent = 0;
	unsigned n_frame_recv = 0;
	uint64_t ts_send_first = 0;
	uint64_t ts_send_last = 0;
	uint64_t ts_recv_first = 0;
	uint64_t ts_recv_last = 0;
	uint64_t ts_rtp_first = 0;
	uint64_t ts_rtp_last = 0;
	unsigned n_rtp_marker = 0;
};


TEST_F(Vie, encode_decode_loop)
{
#define SSRC_A 0x00000001
#define SSRC_B 0x00000002
#define PT 100
	const struct vidcodec *vc;
	struct videnc_state *ves = NULL;
	struct media_ctx *mctx1 = NULL;
	struct media_ctx *mctx2 = NULL;
	int err;
	struct vidcodec_param param_enc = {
		.local_ssrcv = {SSRC_A, 0},
		.local_ssrcc = 1,

		.remote_ssrcv = {SSRC_B, 0, 0, 0},
		.remote_ssrcc = 1,
	};
	struct vidcodec_param param_dec = {
		.local_ssrcv = {SSRC_B, 0},
		.local_ssrcc = 1,

		.remote_ssrcv = {SSRC_A, 0, 0, 0},
		.remote_ssrcc = 1,
	};

	vc = vidcodec_find(&vidcodecl, "VP8", NULL);
	ASSERT_TRUE(vc != NULL);

	ASSERT_TRUE(list_contains(&vidcodecl, &vc->le));
	//ASSERT_TRUE(vc->pt == NULL); XXX should be dynamic ?
	ASSERT_STREQ("VP8", vc->name);
	ASSERT_TRUE(vc->fmtp == NULL);
	ASSERT_TRUE(vc->data != NULL);

	err = vc->enc_alloch(&ves,
			     &mctx1,
			     vc,
			     "asd=123", PT,
			     NULL,
			     &param_enc,
			     videnc_rtp_handler,
			     videnc_rtcp_handler,
			     videnc_err_handler,
			     NULL,
			     this);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(ves != NULL);
	ASSERT_TRUE(mctx1 != NULL);

	err = vc->dec_alloch(&vds,
			     &mctx2,
			     vc,
			     NULL,
			     PT, // todo: which PT ?
			     NULL,
			     &param_dec,
			     viddec_err_handler,
			     NULL,
			     this);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(vds != NULL);
	ASSERT_TRUE(mctx2 != NULL);

	err = vc->enc_starth(ves, false);
	ASSERT_EQ(0, err);

	err = vc->dec_starth(vds, "A");
	ASSERT_EQ(0, err);

	vie_set_video_handlers(video_state_change_handler,
			       render_frame_handler, NULL, this);

	/* Start sending video frames */
	tmr_start(&tmr, 100, frame_handler, this);

	/* Start run-loop, wait for test to complete */
	err = re_main_wait(60000);
	ASSERT_EQ(0, err);

#if 0
	re_printf("frames sent %d (avg. framerate %.1f fps)\n",
		  n_frame_sent,
		  1000.0*n_frame_sent / (ts_send_last - ts_send_first));
	re_printf("frames received %d (avg. framerate %.1f fps)\n",
		  n_frame_recv,
		  1000.0*n_frame_recv / (ts_recv_last - ts_recv_first));
	re_printf("rtp bitrate: %.1f bits per sec\n",
		  8000.0*n_rtp_bytes / (ts_rtp_last - ts_rtp_first));
	re_printf("rtp marker bits: %u\n", n_rtp_marker);
	re_printf("rtcp packets:    %u\n", n_rtcp);
#endif

	/* Verify results after test is complete */
	ASSERT_GE(n_frame_sent, NUM_FRAMES);
	ASSERT_GE(n_frame_recv, NUM_FRAMES);
	ASSERT_GE(n_rtp, 4);
	ASSERT_GE(n_rtp_bytes, 1000);
	ASSERT_GE(n_rtcp, 1);
	ASSERT_EQ(0, n_enc_err);
	ASSERT_EQ(0, n_dec_err);
	ASSERT_EQ(VIE_RENDERER_STATE_RUNNING, last_state);

	/* DONE */
	mem_deref(ves);
}
