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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

static int retval;

static int test_handle_event(struct projfs_event *event, const char *desc,
			     int perm)
{
	int ret = retval;

	printf("  test %s for %s: "
	       "0x%04" PRIx64 "-%08" PRIx64 ", %d\n",
	       desc, event->path,
	       event->mask >> 32, event->mask & 0xFFFFFFFF, event->pid);

	if (ret == RETVAL_DEFAULT)
		ret = perm ? PROJFS_ALLOW : 0;
	else if(!perm && ret > 0)
		ret = 0;

	return ret;
}

static int test_notify_event(struct projfs_event *event)
{
	return test_handle_event(event, "event notification", 0);
}

static int test_perm_event(struct projfs_event *event)
{
	return test_handle_event(event, "permission request", 1);
}

int main(int argc, const char **argv)
{
	const char *lower_path, *mount_path;
	struct projfs *fs;
	struct projfs_handlers handlers;

	tst_parse_opts(argc, argv, 0, &lower_path, &mount_path, &retval);

	handlers.handle_notify_event = &test_notify_event;
	handlers.handle_perm_event = &test_perm_event;

	fs = tst_start_mount(lower_path, mount_path,
			     &handlers, sizeof(handlers), NULL);
	tst_wait_signal();
	tst_stop_mount(fs);

	exit(EXIT_SUCCESS);
}

