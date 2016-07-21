#include "avs_version.h"

static const char *avs_software =
	AVS_PROJECT " " AVS_VERSION " (" ARCH "/" OS ")";


const char *avs_version_str(void)
{
	return avs_software;
}
