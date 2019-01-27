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

#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

int main(int argc, char *const argv[])
{
	const char *lower_path, *mount_path;
	struct projfs *fs;

	test_parse_mount_opts(argc, argv, TEST_OPT_NONE,
			      &lower_path, &mount_path, NULL);

	fs = test_start_mount(lower_path, mount_path, NULL, 0, NULL);
	test_wait_signal();
	test_stop_mount(fs);

	exit(EXIT_SUCCESS);
}

