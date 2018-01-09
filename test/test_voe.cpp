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
#include <avs_voe.h>
#include <gtest/gtest.h>
#include <sys/time.h>
#include <re/re.h>
#include "avs_audio_io.h"
#include "webrtc/base/logging.h"
#include "ztest.h"


class Voe : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		int err;

#if 1
		rtc::LogMessage::SetLogToStderr(false);
#endif

		err = voe_init(&aucodecl);
		ASSERT_EQ(0, err);

		err = audio_io_alloc(&aio, AUDIO_IO_MODE_MOCK);
		ASSERT_EQ(0, err);
        
		err = audio_io_enable_sine(aio);
		ASSERT_EQ(0, err);
        
		voe_register_adm(aio);
	}

	virtual void TearDown() override
	{
		voe_deregister_adm();

		mem_deref(aio);
        
		voe_close();
	}

protected:
	struct list aucodecl = LIST_INIT;
	struct audio_io *aio;
};


#define EXPECTED_PACKETS 16
struct sync_state {
	pthread_mutex_t mutex;

	struct mqueue *mq;
	unsigned n_packet;
	uint8_t pt;
	uint32_t ssrc;
	uint16_t prev_seq;
	int32_t seq_diff;
	uint32_t prev_timestamp;
	int64_t timestamp_diff;
	uint16_t prev_pktsize;
	uint16_t max_pktsize_diff;
};


static void mqueue_handler(int id, void *data, void *arg)
{
	(void)id;
	(void)data;
	(void)arg;

	re_cancel();
}


static void init_sync_state(struct sync_state *pss)
{
	memset(pss, 0, sizeof(*pss));

	mqueue_alloc(&pss->mq, mqueue_handler, NULL);

	pthread_mutex_init(&pss->mutex,NULL);

	pss->prev_seq = 0;
	pss->seq_diff = 0;
	pss->prev_timestamp = 0;
	pss->timestamp_diff = 0;
	pss->max_pktsize_diff = 0;
}


TEST_F(Voe, basic_init_close)
{
	ASSERT_GE(list_count(&aucodecl), 1);

	voe_close();

	ASSERT_EQ(0, list_count(&aucodecl));
}


TEST_F(Voe, extra_close)
{
	voe_close();
}


TEST_F(Voe, close_twice)
{
	voe_close();
	voe_close();
}


TEST_F(Voe, enc_dec_alloc)
{
	struct aucodec_param prm;
	struct auenc_state *aesp = NULL;
	struct audec_state *adsp = NULL;
	const struct aucodec *ac;
	int err;

	memset(&prm, 0, sizeof(prm));

	ac = aucodec_find(&aucodecl, "opus", 48000, 2);
	ASSERT_TRUE(ac != NULL);

	if (ac->enc_alloc){
		err = ac->enc_alloc(&aesp, ac, NULL, &prm,
				    NULL, NULL, NULL, NULL, NULL);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(aesp != NULL);
	}

	if (ac->dec_alloc){
		err = ac->dec_alloc(&adsp, ac, NULL, &prm,
				    NULL, NULL, NULL);
		ASSERT_EQ(0, err);
		ASSERT_TRUE(adsp != NULL);
	}

	mem_deref(aesp);
	mem_deref(adsp);
}


