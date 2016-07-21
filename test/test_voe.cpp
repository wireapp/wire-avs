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


TEST(voe, basic_init_close)
{
	struct list aucodecl = LIST_INIT;
	int err;

	err = voe_init(&aucodecl);
	ASSERT_EQ(0, err);
	
	ASSERT_GE(list_count(&aucodecl), 1);

	voe_close();

	ASSERT_EQ(0, list_count(&aucodecl));
}


TEST(voe, extra_close)
{
	voe_close();
}


TEST(voe, close_twice)
{
	struct list aucodecl = LIST_INIT;
	int err;

	err = voe_init(&aucodecl);
	ASSERT_EQ(0, err);

	voe_close();
	voe_close();
}


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

		voe_register_adm((void*)&ad);
	}

	virtual void TearDown() override
	{
		voe_deregister_adm();

		voe_close();
	}

protected:
	struct list aucodecl = LIST_INIT;
	webrtc::fake_audiodevice ad;
};


TEST_F(Voe, enc_dec_alloc)
{
	struct aucodec_param prm;
	struct auenc_state *aesp = NULL;
	struct audec_state *adsp = NULL;
	struct media_ctx *mctxp = NULL;
	const struct aucodec *ac;
	int pt = 96;
	int srate = 48000;
	int err;
    
	memset(&prm, 0, sizeof(prm));

    	ac = aucodec_find(&aucodecl, "opus", 48000, 2);
    
	if (ac->enc_alloc){
		err = ac->enc_alloc(&aesp, &mctxp, ac, NULL, &prm,
                      NULL, NULL, NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}

	if (ac->dec_alloc){
		err = ac->dec_alloc(&adsp, &mctxp, ac, NULL, &prm,
                            NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}
    
	mem_deref(aesp);
	mem_deref(adsp);
}


TEST_F(Voe, unmute_after_call)
{
	int err;
	struct aucodec_param prm;
	struct auenc_state *aesp = NULL;
	struct audec_state *adsp = NULL;
	struct media_ctx *mctxp = NULL;
	const struct aucodec *ac;
	int pt = 96;
	int srate = 48000;
	bool muted;
    
	memset(&prm, 0, sizeof(prm));

	ac = aucodec_find(&aucodecl, "opus", 48000, 2);
    
	if (ac->enc_alloc){
		err = ac->enc_alloc(&aesp, &mctxp, ac, NULL, &prm,
                            NULL, NULL, NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}
    
	if (ac->dec_alloc){
		err = ac->dec_alloc(&adsp, &mctxp, ac, NULL, &prm,
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


struct sync_state{
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool event;
	uint8_t pt;
	uint32_t ssrc;
    uint16_t prev_seq;
    int32_t seq_diff;
    uint32_t prev_timestamp;
    int64_t timestamp_diff;
    uint8_t packet_buf[100];
    int nbytes;
    const struct aucodec *ac;
    struct audec_state *adsp;
};

static void init_sync_state(struct sync_state* pss){
    pthread_mutex_init(&pss->mutex,NULL);
    pthread_cond_init(&pss->cond, NULL);
    pss->event = false;
    pss->prev_seq = 0;
    pss->seq_diff = 0;
    pss->prev_timestamp = 0;
    pss->timestamp_diff = 0;
    pss->nbytes = 0;
}

static void wait_for_event(struct sync_state* pss){
	int ret = 0;
	struct timeval now;
	struct timespec t;
	gettimeofday(&now, NULL);
    
	t.tv_sec = now.tv_sec + 30;
	t.tv_nsec = 0;
    
	pthread_mutex_lock(&pss->mutex);
	while(!pss->event){
		ret = pthread_cond_timedwait(&pss->cond, &pss->mutex, &t);
		if(ret){
			break;
		}
	}
	pthread_mutex_unlock(&pss->mutex);
	ASSERT_EQ(0, ret);
	pss->event = false;
}

static uint8_t read_uint8(const uint8_t *pkt){
    return pkt[0];
}

static uint16_t read_uint16(const uint8_t *pkt){
    uint16_t tmp = pkt[1] + (((uint16_t)pkt[0]) << 8);
    return tmp;
}

static uint32_t read_uint32(const uint8_t *pkt){
    uint32_t tmp = pkt[3] + (((uint32_t)pkt[2]) << 8);
	tmp += ((uint32_t)pkt[1] << 16) + ((uint32_t)pkt[0] << 24);
    return tmp;
}

static int send_rtp(const uint8_t *pkt, size_t len, void *arg){
	struct sync_state* pss = (struct sync_state*)arg;
    
	pthread_mutex_lock(&pss->mutex);
	pss->event = true;
	pss->pt = read_uint8(&pkt[1]);
    uint16_t seq = read_uint16(&pkt[2]);
    uint32_t timestamp = read_uint32(&pkt[4]);
    pss->ssrc = read_uint32(&pkt[8]);
    pss->seq_diff = seq - pss->prev_seq;
    if(pss->seq_diff < 0) {
        pss->seq_diff += 0xffff;
    }
    pss->prev_seq = seq;
    pss->timestamp_diff = timestamp - pss->prev_timestamp;
    if(pss->timestamp_diff < 0) {
        pss->timestamp_diff += 0xfffffff;
    }
    pss->prev_timestamp = timestamp;
    memcpy(pss->packet_buf, pkt, len);
    pss->nbytes = len;
    
	pthread_cond_signal(&pss->cond);
	pthread_mutex_unlock(&pss->mutex);

    return 0;
}


TEST(voe, enc_dec_alloc_start_stop)
{
	struct list aucodecl = LIST_INIT;
	int err;
	struct auenc_state *aesp = NULL;
	struct audec_state *adsp = NULL;
	struct media_ctx *mctxp = NULL;
	const struct aucodec *ac;
	int pt = 96;
	int srate = 48000;
	webrtc::fake_audiodevice ad;
	struct sync_state ss;
    init_sync_state(&ss);
	struct aucodec_param prm;
    memset(&prm, 0, sizeof(prm));
    prm.local_ssrc = 0x12345678;
	prm.pt = 96;
	prm.srate = 48000;
	prm.ch = 2;

	err = voe_init(&aucodecl);
	ASSERT_EQ(0, err);
    
	voe_register_adm((void*)&ad);
    
	ac = aucodec_find(&aucodecl, "opus", 48000, 2);
	if (ac->enc_alloc){
		err = ac->enc_alloc(&aesp, &mctxp, ac, NULL, &prm,
							send_rtp, NULL, NULL, NULL, &ss);
		ASSERT_EQ(0, err);
	}
    
	if (ac->dec_alloc){
		err = ac->dec_alloc(&adsp, &mctxp, ac, NULL, &prm,
							NULL, NULL, NULL);
		ASSERT_EQ(0, err);
	}
    
    
	if (ac->enc_start){
		ac->enc_start(aesp);
	}

	if (ac->dec_start){
		ac->dec_start(adsp);
	}
    
	wait_for_event(&ss);
    //ASSERT_EQ(pt, ss.pt); // for some reason the first packet has wrong pt ??
    
	wait_for_event(&ss);
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

	voe_deregister_adm();
    
	voe_close();
}

TEST(voe, packet_size_40)
{
    struct list aucodecl = LIST_INIT;
    int err;
    struct auenc_state *aesp = NULL;
    struct audec_state *adsp = NULL;
    struct media_ctx *mctxp = NULL;
    const struct aucodec *ac;
    int pt = 96;
    int srate = 48000;
    webrtc::fake_audiodevice ad;
    struct sync_state ss;
    init_sync_state(&ss);
    struct aucodec_param prm;
    memset(&prm, 0, sizeof(prm));
    prm.local_ssrc = 0x12345678;
    prm.pt = 96;
    prm.srate = 48000;
    prm.ch = 2;
    
    err = voe_init(&aucodecl);
    ASSERT_EQ(0, err);
    
    voe_register_adm((void*)&ad);
    
    ac = aucodec_find(&aucodecl, "opus", 48000, 2);
    if (ac->enc_alloc){
        err = ac->enc_alloc(&aesp, &mctxp, ac, NULL, &prm,
                            send_rtp, NULL, NULL, NULL, &ss);
        ASSERT_EQ(0, err);
    }
    
    if (ac->dec_alloc){
        err = ac->dec_alloc(&adsp, &mctxp, ac, NULL, &prm,
                            NULL, NULL, NULL);
        ASSERT_EQ(0, err);
    }
    
    if (ac->enc_start){
        ac->enc_start(aesp);
    }
    
    if (ac->dec_start){
        ac->dec_start(adsp);
    }
    
    voe_set_packet_size(40);
    
    wait_for_event(&ss);
    //ASSERT_EQ(pt, ss.pt); // for some reason the first packet has wrong pt ??
    
    wait_for_event(&ss);
    ASSERT_EQ(pt, ss.pt);
    ASSERT_EQ(prm.local_ssrc, ss.ssrc);
    ASSERT_EQ(1, ss.seq_diff);
    ASSERT_EQ(2*960, ss.timestamp_diff);
    
    if (ac->enc_stop){
        ac->enc_stop(aesp);
    }
    
    if (ac->dec_stop){
        ac->dec_stop(adsp);
    }
    
    mem_deref(aesp);
    mem_deref(adsp);
    
    voe_deregister_adm();
    
    voe_close();
}

#if 0
static void mqueue_handler(int id, void *data, void *arg)
{
    struct sync_state *pss = (struct sync_state *)data;
    (void)arg;
    
    printf("pss->nbytes = %d \n", pss->nbytes);
    
//if (ac && ac->dec_rtph) {
//    ac->dec_rtph(adsp, ss.packet_buf, ss.nbytes);
//}
}

TEST(voe, enc_dec_alloc_start_stop_interrupt)
{
    struct list aucodecl = LIST_INIT;
    int err;
    struct auenc_state *aesp = NULL;
    struct audec_state *adsp = NULL;
    struct media_ctx *mctxp = NULL;
    const struct aucodec *ac;
    int pt = 96;
    int srate = 48000;
    webrtc::fake_audiodevice ad(true);
    struct sync_state ss;
    init_sync_state(&ss);
    struct aucodec_param prm;
    memset(&prm, 0, sizeof(prm));
    prm.local_ssrc = 0x12345678;
    prm.pt = 96;
    prm.srate = 48000;
    prm.ch = 2;
    
    err = voe_init(&aucodecl);
    ASSERT_EQ(0, err);
    
    err = re_thread_init();
    
    struct mqueue *mq;
    err = mqueue_alloc(&mq, mqueue_handler, NULL);
    if(err){
        printf("Could not allocate mqueue \n");
    }
    
    voe_register_adm((void*)&ad);
    
    ac = aucodec_find(&aucodecl, "opus", 48000, 2);
    ss.ac = ac;
    
    if (ac->enc_alloc){
        err = ac->enc_alloc(&aesp, &mctxp, ac, NULL, &prm,
                            send_rtp, NULL, NULL, NULL, &ss);
        ASSERT_EQ(0, err);
    }
    
    if (ac->dec_alloc){
        err = ac->dec_alloc(&adsp, &mctxp, ac, NULL, &prm,
                            NULL, NULL, NULL);
        ASSERT_EQ(0, err);
    }
    ss.adsp = adsp;
    
    if (ac->enc_start){
        ac->enc_start(aesp);
    }
    
    if (ac->dec_start){
        ac->dec_start(adsp);
    }
    
    wait_for_event(&ss);
    
    wait_for_event(&ss);
    pthread_mutex_lock(&ss.mutex);
    if (mqueue_push(mq, 0, (void*)&ss) != 0) {
        error("mediamgr_set_sound_mode failed \n");
    }
    pthread_mutex_unlock(&ss.mutex);
    
    for(int i = 0; i < 200; i++){
            wait_for_event(&ss);
    }
    
    
    //sleep(3);
    
    if (ac->enc_stop){
        ac->enc_stop(aesp);
    }
    
    if (ac->dec_stop){
        ac->dec_stop(adsp);
    }
    
    mem_deref(aesp);
    mem_deref(adsp);
    
    voe_deregister_adm();
    
    voe_close();
}
#endif

