/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <gtest/gtest.h>
#include <re.h>
#include <avs.h>
#include "fakes.hpp"


#if TARGET_OS_IPHONE
int ztest_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
	int err;

	fd_setsize(4096);

	/* initialization of libre must be done here. it is not
	 * fair to expect each testcase to init/close libre.
	 *
	 */
	err = libre_init();
	if (err) {
		re_fprintf(stderr, "libre_init failed (%m)\n", err);
		return err;
	}

	err = avs_init(0);
	if (err) {
		re_fprintf(stderr, "avs_init failed (%m)\n", err);
		return err;
	}

	sys_coredump_set(true);

	testing::InitGoogleTest(&argc, argv);
	int r = RUN_ALL_TESTS();

	avs_close();
	libre_close();

	if (r != 0)
		return r;

	/* check for memory leaks */
	mem_debug();
	tmr_debug();

	struct memstat memstat;
	if (0 == mem_get_stat(&memstat)) {

		if (memstat.bytes_cur != 0) {
			re_fprintf(stderr,
				   "\n****************"
				   " MEMORY LEAKS DETECTED!"
				   " ****************\n");
			re_fprintf(stderr, "test leaked memory: %zu bytes\n",
				   memstat.bytes_cur);
			re_fprintf(stderr,
				   "\nPlease fix the leaks ASAP"
				   " before committing your changes.\n");
			re_fprintf(stderr,
				   "****************************"
				   "****************************"
				   "\n");
			return 1;
		}
	}

	return r;
}
