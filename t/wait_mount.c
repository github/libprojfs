/* Linux Projected Filesystem
   Copyright (C) 2018-2019 GitHub, Inc.

   See the NOTICE file distributed with this library for additional
   information regarding copyright ownership.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library, in the file COPYING; if not,
   see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE		// for nanosleep() in <time.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "test_common.h"

#define MOUNT_SLEEP_TIMESPEC { 0, 1000*1000 }

#define MOUNT_WAIT_SEC_DEFAULT 30
#define MOUNT_WAIT_SEC_MAX 3600

static int get_curr_time(time_t *sec)
{
	struct timeval tv;
	int ret = 0;

	if (gettimeofday(&tv, NULL) < 0) {
		warn("unable to get current time");
		ret = -1;
	} else {
		*sec = tv.tv_sec;
	}

	return ret;
}

/* modelled on wait_for_mount() in FUSE's test/util.py */
static int wait_for_mount(dev_t prior_dev, const char *mountdir,
			  time_t max_wait)
{
	struct stat mnt;
	const struct timespec wait_req = MOUNT_SLEEP_TIMESPEC;
	time_t start, now, wait = 0;
	time_t warn_sec = 0;
	int ret = 0;

	ret = get_curr_time(&start);
	if (ret < 0)
		goto out;

	do {
		if (stat(mountdir, &mnt) != 0) {
			// limit warnings to once per second
			if (warn_sec < wait) {
				warn("unable to query mount point: %s",
				     mountdir);
				warn_sec = wait;
			}
		} else if (prior_dev != mnt.st_dev) {
			break;
		}

		nanosleep(&wait_req, NULL);

		ret = get_curr_time(&now);
		if (ret < 0)
			break;

		wait = now - start;
		if (wait >= max_wait) {
			warnx("timeout waiting for filesystem mount at: %s",
			      mountdir);
			ret = -1;
			break;
		}
	} while (1);

out:
	return ret;
}

int main(int argc, char *const argv[])
{
	char *args[3];
	long int prior_dev;
	long int timeout;
	unsigned int opt_flags;
	time_t max_wait = MOUNT_WAIT_SEC_DEFAULT;

	test_parse_opts(argc, argv, TEST_OPT_TIMEOUT, 2, 2, args,
			"<device-id> <mount-path>");

	prior_dev = test_parse_long(args[0], 16);
	if (errno > 0 || prior_dev <= 0)
		test_exit_error(argv[0], "invalid device ID: %s", args[0]);

	opt_flags = test_get_opts(TEST_OPT_TIMEOUT, &timeout);
	if (opt_flags != TEST_OPT_NONE) {
		if (timeout > MOUNT_WAIT_SEC_MAX) {
			test_exit_error(argv[0], "invalid timeout: %li",
					timeout);
		}
		max_wait = timeout;
	}

	if (wait_for_mount((dev_t)prior_dev, args[1], max_wait) < 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}