TEST_F(Voe, unmute_after_call)
{
	struct aucodec_param prm;
	struct auenc_state *aesp = NULL;
	struct audec_state *adsp = NULL;
	const struct aucodec *ac;
	bool muted;
	int err;

	memset(&prm, 0, sizeof(prm));

	ac = aucodec_find(&aucodecl, "opus", 48000, 2);
	ASSERT_TRUE(ac != NULL);

	if (ac->enc_alloc){
		err = ac->enc_alloc(&aesp, ac, NULL, &prm,
				    NULL, NULL, NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}

	if (ac->dec_alloc){
		err = ac->dec_alloc(&adsp, ac, NULL, &prm,
				    NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}

	err = voe_get_mute(&muted);
	ASSERT_EQ(err, 0);
	ASSERT_EQ(muted, false);

	err = voe_set_mute(true);
	ASSERT_EQ(err, 0);

	err = voe_get_mute(&muted);
	ASSERT_EQ(err, 0);
	ASSERT_EQ(muted, true);

	mem_deref(aesp);
	mem_deref(adsp);

	err = voe_get_mute(&muted);
	ASSERT_EQ(err, 0);
	ASSERT_EQ(muted, false);
}


/* NOTE: called from a WebRTC thread */
static int send_rtp_handler(const uint8_t *pkt, size_t len, void *arg)
{
	struct sync_state* pss = (struct sync_state*)arg;
	struct mbuf *mb = mbuf_alloc(len);
	struct rtp_header hdr;
	bool complete = false;
	int err = 0;

	mbuf_write_mem(mb, pkt, len);
	mb->pos = 0;

	err = rtp_hdr_decode(&hdr, mb);
	if (err)
		goto out;

	pthread_mutex_lock(&pss->mutex);

	++pss->n_packet;

	if (pss->n_packet <= EXPECTED_PACKETS) {

		pss->pt = hdr.pt;
		pss->ssrc = hdr.ssrc;
		pss->seq_diff = hdr.seq - pss->prev_seq;
		if (pss->seq_diff < 0) {
			pss->seq_diff += 0xffff;
		}
		pss->prev_seq = hdr.seq;
		pss->timestamp_diff = hdr.ts - pss->prev_timestamp;
		if (pss->timestamp_diff < 0) {
			pss->timestamp_diff += 0xfffffff;
		}
		pss->prev_timestamp = hdr.ts;
		if(pss->prev_pktsize > 0){
			int16_t diff = len - pss->prev_pktsize;
			diff = std::abs(diff);
			pss->max_pktsize_diff = std::max(pss->max_pktsize_diff, (uint16_t)diff);
		}
		pss->prev_pktsize = (uint16_t)len;
	}
	else {
		info("ignore rtp packet\n");
	}

	if (pss->n_packet >= EXPECTED_PACKETS) {
		complete = true;
	}

	pthread_mutex_unlock(&pss->mutex);

	if (complete)
		mqueue_push(pss->mq, 0, NULL);

 out:
	mem_deref(mb);

	return err;
}


TEST_F(Voe, enc_dec_alloc_start_stop)
{
	struct auenc_state *aesp = NULL;
	struct audec_state *adsp = NULL;
	struct media_ctx *mctxp = NULL;
	const struct aucodec *ac;
	struct sync_state ss;
	struct aucodec_param prm;
	int pt = 96;
	int srate = 48000;
	int err;

	init_sync_state(&ss);

	memset(&prm, 0, sizeof(prm));
	prm.local_ssrc = 0x12345678;
	prm.pt = 96;
	prm.srate = 48000;
	prm.ch = 2;

	ac = aucodec_find(&aucodecl, "opus", 48000, 2);
	if (ac->enc_alloc){
		err = ac->enc_alloc(&aesp, ac, NULL, &prm,
				    send_rtp_handler, NULL, NULL, NULL, &ss);
		ASSERT_EQ(0, err);
	}

	if (ac->dec_alloc){
		err = ac->dec_alloc(&adsp, ac, NULL, &prm,
				    NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}

	if (ac->enc_start){
		ac->enc_start(aesp, false, NULL, &mctxp);
	}

	if (ac->dec_start){
		ac->dec_start(adsp, &mctxp);
	}

	err = re_main_wait(30000);
	ASSERT_EQ(0, err);

	ASSERT_GE(ss.n_packet, EXPECTED_PACKETS);
	ASSERT_EQ(pt, ss.pt);
	ASSERT_EQ(prm.local_ssrc, ss.ssrc);
	ASSERT_EQ(1, ss.seq_diff);
	ASSERT_EQ(960, ss.timestamp_diff);

	if (ac->enc_stop){
		ac->enc_stop(aesp);
	}

	if (ac->dec_stop){
		ac->dec_stop(adsp);
	}

	mem_deref(aesp);
	mem_deref(adsp);

	mem_deref(ss.mq);
}


#if 0
TEST_F(Voe, cbr_off)
{
    struct auenc_state *aesp = NULL;
    struct audec_state *adsp = NULL;
    struct media_ctx *mctxp = NULL;
    const struct aucodec *ac;
    struct sync_state ss;
    struct aucodec_param prm;
    int pt = 96;
    int srate = 48000;
    int err;
    
    init_sync_state(&ss);
    
    memset(&prm, 0, sizeof(prm));
    prm.local_ssrc = 0x12345678;
    prm.pt = 96;
    prm.srate = 48000;
    prm.ch = 2;
    
    ac = aucodec_find(&aucodecl, "opus", 48000, 2);
    if (ac->enc_alloc){
        err = ac->enc_alloc(&aesp, ac, NULL, &prm,
                            send_rtp_handler, NULL, NULL, NULL, &ss);
        ASSERT_EQ(0, err);
    }
    
    if (ac->dec_alloc){
        err = ac->dec_alloc(&adsp, ac, NULL, &prm,
                            NULL, NULL, NULL);
        ASSERT_EQ(0, err);
    }
    
    voe_enable_cbr(false);
    
    if (ac->enc_start){
        ac->enc_start(aesp, &mctxp);
    }
    
    if (ac->dec_start){
        ac->dec_start(adsp, &mctxp);
    }
    
    err = re_main_wait(30000);
    ASSERT_EQ(0, err);
    
    ASSERT_GE(ss.n_packet, EXPECTED_PACKETS);
    ASSERT_EQ(pt, ss.pt);
    ASSERT_EQ(prm.local_ssrc, ss.ssrc);
    ASSERT_EQ(1, ss.seq_diff);
    ASSERT_EQ(960, ss.timestamp_diff);
    ASSERT_GE(ss.max_pktsize_diff, 0);
    
    if (ac->enc_stop){
        ac->enc_stop(aesp);
    }
    
    if (ac->dec_stop){
        ac->dec_stop(adsp);
    }
    
    mem_deref(aesp);
    mem_deref(adsp);
    
    mem_deref(ss.mq);
}

TEST_F(Voe, cbr_on)
{
    struct auenc_state *aesp = NULL;
    struct audec_state *adsp = NULL;
    struct media_ctx *mctxp = NULL;
    const struct aucodec *ac;
    struct sync_state ss;
    struct aucodec_param prm;
    int pt = 96;
    int srate = 48000;
    int err;
    
    init_sync_state(&ss);
    
    memset(&prm, 0, sizeof(prm));
    prm.local_ssrc = 0x12345678;
    prm.pt = 96;
    prm.srate = 48000;
    prm.ch = 2;
    
    voe_enable_cbr(true);
    
    ac = aucodec_find(&aucodecl, "opus", 48000, 2);
    if (ac->enc_alloc){
        err = ac->enc_alloc(&aesp, ac, NULL, &prm,
                            send_rtp_handler, NULL, NULL, NULL, &ss);
        ASSERT_EQ(0, err);
    }
    
    if (ac->dec_alloc){
        err = ac->dec_alloc(&adsp, ac, NULL, &prm,
                            NULL, NULL, NULL);
        ASSERT_EQ(0, err);
    }
    
    if (ac->enc_start){
        ac->enc_start(aesp, &mctxp);
    }
    
    if (ac->dec_start){
        ac->dec_start(adsp, &mctxp);
    }
    
    err = re_main_wait(30000);
    ASSERT_EQ(0, err);
    
    ASSERT_GE(ss.n_packet, EXPECTED_PACKETS);
    ASSERT_EQ(pt, ss.pt);
    ASSERT_EQ(prm.local_ssrc, ss.ssrc);
    ASSERT_EQ(1, ss.seq_diff);
    ASSERT_EQ(960, ss.timestamp_diff);
    ASSERT_EQ(ss.max_pktsize_diff, 0);
    
    if (ac->enc_stop){
        ac->enc_stop(aesp);
    }
    
    if (ac->dec_stop){
        ac->dec_stop(adsp);
    }
    
    mem_deref(aesp);
    mem_deref(adsp);
    
    mem_deref(ss.mq);
}
#endif
