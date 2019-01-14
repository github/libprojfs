/* Linux Projected Filesystem
   Copyright (C) 2018-2019 GitHub, Inc.

   See the COPYING file distributed with this library for additional
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
   License along with this library, in the file LICENSE.md; if not,
   see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE		// for basename() in <string.h>
				// and nanosleep()

#include <err.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MOUNT_MAX_WAIT_SEC 30
#define MOUNT_WAIT_TIMESPEC { 0, 1000*1000 }

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
	const struct timespec wait_req = MOUNT_WAIT_TIMESPEC;
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

int main(int argc, const char *argv[])
{
	long int prior_dev;
	int max_wait = MOUNT_MAX_WAIT_SEC;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "Usage: %s <dev-id> <mount-path> "
				"[<max-wait-sec>]\n",
			basename(argv[0]));
		exit(EXIT_FAILURE);
	}

	if (sscanf(argv[1], "%li", &prior_dev) != 1 || prior_dev <= 0) {
		fprintf(stderr, "invalid device ID: %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	if (argc == 4 && (sscanf(argv[3], "%d", &max_wait) != 1 ||
			  max_wait < 0)) {
		fprintf(stderr, "invalid timeout value: %s\n", argv[3]);
		exit(EXIT_FAILURE);
	}

	if (wait_for_mount((dev_t) prior_dev, argv[2], (time_t) max_wait) < 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

