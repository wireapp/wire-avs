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
} audummy;


struct auenc_state {
	const struct aucodec *ac;  /* inheritance */

	struct tmr tmr_tx;
	uint32_t ts;
	int pt;

	auenc_rtp_h *rtph;
	auenc_rtcp_h *rtcph;
	auenc_packet_h *pkth;
	auenc_err_h *errh;
	void *arg;
};


struct audec_state {
	const struct aucodec *ac;  /* inheritance */

	audec_recv_h *recvh;
	audec_err_h *errh;
	void *arg;
};


static void timeout(void *arg)
{
	struct auenc_state *aes = arg;
	static uint8_t pld[100];

	tmr_start(&aes->tmr_tx, 20, timeout, aes);

	if (aes->pkth)
		aes->pkth(aes->pt, aes->ts, pld, sizeof(pld), aes->arg);

	aes->ts += 1920; /* opus in 48000Hz/2ch */
}


static void aes_destructor(void *arg)
{
	struct auenc_state *aes = arg;

	tmr_cancel(&aes->tmr_tx);

	(void)aes;
}


static int enc_alloc(struct auenc_state **aesp,
		     struct media_ctx **mctxp,
		     const struct aucodec *ac, const char *fmtp, int pt,
		     uint32_t srate, uint8_t ch,
		     auenc_rtp_h *rtph,
		     auenc_rtcp_h *rtcph,
		     auenc_packet_h *pkth,
		     auenc_err_h *errh,
		     void *arg)
{
	struct auenc_state *aes;
	int err = 0;

	if (!aesp || !ac || !mctxp) {
		return EINVAL;
	}

	info("audummy: enc_alloc: allocating codec:%s(%d)\n", ac->name, pt);

	aes = (struct auenc_state *)mem_zalloc(sizeof(*aes), aes_destructor);
	if (!aes)
		return ENOMEM;

	aes->ac = ac;
	aes->pt = pt;
	aes->rtph = rtph;
	aes->rtcph = rtcph;
	aes->pkth = pkth;
	aes->errh = errh;
	aes->arg = arg;

	if (err)
		mem_deref(aes);
	else
		*aesp = aes;

	return err;
}


static int audummy_start(struct auenc_state *aes)
{
	if (!aes)
		return EINVAL;

	tmr_start(&aes->tmr_tx, 20, timeout, aes);

	return 0;
}


static void audummy_stop(struct auenc_state *aes)
{
	if (!aes)
		return;

	tmr_cancel(&aes->tmr_tx);
}


static void ads_destructor(void *arg)
{
	struct auenc_state *ads = arg;

	(void)ads;
}


static int dec_alloc(struct audec_state **adsp,
		     struct media_ctx **mctxp,
		     const struct aucodec *ac,
		     const char *fmtp, int pt, uint32_t srate, uint8_t ch,
		     audec_recv_h *recvh,
		     audec_err_h *errh,
		     void *arg)
{
	struct audec_state *ads;
	int err = 0;

	if (!adsp || !ac || !mctxp)
		return EINVAL;

	info("audummy: dec_alloc: allocating codec:%s(%d)\n", ac->name, pt);

	ads = mem_zalloc(sizeof(*ads), ads_destructor);
	if (!ads)
		return ENOMEM;

	ads->ac = ac;
	ads->recvh = recvh;
	ads->errh = errh;
	ads->arg = arg;

	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


static struct aucodec audummy_aucodecv[NUM_CODECS] = {
	{
		.name      = "opus",
		.srate     = 48000,
		.ch        = 2,
		.fmtp      = "stereo=0;sprop-stereo=0",
		.has_rtp   = true,

		.enc_alloc = enc_alloc,
		.ench      = NULL,
		.enc_start = audummy_start,
		.enc_stop  = audummy_stop,

		.dec_alloc = dec_alloc,
		.dec_rtph  = NULL,
		.dec_rtcph = NULL,
		.dec_start = NULL,
		.dec_stats = NULL,
		.dec_stop  = NULL,
	}
};


int audummy_init(struct list *aucodecl)
{
	size_t i;
	int err = 0;

	memset(&audummy, 0, sizeof(audummy));

	/* list all supported codecs */

	audummy.ncodecs = 0;
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
