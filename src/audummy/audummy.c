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
#include "avs_log.h"
#include "avs_string.h"
#include "avs_aucodec.h"

#include "avs_audummy.h"


#define NUM_CODECS 1


static struct {
	int ncodecs;
	bool force_error;
} audummy;


struct auenc_state {
	const struct aucodec *ac;  /* inheritance */

	struct tmr tmr_tx;
	uint32_t ssrc;
	uint32_t ts;
	uint16_t seq;
	int pt;

	auenc_rtp_h *rtph;
	auenc_rtcp_h *rtcph;
	auenc_err_h *errh;
	void *arg;
};


struct audec_state {
	const struct aucodec *ac;  /* inheritance */

	audec_err_h *errh;
	void *arg;
};


static int send_packet(struct auenc_state *aes, const uint8_t *pld, size_t len)
{
	struct rtp_header hdr;
	struct mbuf *mb = mbuf_alloc(128);
	int err;

	memset(&hdr, 0, sizeof(hdr));

	hdr.ver  = RTP_VERSION;
	hdr.m    = 0;
	hdr.pt   = aes->pt;
	hdr.seq  = aes->seq++;
	hdr.ts   = aes->ts;
	hdr.ssrc = aes->ssrc;

	err = rtp_hdr_encode(mb, &hdr);
	if (err)
		goto out;

	err = mbuf_write_mem(mb, pld, len);
	if (err)
		goto out;

	err = aes->rtph(mb->buf, mb->end, aes->arg);
	if (err)
		goto out;

 out:
	mem_deref(mb);
	return err;
}


static void timeout(void *arg)
{
	struct auenc_state *aes = arg;
	static uint8_t pld[100];
	int err;

	tmr_start(&aes->tmr_tx, 20, timeout, aes);

	debug("audummy: encoder send %zu bytes\n", sizeof(pld));

	err = send_packet(aes, pld, sizeof(pld));
	if (err) {
		warning("audummy: send_packet failed (%m)\n", err);
	}

	aes->ts += 1920; /* opus in 48000Hz/2ch */
}


static void aes_destructor(void *arg)
{
	struct auenc_state *aes = arg;

	tmr_cancel(&aes->tmr_tx);

	(void)aes;
}


static int enc_alloc(struct auenc_state **aesp,
		     const struct aucodec *ac, const char *fmtp,
		     struct aucodec_param *prm,
		     auenc_rtp_h *rtph,
		     auenc_rtcp_h *rtcph,
		     auenc_err_h *errh,
		     void *extcodec_arg,
		     void *arg)
{
	struct auenc_state *aes;
	int err = 0;

	(void)extcodec_arg; /* Not an external codec */
	
	if (!aesp || !ac) {
		return EINVAL;
	}

	info("audummy: enc_alloc: allocating codec:%s(%d)\n", ac->name, prm->pt);

	aes = (struct auenc_state *)mem_zalloc(sizeof(*aes), aes_destructor);
	if (!aes)
		return ENOMEM;

	aes->ssrc = rand_u32();
	aes->seq = rand_u16() & 0x0fff;

	aes->ac = ac;
	aes->pt = prm->pt;
	aes->rtph = rtph;
	aes->rtcph = rtcph;
	aes->errh = errh;
	aes->arg = arg;

	if (err)
		mem_deref(aes);
	else
		*aesp = aes;

	return err;
}


static int audummy_start(struct auenc_state *aes,
			 bool cbr,
			 const struct aucodec_param *prm,
			 struct media_ctx **mctxp)
{
	if (!aes || !mctxp)
		return EINVAL;

	(void)cbr;
	(void)prm;
	
	if(audummy.force_error)
		return EIO;
    
	tmr_start(&aes->tmr_tx, 20, timeout, aes);

	return 0;
}


static void audummy_stop(struct auenc_state *aes)
{
	if (!aes)
		return;

	info("audummy: encoder stopped\n");
	tmr_cancel(&aes->tmr_tx);
}


static void ads_destructor(void *arg)
{
	struct auenc_state *ads = arg;

	(void)ads;
}


static int dec_alloc(struct audec_state **adsp,
		     const struct aucodec *ac,
		     const char *fmtp,
		     struct aucodec_param *prm,
		     audec_err_h *errh,
		     void *extcodec_arg,
		     void *arg)
{
	struct audec_state *ads;
	int err = 0;

	(void)extcodec_arg; /* Not an external codec */
	
	if (!adsp || !ac)
		return EINVAL;

	info("audummy: dec_alloc: allocating codec:%s(%d)\n", ac->name, prm->pt);

	ads = mem_zalloc(sizeof(*ads), ads_destructor);
	if (!ads)
		return ENOMEM;

	ads->ac = ac;
	ads->errh = errh;
	ads->arg = arg;

	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


static int audec_rtp_handler(struct audec_state *ads,
			     const uint8_t *pkt, size_t len)
{
	struct rtp_header hdr;
	struct mbuf mb;
	int err;

	mb.buf = (void *)pkt;
	mb.pos = 0;
	mb.end = len;
	mb.size = len;

	err = rtp_hdr_decode(&hdr, &mb);
	if (err) {
		warning("audummy: could not decode RTP header (%m)\n", err);
		return err;
	}

#if 1
	/* Dont print anything here, it is too noisy .. */
	debug("audummy: decoded RTP packet:  seq=%u  ts=%u\n",
	      hdr.seq, hdr.ts);
#endif

	return 0;
}


static struct aucodec audummy_aucodecv[NUM_CODECS] = {
	{
		.name      = "opus",
		.srate     = 48000,
		.ch        = 2,
		.fmtp      = "stereo=0;sprop-stereo=0",

		.enc_alloc = enc_alloc,

		.enc_start = audummy_start,
		.enc_stop  = audummy_stop,

		.dec_alloc = dec_alloc,
		.dec_rtph  = audec_rtp_handler,
		.dec_rtcph = NULL,
		.dec_start = NULL,
		.dec_stop  = NULL,
		.get_stats = NULL,
	}
};


int audummy_init(struct list *aucodecl)
{
	size_t i;
	int err = 0;

	memset(&audummy, 0, sizeof(audummy));

	/* list all supported codecs */

	audummy.ncodecs = 0;
	audummy.force_error = false;
	for (i = 0; i < NUM_CODECS; ++i) {
		struct aucodec *ac = &audummy_aucodecv[i];

		if (!ac->name || !ac->srate || !ac->ch)
			continue;

		ac->data = NULL;

		aucodec_register(aucodecl, ac);
		++audummy.ncodecs;

		info("audummy_init: registering %s(%d) ch=%d\n",
		     ac->name, ac->srate, ac->ch);
	}

	if (err)
		audummy_close();

	return err;
}


void audummy_close(void)
{
	size_t i;

	for (i = 0; i < NUM_CODECS; ++i) {
		struct aucodec *ac = &audummy_aucodecv[i];

		aucodec_unregister(ac);
		--audummy.ncodecs;

		info("audummy_close: unregistering %s(%d) ch=%d\n",
		     ac->name, ac->srate, ac->ch);
	}
}

void audummy_force_error(void)
{
	audummy.force_error = true;
}

