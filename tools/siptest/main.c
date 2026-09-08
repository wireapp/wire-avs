#include <unistd.h>
#include <re.h>
#include <avs_wcall.h>

static WUSER_HANDLE wuser;

//#define SIP_AOR "sip:wire@192.168.2.240:5061;regint=0"
#define SIP_AOR "sip:wire@172.20.10.8:5061;regint=0"

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
	wcall_sip_create(wuser, "1111", SIP_AOR);

	while(true) {
		sleep(5);
	}

	wcall_sip_destroy(wuser, "1111", SIP_AOR);
	wcall_sip_close(wuser);

	sleep(5);
	
	return 0;
}
