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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_common.h"

static int test_handle_event(struct projfs_event *event, const char *desc,
			     int proj, int perm)
{
	unsigned int opt_flags, ret_flags;
	const char *retfile, *lockfile = NULL;
	int ret, timeout = 0, fd = 0, res;

	opt_flags = test_get_opts((TEST_OPT_RETVAL | TEST_OPT_RETFILE |
				   TEST_OPT_TIMEOUT | TEST_OPT_LOCKFILE),
				  &ret, &ret_flags, &retfile, &timeout,
				  &lockfile);

	if ((opt_flags & TEST_OPT_RETFILE) == TEST_OPT_NONE ||
	    (ret_flags & TEST_FILE_EXIST) != TEST_FILE_NONE) {
		printf("  test %s for %s%s%s: "
		       "0x%04" PRIx64 "-%08" PRIx64 ", %d\n",
		       desc, event->path,
		       ((event->target_path == NULL) ? "" : ", "),
		       ((event->target_path == NULL) ? ""
						     : event->target_path),
		       event->mask >> 32, event->mask & 0xFFFFFFFF,
		       event->pid);
	}

	if (proj) {
		if ((event->mask & ~PROJFS_ONDIR) != PROJFS_CREATE_SELF) {
			fprintf(stderr, "unknown projection flags\n");
			ret = -EINVAL;
		}

		// TODO: hydrate file/dir based on projection list
	}

	if (lockfile) {
		fd = open(lockfile, (O_CREAT | O_EXCL | O_RDWR), 0600);
		if (fd == -1 && errno == EEXIST)
			return -EEXIST;
		else if (fd == -1)
			return -EINVAL;
	}

	if (timeout)
		sleep(timeout);

	if (lockfile) {
		close(fd);
		res = unlink(lockfile);
		if (res == -1)
			return -EINVAL;
	}

	if ((ret_flags & TEST_VAL_SET) == TEST_VAL_UNSET)
		ret = perm ? PROJFS_ALLOW : 0;
	else if (!perm && ret > 0)
		ret = 0;

	test_free_opts();

	return ret;
}

static int test_proj_event(struct projfs_event *event)
{
	return test_handle_event(event, "projection request", 1, 0);
}

static int test_notify_event(struct projfs_event *event)
{
	return test_handle_event(event, "event notification", 0, 0);
}

static int test_perm_event(struct projfs_event *event)
{
	return test_handle_event(event, "permission request", 0, 1);
}

int main(int argc, char *const argv[])
{
	const char *lower_path, *mount_path;
	struct projfs *fs;
	struct projfs_handlers handlers = { 0 };

	test_parse_mount_opts(argc, argv,
			      (TEST_OPT_RETVAL | TEST_OPT_RETFILE |
			       TEST_OPT_TIMEOUT | TEST_OPT_LOCKFILE),
			      &lower_path, &mount_path);

	handlers.handle_proj_event = &test_proj_event;
	handlers.handle_notify_event = &test_notify_event;
	handlers.handle_perm_event = &test_perm_event;

	fs = test_start_mount(lower_path, mount_path,
			      &handlers, sizeof(handlers), NULL);
	test_wait_signal();
	test_stop_mount(fs);

	exit(EXIT_SUCCESS);
}

