#include <re.h>
#include "avs_msystem.h"
#include "avs_version.h"

#ifndef ARCH
#define ARCH AVS_ARCH
#endif

#ifndef OS
#define OS AVS_OS
#endif

static char ver_str[256] = "";

const char *avs_version_str(void)
{
	const char *ver = msystem_get_version();
	const char *project = msystem_get_project();

	snprintf(ver_str, sizeof(ver_str),
		 "%s %s (%s/%s)",
		 project ? project : AVS_PROJECT,
		 ver ? ver : AVS_VERSION,
		 ARCH, OS);

	return ver_str;
}


const char *avs_version_short(void)
{
	const char *ver = msystem_get_version();

	return ver ? ver : AVS_VERSION;
}
