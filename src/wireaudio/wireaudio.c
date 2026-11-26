
#include <string.h>
#include <re.h>
#include <rem.h>
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

/* Largest mixer frame a leg can handle: 48kHz stereo 40ms */
#define LEG_MAX_SAMPLES ((48000*2*40)/1000)

struct {
	struct hash *wdevs;
	struct ausrc *ausrc;
	struct auplay *auplay;
} gwa = {
	.wdevs = NULL,
	.ausrc = NULL,
	.auplay = NULL
};

struct wdev {
	/* Opaque conversation key given by the SIP side, not
	 * necessarily the convid (which may not fit a device name)
	 */
	char *convid;

	size_t handler_cnt;

	struct lock *lock;

	struct aumix *ausrc_mix;

	struct list legl;  /* struct wleg, not owned */

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

/*
 * One PSTN leg (SIP call) in the conversation. The device name is
 * "<conv>/<leg>": the conversation key selects the wdev (and
 * its mixer), the full name pairs the leg's ausrc and auplay.
 *
 * A leg has a single mixer source, so that the aumix mix-minus
 * keeps the caller's own audio out of what is sent back to it.
 */
struct wleg {
	char *name;
	struct wdev *wdev;
	struct aumix_source *mix_src;

	/* Protects src/play; the frame handler runs on the mixer thread.
	 * Never call into aumix while holding it.
	 */
	struct lock *lock;
	struct ausrc_st *src;    /* not owned */
	struct auplay_st *play;  /* not owned */

	int16_t sampv[LEG_MAX_SAMPLES];

	struct le le;
};

struct ausrc_st {
	const struct ausrc *as;  /* pointer to base-class (inheritance) */

	struct wleg *leg;

	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
};

struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */

	struct wleg *leg;

	auplay_write_h *wh;
	void *arg;
};


static void wdev_destructor(void *arg)
{
	struct wdev *wdev = arg;

	info("wireaudio: wdev(%p): destructor\n", wdev);

	aumix_source_enable(wdev->wa.aumix_src, false);

	hash_unlink(&wdev->le);
	
	mem_deref(wdev->ausrc_mix);
	mem_deref(wdev->convid);

	mem_deref(wdev->wa.aumix_src);
	mem_deref(wdev->lock);
}

