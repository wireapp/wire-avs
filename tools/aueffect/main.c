#include <ctype.h>
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <re.h>
#include <avs.h>

static void progress_handler(int progress, void *arg)
{
	printf("Progress=%d\n", progress);
}


static void log_handler(uint32_t level, const char *msg, void *arg)
{
	fprintf(stdout, msg, arg);
}


struct log log_def = {
	.h = log_handler
}; 

int main(int argc, char *argv[])
{
	char *wavin;
	char *wavout;
	int err;

	log_set_min_level(LOG_LEVEL_DEBUG);
	//log_register_handler(&log_def);
	
	if (argc != 3) {
		fprintf(stderr, "Usage: %s wavin wavout\n", argv[0]);
		return 22;
	}

	wavin = argv[1];
	wavout = argv[2];

	err = apply_effect_to_wav(wavin, wavout, AUDIO_EFFECT_NONE, false,
				  progress_handler, NULL);
	printf("WAV completed with err=%d\n", err);

	return 0;
}
