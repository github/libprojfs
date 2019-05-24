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

int main(int argc, const char **argv)
{
	struct projfs *fs;
	if (argc < 3) {
		fprintf(stderr, "usage: %s [OPTIONS ...] "
				"<lower-path> <mount-path>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	fs = test_start_mount(argv[argc - 2], argv[argc - 1], NULL, 0, NULL,
	                      argc - 3, argv + 1);
	test_wait_signal();
	test_stop_mount(fs);

	exit(EXIT_SUCCESS);
}