static int alloc_wdevs(struct hash **wdevs)
{
	int err;

	if (*wdevs)
		return EALREADY;

	err = hash_alloc(wdevs, 32);

	if (err) {
		warning("wireaudio: could not allocate wdevs\n");
	}

	return err;
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


/* Called on the mixer thread with the mix of all sources but this leg */
static void leg_mix_frame_handler(const int16_t *sampv,
				  size_t sampc,
				  void *arg)
{
	struct wleg *leg = arg;
	size_t n = min(sampc, ARRAY_SIZE(leg->sampv));

	lock_write_get(leg->lock);

	/* Audio from the PSTN caller goes into the mix, to be heard
	 * by the wire side and all the other legs
	 */
	if (leg->play && leg->play->wh) {
		leg->play->wh(leg->sampv, n, leg->play->arg);
		aumix_source_put(leg->mix_src, leg->sampv, n);
	}

	/* The mix minus this leg goes back to the PSTN caller */
	if (leg->src && leg->src->rh) {
		leg->src->rh(sampv, sampc, leg->src->arg);
	}

	lock_rel(leg->lock);
}

static void wdev_mix_frame_handler(const int16_t *sampv,
				   size_t sampc,
				   void *arg)
{
	struct wdev *wdev = arg;

	lock_write_get(wdev->lock);
	if (wdev->wa.rh) {
		int err = auresamp(&wdev->wa.src_resamp,
				   wdev->wa.src_sampv, &wdev->wa.src_sampc,
				   sampv, sampc);
#if 0
		re_printf("wdev-rh: err=%d insampc=%d out_sampc=%d sin=%w sout=%w\n",
			  err,
			  sampc, wdev->wa.src_sampc,
			  sampv, 6,
			  wdev->wa.src_sampv, 6);
#else
		(void)err;
#endif
		wdev->wa.rh((const void *)wdev->wa.src_sampv,
			    wdev->wa.src_sampc,
			    wdev->wa.arg);
	}
	if (wdev->wa.wh) {
		wdev->wa.wh(wdev->wa.sampv, wdev->wa.sampc, wdev->wa.arg);

		wdev->wa.play_sampc = AUDIO_SAMPLES;
		int err = auresamp(&wdev->wa.play_resamp,
				   wdev->wa.play_sampv, &wdev->wa.play_sampc,
				   wdev->wa.sampv, wdev->wa.sampc);

#if 0
		re_printf("wdev-wh: err=%d insampc=%d out_sampc=%d wframe=%w\n",
			  err,
			  wdev->wa.sampc, wdev->wa.play_sampc,
			  wdev->wa.play_sampv, 10);
#else
		(void)err;
#endif

		aumix_source_put(wdev->wa.aumix_src,
				 wdev->wa.play_sampv, wdev->wa.play_sampc);
	}
	lock_rel(wdev->lock);
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

	err = lock_alloc(&wdev->lock);
	if (err)
		goto out;

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

	if (!gwa.wdevs) {
		err = alloc_wdevs(&gwa.wdevs);
		if (err)
			goto out;
	}
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

static void leg_destructor(void *arg)
{
	struct wleg *leg = arg;

	info("wireaudio: leg(%p): %s destructor\n", leg, leg->name);

	/* Removing the source from the mixer waits for a running
	 * frame handler, after which it is no longer called
	 */
	aumix_source_enable(leg->mix_src, false);
	mem_deref(leg->mix_src);

	list_unlink(&leg->le);

	mem_deref(leg->lock);
	mem_deref(leg->name);
	mem_deref(leg->wdev);
}


/* Get a reference to the leg for device "<conv>/<leg>", allocating
 * it (and its wdev) if needed. A device without '/' is a single leg
 * with the whole name as conversation key.
 */
static int leg_get(struct wleg **legp, const char *device,
		   uint32_t srate, uint32_t ptime, uint8_t ch)
{
	struct wleg *leg = NULL;
	struct wdev *wdev;
	const char *slash;
	char *convid = NULL;
	struct le *le;
	int err;

	if (!legp || !device)
		return EINVAL;

	slash = strrchr(device, '/');
	if (slash)
		err = re_sdprintf(&convid, "%b", device,
				  (size_t)(slash - device));
	else
		err = str_dup(&convid, device);
	if (err)
		return err;

	wdev = mem_ref(find_device(convid));
	if (!wdev) {
		err = alloc_device(&wdev, convid, srate, ptime, ch);
		if (err)
			goto out;
	}

	LIST_FOREACH(&wdev->legl, le) {
		struct wleg *l = le->data;

		if (0 == str_cmp(l->name, device)) {
			*legp = mem_ref(l);
			goto out;
		}
	}

	leg = mem_zalloc(sizeof(*leg), leg_destructor);
	if (!leg) {
		err = ENOMEM;
		goto out;
	}

	/* The leg takes over our wdev reference */
	leg->wdev = wdev;
	wdev = NULL;

	err = lock_alloc(&leg->lock);
	if (err)
		goto out;

	err = str_dup(&leg->name, device);
	if (err)
		goto out;

	err = aumix_source_alloc(&leg->mix_src, leg->wdev->ausrc_mix,
				 leg_mix_frame_handler, leg);
	if (err)
		goto out;

	list_append(&leg->wdev->legl, &leg->le, leg);
	aumix_source_enable(leg->mix_src, true);

	info("wireaudio: leg(%p): %s allocated\n", leg, leg->name);

	*legp = leg;
	leg = NULL;

 out:
	mem_deref(leg);
	mem_deref(wdev);
	mem_deref(convid);

	return err;
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	info("wireaudio: ausrc=%p destructor\n", st);

	if (st->leg) {
		lock_write_get(st->leg->lock);
		if (st->leg->src == st)
			st->leg->src = NULL;
		lock_rel(st->leg->lock);
	}

	mem_deref(st->leg);
}

static int wa_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
			struct media_ctx **ctx,
			struct ausrc_prm *prm, const char *device,
			ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct wleg *leg;
	int err = 0;

	info("wa_src_alloc: device=%s srate=%d ch=%d ptime=%d fmt=%d\n",
	     device, prm->srate, prm->ch, prm->ptime, prm->fmt);

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	err = leg_get(&st->leg, device, prm->srate, prm->ptime, prm->ch);
	if (err)
		goto out;

	st->as = as;
	st->rh = rh;
	st->errh = errh;
	st->arg = arg;

	leg = st->leg;
	lock_write_get(leg->lock);
	if (leg->src) {
		warning("wireaudio: leg(%p): replacing ausrc %p with %p\n",
			leg, leg->src, st);
	}
	leg->src = st;
	lock_rel(leg->lock);

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

	if (st->leg) {
		lock_write_get(st->leg->lock);
		if (st->leg->play == st)
			st->leg->play = NULL;
		lock_rel(st->leg->lock);
	}

	mem_deref(st->leg);
}

static int wa_play_alloc(struct auplay_st **stp, const struct auplay *ap,
				struct auplay_prm *prm, const char *device,
				auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	struct wleg *leg;
	int err = 0;

	info("wa_play_alloc: device=%s srate=%d ch=%d ptime=%d fmt=%d\n",
	     device, prm->srate, prm->ch, prm->ptime, prm->fmt);

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	err = leg_get(&st->leg, device, prm->srate, prm->ptime, prm->ch);
	if (err)
		goto out;

	st->ap = ap;
	st->wh = wh;
	st->arg = arg;

	leg = st->leg;
	lock_write_get(leg->lock);
	if (leg->play) {
		warning("wireaudio: leg(%p): replacing auplay %p with %p\n",
			leg, leg->play, st);
	}
	leg->play = st;
	lock_rel(leg->lock);

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

	alloc_wdevs(&gwa.wdevs);

	err  = ausrc_register(&gwa.ausrc, baresip_ausrcl(),
			      "wireaudio", wa_src_alloc);
	err |= auplay_register(&gwa.auplay, baresip_auplayl(),
			       "wireaudio", wa_play_alloc);
	
	return err;
}


static int wireaudio_close(void)
{
	info("wireaudio: module_close\n");

	gwa.ausrc = mem_deref(gwa.ausrc);
	gwa.auplay = mem_deref(gwa.auplay);

	/* Any remaining wdevs are owned elsewhere; only unlink them
	 * so that their destructors don't touch the freed hash.
	 */
	hash_clear(gwa.wdevs);
	gwa.wdevs = mem_deref(gwa.wdevs);

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
	struct wdev *wdev;
	bool release = false;
	int err = 0;
	
	if (!convid)
		return EINVAL;

	wdev = find_device(convid);

	info("wireaudio: set_handlers: convid=%s rh=%p wh=%p arg=%p wdev=%p\n",
	     convid, rh, wh, arg, wdev);

	/* While handler_cnt > 0 the handlers hold one reference
	 * on the wdev, so the device outlives any ausrc/auplay
	 * and is released when the last handler is removed.
	 */
	if (wdev) {
		lock_write_get(wdev->lock);
		if (rh || wh) {
			/* The device may already exist because baresip
			 * allocated it (ausrc/auplay) before the adm
			 * appeared, so install handlers if not there already.
			 */
			if (0 == wdev->handler_cnt) {
				wdev->wa.rh = rh;
				wdev->wa.wh = wh;
				wdev->wa.arg = arg;
				mem_ref(wdev);
			}
			wdev->handler_cnt++;
		}
		else if (wdev->handler_cnt > 0) {
			wdev->handler_cnt--;
			if (0 == wdev->handler_cnt) {
				wdev->wa.rh = NULL;
				wdev->wa.wh = NULL;
				wdev->wa.arg = NULL;
				release = true;
			}
		}
		lock_rel(wdev->lock);

		/* Drop the handler reference outside the lock,
		 * as it may destroy the wdev (and its lock)
		 */
		if (release)
			mem_deref(wdev);

		return 0;
	}
	else {
		if (rh == NULL || wh == NULL)
			return 0;
		
		/* The reference from alloc_device is the handler
		 * reference, released when handler_cnt drops to 0
		 */
		err = alloc_device(&wdev, convid,
				   PSTN_SRATE, PSTN_PTIME, PSTN_CHAN);
		if (err)
			goto out;

		lock_write_get(wdev->lock);
		wdev->handler_cnt = 1;
		wdev->wa.rh = rh;
		wdev->wa.wh = wh;
		wdev->wa.arg = arg;
		lock_rel(wdev->lock);
	}

 out:
	return err;
}
			   


EXPORT_SYM const struct mod_export DECL_EXPORTS(wireaudio) = {
	"wireaudio",
	"audio",
	wireaudio_init,
	wireaudio_close,
};
