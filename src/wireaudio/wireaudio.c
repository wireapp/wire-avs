
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define AVS_H STR(AVS_HEADER)

#include <string.h>
#include <re.h>
#include <rem.h>
#include AVS_H
#include <baresip.h>


/* Configurable items */
#define AUDIO_SRATE 16000
#define AUDIO_CHAN  1
#define AUDIO_PTIME 20
#define AUDIO_SAMPLES ((AUDIO_SRATE*AUDIO_CHAN*AUDIO_PTIME)/1000)

#define PSTN_SRATE 8000
#define PSTN_CHAN  1
#define PSTN_PTIME 20
#define PSTN_SAMPLES ((PSTN_SRATE*PSTN_CHAN*PSTN_PTIME)/1000)


/** Wire audio */
struct wireaudio {
	uint32_t index;
	struct ausrc *ausrc;
	struct auplay *auplay;

	struct hash *wdevs;

	struct aubuf *ab;
	//struct ausrc_st *ausrc;
	//struct auplay_st *auplay;
	struct auenc_state *enc;
	struct audec_state *dec;
	int16_t *sampv;
	size_t sampc;
	struct tmr tmr;
	uint32_t srate;
	uint32_t ch;
	enum aufmt fmt;

	uint32_t n_read;
	uint32_t n_write;
};

static const struct {
	uint32_t srate;
	uint32_t ch;
} configv[] = {
	{ 8000, 1},
	{16000, 1},
	{32000, 1},
	{44100, 1},
	{48000, 1},
	{ 8000, 2},
	{16000, 2},
	{32000, 2},
	{44100, 2},
	{48000, 2},
};

static struct wireaudio gwa;


struct wdev {
	char *convid;

	struct aumix *ausrc_mix;
	
	struct list ausrcl;
	struct list auplayl;

	struct {
		int16_t sampv[AUDIO_SAMPLES];
		size_t  sampc;

		int16_t play_sampv[AUDIO_SAMPLES];
		size_t  play_sampc;

		int16_t src_sampv[AUDIO_SAMPLES];
		size_t  src_sampc;

		struct auresamp play_resamp;
		struct auresamp src_resamp;

		ausrc_read_h *rh;
		auplay_write_h *wh;

		struct aumix_source *aumix_src;

		void *arg;
	} wa;

	struct le le;
};

struct ausrc_st {
	const struct ausrc *as;  /* pointer to base-class (inheritance) */
	
	struct ausrc_prm *prm;
	struct wdev *wdev;
	struct aumix_source *mix_src;

	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
	
	struct le le;
};

struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	
	struct auplay_prm *prm;
	struct wdev *wdev;
	struct aumix_source *mix_src;

	auplay_write_h *wh;
	void *arg;

	void  *sampv;
	size_t sampc;
	void  *out_sampv;
	size_t out_sampc;

	struct le le;
};


static void wdev_destructor(void *arg)
{
	struct wdev *wdev = arg;

	list_flush(&wdev->ausrcl);
	list_flush(&wdev->auplayl);

	mem_deref(wdev->ausrc_mix);
	mem_deref(wdev->convid);

	mem_deref(wdev->wa.aumix_src);
}

static bool list_apply_handler(struct le *le, void *arg)
{
	struct wdev *wdev = le->data;

	return 0 == str_cmp(wdev->convid, arg);
}

static struct wdev *find_device(const char *convid)
{
	return list_ledata(hash_lookup(gwa.wdevs, hash_joaat_str(convid),
				       list_apply_handler, (void *)convid));
}


static void ausrc_mix_frame_handler(const int16_t *sampv,
				    size_t sampc,
				    void *arg)
{
	struct ausrc_st *st = arg;

#if 0
	info("ausrc(%p): mix frame with size: %lld buf=%w\n",
	     st, sampc, sampv, 10);
#endif
	if (st->rh) {
		st->rh(sampv, sampc, st->arg);
	}
}

