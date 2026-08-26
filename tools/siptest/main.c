#include <unistd.h>
#include <re.h>
#include <avs_wcall.h>

static WUSER_HANDLE wuser;

int main(int argc, char **argv)
{
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
	wcall_sip_create(wuser, "sip:foo@bar.com;regint=0");

	sleep(10);

	wcall_sip_destroy(wuser, "sip:foo@bar.com");
	wcall_sip_close(wuser);
		
	return 0;
}
