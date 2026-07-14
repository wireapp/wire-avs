#include <re.h>
#include "avs.h"

#include <math.h>

// MOS coefficients to be used in G107.2 estimation
// that provides an estimate for wideband audio
const int MOS_PACKETIZATION_TIME_MS = 20;
const int MOS_G107_2_RO = 148;
const int MOS_G107_2_A_IS = 0;
const double MOS_ONE_SIXTH = 1.0 / 6.0;

static double get_absolute_delay(double rtt, double jitter_buffer_delay) {
	return rtt / 2.0 + jitter_buffer_delay + MOS_PACKETIZATION_TIME_MS;
}

// G107.2 Annex A "MOS values derived from the transmission rating factor"
static double mos_mapping(double transmission_rating) {
	if (transmission_rating < 0) {
		return 1;
	}
	else if (transmission_rating > 100) {
		return 4.5;
	}

	return 1 + 0.035 * transmission_rating + transmission_rating * (transmission_rating - 60) * (100 - transmission_rating) * 0.000007;
}

double g107_2_estimate(double rtt, double packet_lost_percentage, double jitter_buffer_delay) {
	double id = 0.0;
	const double absolute_delay = get_absolute_delay(rtt, jitter_buffer_delay);
	const double iee = 10.2 + (132 - 10.2) * (packet_lost_percentage / (packet_lost_percentage + 4.3));

	if (absolute_delay <= 100) {
		id = 0;
	}
	else {
		const double x = (log(absolute_delay) - log(100)) / log(2);
		id = 1.48 * 25 * (pow(1 + pow(x, 6), MOS_ONE_SIXTH) - 3 * pow(1 + pow(x / 3.0, 6), MOS_ONE_SIXTH) + 2);
	}

	const double transmission_rating = (MOS_G107_2_RO + MOS_G107_2_A_IS - id - iee ) / 1.48;
	const double mos = mos_mapping(transmission_rating);

	return mos > 1.0 ? mos : 1.0;
}