static void auplay_mix_frame_handler(const int16_t *sampv,
				    size_t sampc,
				    void *arg)
{
	struct auplay_st *ap = arg;

	if (ap->wh) {
		ap->wh(ap->sampv, ap->sampc, ap->arg);
	}

	/* This is to be mixed with all others together with the wire
	 * audio stream, to all participants on this gateway
	 */
	aumix_source_put(ap->mix_src, ap->sampv, ap->sampc);
}

static void wdev_mix_frame_handler(const int16_t *sampv,
				   size_t sampc,
				   void *arg)
{
	struct wdev *wdev = arg;

	if (wdev->wa.rh) {
		auresamp(&wdev->wa.src_resamp,
			 wdev->wa.src_sampv, &wdev->wa.src_sampc,
			 sampv, sampc);
		wdev->wa.rh((void *)wdev->wa.src_sampv, wdev->wa.src_sampc,
			    wdev->wa.arg);
	}
	if (wdev->wa.wh) {
		wdev->wa.wh(wdev->wa.sampv, wdev->wa.sampc, wdev->wa.arg);

		wdev->wa.play_sampc = AUDIO_SAMPLES;
		int err = auresamp(&wdev->wa.play_resamp,
				   wdev->wa.play_sampv, &wdev->wa.play_sampc,
				   wdev->wa.sampv, wdev->wa.sampc);

#if 0
		re_printf("err=%d insampc=%d out_sampc=%d wframe=%w\n",
			  err,
			  wdev->wa.sampc, wdev->wa.play_sampc,
			  wdev->wa.play_sampv, 10);
#endif

		aumix_source_put(wdev->wa.aumix_src,
				 wdev->wa.play_sampv, wdev->wa.play_sampc);
	}
		
}


static int alloc_device(struct wdev **wdevp,
			const char *convid,
			uint32_t srate, uint32_t ptime, uint8_t ch)
{
	struct wdev *wdev;
	int err;
	
	info("wireaudio: alloc_device: convid=%s srate=%d ch=%d ptime=%d\n",
	     convid, srate, ch, ptime);

	wdev = mem_zalloc(sizeof(*wdev), wdev_destructor);
	if (!wdev)
		return ENOMEM;

	err = aumix_alloc(&wdev->ausrc_mix, srate, ch, ptime);
	if (err)
		goto out;

	err = aumix_source_alloc(&wdev->wa.aumix_src, wdev->ausrc_mix,
				 wdev_mix_frame_handler, wdev);
	if (err) {
		goto out;
	}

	aumix_source_enable(wdev->wa.aumix_src, true);

	auresamp_init(&wdev->wa.play_resamp);
	auresamp_setup(&wdev->wa.play_resamp,
		       AUDIO_SRATE, AUDIO_CHAN,
		       PSTN_SRATE, PSTN_CHAN);
	
	auresamp_init(&wdev->wa.src_resamp);
	auresamp_setup(&wdev->wa.src_resamp,
		       PSTN_SRATE, PSTN_CHAN,
		       AUDIO_SRATE, AUDIO_CHAN);

	str_dup(&wdev->convid, convid);
	wdev->wa.sampc = AUDIO_SAMPLES;
	wdev->wa.play_sampc = AUDIO_SAMPLES;
	wdev->wa.src_sampc = AUDIO_SAMPLES;

	hash_append(gwa.wdevs, hash_joaat_str(convid), &wdev->le, wdev);

 out:
	if (err) {
		mem_deref(wdev);
	}
	else if (wdevp) {
		*wdevp = wdev;
	}

	return err;
}

static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	info("wireaudio: ausrc=%p destructor\n", st);

	aumix_source_enable(st->mix_src, false);
	list_unlink(&st->le);
	
	mem_deref(st->mix_src);
	
}

