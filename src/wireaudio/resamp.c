
#include <string.h>
#include <re.h>
#include "resamp.h"


/*
 * Low-pass FIR at 16kHz: Kaiser windowed sinc, 47 taps, cutoff 4kHz,
 * beta 5, in Q15 (sum 32768). Flat within 0.02 dB up to 3.4kHz and at
 * least 55 dB down from 4.6kHz. Being a half-band filter, every other
 * tap is 0. rem's auresamp uses a filter designed for 48kHz for 8/16kHz,
 * which cuts everything above about 1.2kHz.
 */
static const int16_t tapv[] = {
	  -17,      0,     43,      0,    -85,      0,    149,      0,
	 -241,      0,    371,      0,   -553,      0,    813,      0,
	-1207,      0,   1875,      0,  -3347,      0,  10389,  16388,
	10389,      0,  -3347,      0,   1875,      0,  -1207,      0,
	  813,      0,   -553,      0,    371,      0,   -241,      0,
	  149,      0,    -85,      0,     43,      0,    -17
};

/* WA_RESAMP_TAPC must match the filter (baresip builds as C99) */
typedef char tapc_check[ARRAY_SIZE(tapv) == WA_RESAMP_TAPC ? 1 : -1];


static void push(struct wa_resamp *rs, int16_t x)
{
	memmove(rs->histv, rs->histv + 1,
		sizeof(rs->histv) - sizeof(rs->histv[0]));
	rs->histv[WA_RESAMP_TAPC - 1] = x;
}

/* Filter output for the current history, times 2^gain. The sum of
 * the absolute taps times full scale fits in 32 bits.
 */
static int16_t fir(const struct wa_resamp *rs, unsigned gain)
{
	int32_t acc = 0;
	size_t i;

	for (i = 0; i < WA_RESAMP_TAPC; i++)
		acc += (int32_t)tapv[i] * rs->histv[i];

	acc >>= 15 - gain;

	if (acc > INT16_MAX)
		return INT16_MAX;
	if (acc < INT16_MIN)
		return INT16_MIN;

	return (int16_t)acc;
}

/* 8kHz -> 16kHz: a zero after each sample, filtered with gain 2 */
size_t wa_resamp_up(struct wa_resamp *rs, int16_t *outv,
		    const int16_t *inv, size_t inc)
{
	size_t i;

	for (i = 0; i < inc; i++) {
		push(rs, inv[i]);
		*outv++ = fir(rs, 1);
		push(rs, 0);
		*outv++ = fir(rs, 1);
	}

	return inc * 2;
}

/* 16kHz -> 8kHz: filtered, every second sample */
size_t wa_resamp_down(struct wa_resamp *rs, int16_t *outv,
		      const int16_t *inv, size_t inc)
{
	size_t i;

	for (i = 0; i + 1 < inc; i += 2) {
		push(rs, inv[i]);
		push(rs, inv[i + 1]);
		*outv++ = fir(rs, 0);
	}

	return inc / 2;
}
