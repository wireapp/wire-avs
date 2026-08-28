#include <unistd.h>
#include <re.h>
#include <avs_wcall.h>

static WUSER_HANDLE wuser;

#define SIP_AOR "sip:foo@bar.com;regint=0"

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <config-path>\n", argv[0]);
		return 1;
	}
	
	wcall_run();

	wuser = wcall_create_ex("join_by", "phone", false,
				"pstn",
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL,
				NULL);
	
	wcall_sip_init(wuser, argv[1]);
	wcall_sip_create(wuser, SIP_AOR);

	sleep(5);

	wcall_sip_destroy(wuser, SIP_AOR);
	wcall_sip_close(wuser);

	sleep(5);
	
	return 0;
}
