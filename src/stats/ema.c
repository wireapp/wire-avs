#include <re.h>
#include "avs.h"

#include <math.h>

// EMA coefficients to balance responsiveness and averaging
// Typical sampling and reporting intervals 1 sec and 10
const double SAMPLING_RATE_MSEC = 1000;
const double AVERAGING_RATE_MSEC = 10000;
const double MULTIPLIER = 2.0;
const double ALPHA = SAMPLING_RATE_MSEC / AVERAGING_RATE_MSEC * MULTIPLIER;
const double EMA_MIN_VALUE = 1.0;
const double EMA_MAX_VALUE = 3.0;

struct avs_ema {
	float val;
	void *arg;
};

static void destructor(void *arg)
{
	const struct avs_ema *ema = arg;

	(void)ema;
}

int ema_alloc(struct avs_ema **emap, void *arg)
{
	struct avs_ema *ema;
	int err = 0;

	if (!emap)
		return EINVAL;

	ema = mem_zalloc(sizeof(*ema), destructor);
	if (!ema)
		return ENOMEM;

	ema->val = EMA_MIN_VALUE;
	ema->arg = arg;

	*emap = ema;

	return err;
}

int ema_update(struct avs_ema *ema, float data)
{
	if (data < EMA_MIN_VALUE || data > EMA_MAX_VALUE) {
		return EINVAL;
	}
	ema->val = (data - ema->val) * ALPHA + ema->val;
	return 0;
}

int ema_get_val(const struct avs_ema *ema, int *val)
{
	if (!ema || !val)
		return EINVAL;

	*val = round(ema->val);

	return 0;
}