static int wa_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
			struct media_ctx **ctx,
			struct ausrc_prm *prm, const char *device,
			ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct wdev *wdev;
	int err = 0;

	info("wa_src_alloc: convid=%s srate=%d ch=%d ptime=%d fmt=%d\n",
	     device, prm->srate, prm->ch, prm->ptime, prm->fmt);

	wdev = find_device(device);
	if (!wdev) {
		err = alloc_device(&wdev, device,
				   prm->srate, prm->ptime, prm->ch);
		if (err) {
			return err;
		}
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st) {
		return ENOMEM;
	}

	err = aumix_source_alloc(&st->mix_src, wdev->ausrc_mix,
				 ausrc_mix_frame_handler, st);
	if (err) {
		goto out;
	}
	

	st->as = as;
	st->wdev = wdev;
	st->rh = rh;
	st->errh = errh;
	st->arg = arg;

	list_append(&wdev->ausrcl, &st->le, st);
	aumix_source_enable(st->mix_src, true);

 out:
	if (err) {
		mem_deref(st);
	}
	else if (stp) {
		*stp = st;
	}

	return err;
}

static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	info("wireaudio: auplay=%p destructor\n", st);

	aumix_source_enable(st->mix_src, false);

	mem_deref(st->mix_src);

	list_unlink(&st->le);
}

static int wa_play_alloc(struct auplay_st **stp, const struct auplay *ap,
				struct auplay_prm *prm, const char *device,
				auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	struct wdev *wdev;
	int err = 0;

	info("wa_play_alloc: convid=%s srate=%d ch=%d ptime=%d fmt=%d\n",
	     device, prm->srate, prm->ch, prm->ptime, prm->fmt);

	wdev = find_device(device);
	if (!wdev) {
		err = alloc_device(&wdev, device,
				   prm->srate, prm->ptime, prm->ch);
		if (err) {
			return err;
		}
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st) {
		return ENOMEM;
	}

	err = aumix_source_alloc(&st->mix_src, wdev->ausrc_mix,
				 auplay_mix_frame_handler, st);
	if (err) {
		goto out;
	}

	st->ap = ap;
	st->wdev = wdev;
	st->wh = wh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);

	st->out_sampc = AUDIO_SRATE * AUDIO_CHAN * AUDIO_PTIME / 1000;
	st->out_sampv = mem_alloc(sizeof(int16_t) * st->sampc, NULL);
	
	list_append(&wdev->auplayl, &st->le, st);

	aumix_source_enable(st->mix_src, true);

 out:
	if (err) {
		mem_deref(st);
	}
	else if (stp) {
		*stp = st;
	}

	return err;
}


static int wireaudio_init(void)
{
	int err = 0;
	
	info("wireaudio: module_init\n");

	err = hash_alloc(&gwa.wdevs, 32);

	err  = ausrc_register(&gwa.ausrc, baresip_ausrcl(),
			      "wireaudio", wa_src_alloc);
	err |= auplay_register(&gwa.auplay, baresip_auplayl(),
			       "wireaudio", wa_play_alloc);
	
	return err;
}


static int wireaudio_close(void)
{
	info("wireaudio: module_close\n");
	return 0;
}

int wireaudio_set_handlers(const char *convid,
			   ausrc_read_h *rh,
			   auplay_write_h *wh,
			   void *arg);


int wireaudio_set_handlers(const char *convid,
			   ausrc_read_h *rh,
			   auplay_write_h *wh,
			   void *arg)
{
	struct wdev *wdev = find_device(convid);
	int err = 0;

	if (!wdev) {
		err = alloc_device(&wdev, convid,
				   PSTN_SRATE, PSTN_PTIME, PSTN_CHAN);
		if (err)
			goto out;
	}

	wdev->wa.rh = rh;
	wdev->wa.wh = wh;
	wdev->wa.arg = arg;

 out:
	return err;
}
			   


EXPORT_SYM const struct mod_export DECL_EXPORTS(wireaudio) = {
	"wireaudio",
	"application",
	wireaudio_init,
	wireaudio_close,
};
