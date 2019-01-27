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

#define _GNU_SOURCE		// for basename() in <string.h>
				// and nanosleep() in <time.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	} else
		*sec = tv.tv_sec;

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
				warn("unable to check mount point");
				warn_sec = wait;
			}
		} else if (prior_dev != mnt.st_dev)
			break;

		nanosleep(&wait_req, NULL);

		ret = get_curr_time(&now);
		if (ret < 0)
			break;

		wait = now - start;
		if (wait >= max_wait) {
			warnx("timeout waiting for filesystem mount");
			ret = -1;
			break;
		}
	} while (1);

out:
	return ret;
}

int main(int argc, char *const argv[])
{
	long int prior_dev;
	long int timeout;
	time_t max_wait = MOUNT_WAIT_SEC_DEFAULT;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "Usage: %s <dev-id> <mount-path> "
				"[<max-wait-sec>]\n",
			basename(argv[0]));
		exit(EXIT_FAILURE);
	}

        prior_dev = test_parse_long(argv[1], 16);
        if (errno > 0 || prior_dev <= 0)
		test_exit_error(argv[0], "invalid device ID: %s", argv[1]);

	if (argc == 4) {
		timeout = test_parse_long(argv[3], 10);
		if (errno > 0 || timeout < 0 || timeout > MOUNT_WAIT_SEC_MAX)
			test_exit_error(argv[0], "invalid timeout value: %s",
					argv[3]);
		max_wait = timeout;
	}

	if (wait_for_mount((dev_t)prior_dev, argv[2], max_wait) < 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

