#include "avs_version.h"

#ifndef ARCH
#define ARCH AVS_ARCH
#endif

#ifndef OS
#define OS AVS_OS
#endif

static const char *avs_software =
	AVS_PROJECT " " AVS_VERSION " (" ARCH "/" OS ")";
static const char *avs_short_ver = AVS_VERSION;


const char *avs_version_str(void)
{
	return avs_software;
}


const char *avs_version_short(void)
{
	return avs_short_ver;
}
