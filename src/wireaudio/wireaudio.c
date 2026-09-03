
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define AVS_H STR(AVS_HEADER)

#include <string.h>
#include <re.h>
#include <rem.h>
#include AVS_H
#include <baresip.h>


/* Configurable items */
#define PTIME 20


/** Wire audio */
struct wireaudio {
	uint32_t index;
	struct aubuf *ab;
	struct ausrc_st *ausrc;
	struct auplay_st *auplay;
	const struct aucodec *ac;
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

static struct wireaudio *gwa = NULL;
static char aucodec[64];


static void wireaudio_destructor(void *arg)
{
	struct wireaudio *wa = arg;

	tmr_cancel(&wa->tmr);
	mem_deref(wa->ausrc);
	mem_deref(wa->auplay);
	mem_deref(wa->sampv);
	mem_deref(wa->ab);
	mem_deref(wa->enc);
	mem_deref(wa->dec);
}


static void print_stats(struct wireaudio *wa)
{
	double rw_ratio = 0.0;

	if (wa->n_write)
		rw_ratio = 1.0 * wa->n_read / wa->n_write;

	(void)re_fprintf(stdout, "\r%uHz %dch %s "
			 " n_read=%u n_write=%u rw_ratio=%.2f",
			 wa->srate, wa->ch, aufmt_name(wa->fmt),
			 wa->n_read, wa->n_write, rw_ratio);

	if (str_isset(aucodec))
		(void)re_fprintf(stdout, " codec='%s'", aucodec);

	fflush(stdout);
}


static void tmr_handler(void *arg)
{
	struct wireaudio *wa = arg;

	tmr_start(&wa->tmr, 100, tmr_handler, wa);
	print_stats(wa);
}


static int codec_read(struct wireaudio *wa, int16_t *sampv, size_t sampc)
{
	uint8_t x[2560];
	size_t xlen = sizeof(x);
	int err;

	aubuf_read_samp(wa->ab, wa->sampv, wa->sampc);

	err = wa->ac->ench(wa->enc, x, &xlen,
			   AUFMT_S16LE, wa->sampv, wa->sampc);
	if (err)
		goto out;

	if (wa->ac->dech) {
		err = wa->ac->dech(wa->dec, AUFMT_S16LE, sampv, &sampc,
				   x, xlen);
		if (err)
			goto out;
	}
	else {
		info("wireaudio: no decode handler\n");
	}

 out:

	return err;
}


static void read_handler(const void *sampv, size_t sampc, void *arg)
{
	struct wireaudio *wa = arg;
	size_t num_bytes = sampc * aufmt_sample_size(wa->fmt);
	int err;

	++wa->n_read;

	err = aubuf_write(wa->ab, sampv, num_bytes);
	if (err) {
		warning("wireaduio: aubuf_write: %m\n", err);
	}
}


static void write_handler(void *sampv, size_t sampc, void *arg)
{
	struct wireaudio *wa = arg;
	size_t num_bytes = sampc * aufmt_sample_size(wa->fmt);
	int err;

	++wa->n_write;

	/* read from beginning */
	if (wa->ac) {
		err = codec_read(wa, sampv, sampc);
		if (err) {
			warning("wireaudio: codec_read error "
				"on %zu samples (%m)\n", sampc, err);
		}
	}
	else {
		aubuf_read(wa->ab, sampv, num_bytes);
	}
}


static void error_handler(int err, const char *str, void *arg)
{
	(void)arg;
	warning("wireaudio: ausrc error: %m (%s)\n", err, str);
	gwa = mem_deref(gwa);
}


static void start_codec(struct wireaudio *wa, const char *name)
{
	struct auenc_param prm = {PTIME, 0};
	int err;

	wa->ac = aucodec_find(baresip_aucodecl(), name,
			      configv[wa->index].srate,
			      configv[wa->index].ch);
	if (!wa->ac) {
		warning("wireaudio: could not find codec: %s\n", name);
		return;
	}

	if (wa->ac->encupdh) {
		err = wa->ac->encupdh(&wa->enc, wa->ac, &prm, NULL);
		if (err) {
			warning("wireaudio: encoder update failed: %m\n", err);
		}
	}

	if (wa->ac->decupdh) {
		err = wa->ac->decupdh(&wa->dec, wa->ac, NULL);
		if (err) {
			warning("wireaudio: decoder update failed: %m\n", err);
		}
	}
}


static int wireaudio_reset(struct wireaudio *wa)
{
	struct auplay_prm auplay_prm;
	struct ausrc_prm ausrc_prm;
	const struct config *cfg = conf_config();
	int err;

	if (!cfg)
		return ENOENT;

	if (cfg->audio.src_fmt != cfg->audio.play_fmt) {
		warning("wireaudio: ausrc_format and auplay_format"
			" must be the same\n");
		return EINVAL;
	}

	wa->fmt = cfg->audio.src_fmt;

	/* Optional audio codec */
	if (str_isset(aucodec)) {
		if (cfg->audio.src_fmt != AUFMT_S16LE) {
			warning("wireaudio: only s16 supported with codec\n");
			return EINVAL;
		}

		start_codec(wa, aucodec);
	}

	/* audio player/source must be stopped first */
	wa->auplay = mem_deref(wa->auplay);
	wa->ausrc  = mem_deref(wa->ausrc);

	wa->sampv  = mem_deref(wa->sampv);
	wa->ab     = mem_deref(wa->ab);

	wa->srate = configv[wa->index].srate;
	wa->ch    = configv[wa->index].ch;

	if (str_isset(aucodec)) {
		wa->sampc = wa->srate * wa->ch * PTIME / 1000;
		wa->sampv = mem_alloc(wa->sampc * 2, NULL);
		if (!wa->sampv)
			return ENOMEM;
	}

	info("Audio-loop: %uHz, %dch\n", wa->srate, wa->ch);

	err = aubuf_alloc(&wa->ab, 320, 0);
	if (err)
		return err;

	auplay_prm.srate      = wa->srate;
	auplay_prm.ch         = wa->ch;
	auplay_prm.ptime      = PTIME;
	auplay_prm.fmt        = wa->fmt;
	err = auplay_alloc(&wa->auplay, baresip_auplayl(),
			   cfg->audio.play_mod, &auplay_prm,
			   cfg->audio.play_dev, write_handler, wa);
	if (err) {
		warning("wireaudio: auplay %s,%s failed: %m\n",
			cfg->audio.play_mod, cfg->audio.play_dev,
			err);
		return err;
	}

	ausrc_prm.srate      = wa->srate;
	ausrc_prm.ch         = wa->ch;
	ausrc_prm.ptime      = PTIME;
	ausrc_prm.fmt        = wa->fmt;
	err = ausrc_alloc(&wa->ausrc, baresip_ausrcl(),
			  NULL, cfg->audio.src_mod,
			  &ausrc_prm, cfg->audio.src_dev,
			  read_handler, error_handler, wa);
	if (err) {
		warning("wireaudio: ausrc %s,%s failed: %m\n", cfg->audio.src_mod,
			cfg->audio.src_dev, err);
		return err;
	}

	return err;
}


static int wireaudio_alloc(struct wireaudio **wap)
{
	struct wireaudio *wa;
	int err;

	wa = mem_zalloc(sizeof(*wa), wireaudio_destructor);
	if (!wa)
		return ENOMEM;

	tmr_start(&wa->tmr, 100, tmr_handler, wa);

	err = wireaudio_reset(wa);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(wa);
	else
		*wap = wa;

	return err;
}


static int wireaudio_cycle(struct wireaudio *wa)
{
	int err;

	++wa->index;

	if (wa->index >= ARRAY_SIZE(configv)) {
		gwa = mem_deref(gwa);
		info("\nAudio-loop stopped\n");
		return 0;
	}

	err = wireaudio_reset(wa);
	if (err)
		return err;

	info("\nAudio-loop started: %uHz, %dch\n", wa->srate, wa->ch);

	return 0;
}


/**
 * Start the audio loop (for testing)
 */
static int wireaudio_start(struct re_printf *pf, void *arg)
{
	int err;

	(void)pf;
	(void)arg;

	if (gwa) {
		err = wireaudio_cycle(gwa);
		if (err) {
			warning("wireaudio: loop cycle: %m\n", err);
		}
	}
	else {
		err = wireaudio_alloc(&gwa);
		if (err) {
			warning("wireaudio: alloc failed %m\n", err);
		}
	}

	return err;
}


static int wireaudio_stop(struct re_printf *pf, void *arg)
{
	(void)arg;

	if (gwa) {
		(void)re_hprintf(pf, "audio-loop stopped\n");
		gwa = mem_deref(gwa);
	}

	return 0;
}



static int module_init(void)
{
	info("wireaudio: module_init\n");
	return 0;
}


static int module_close(void)
{
	info("wireaudio: module_close\n");
	wireaudio_stop(NULL, NULL);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(wireaudio) = {
	"wireaudio",
	"application",
	module_init,
	module_close,
};
