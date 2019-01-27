/* Linux Projected Filesystem
   Copyright (C) 2019 GitHub, Inc.

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"

int main(int argc, const char **argv)
{
	int err;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <errno>\n", basename(argv[0]));
		exit(EXIT_FAILURE);
	}

	err = test_find_retval(0, argv[1], "errno");

	if (err > 0) {
		fprintf(stderr, "%s: invalid errno: %s\n", basename(argv[0]),
							   argv[1]);
		exit(EXIT_FAILURE);
	}

	printf("%s\n", strerror(-err));

	exit(EXIT_SUCCESS);
}

