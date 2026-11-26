/*
 * 2:1 resampling between the wire side (16kHz) and the PSTN mix (8kHz),
 * mono S16. Each direction keeps its own state, zeroed to start.
 */

#define WA_RESAMP_TAPC 47

struct wa_resamp {
	int16_t histv[WA_RESAMP_TAPC];  /* 16kHz, newest last */
};

/* outv has room for 2 * inc samples, returns the count written */
size_t wa_resamp_up(struct wa_resamp *rs, int16_t *outv,
		    const int16_t *inv, size_t inc);

/* outv has room for inc / 2 samples, returns the count written */
size_t wa_resamp_down(struct wa_resamp *rs, int16_t *outv,
		      const int16_t *inv, size_t inc);
