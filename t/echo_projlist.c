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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

static void print_projlist(const char *argv0,
			   struct test_projlist_entry *entry)
{
	char c;

	while (entry != NULL) {
		if (S_ISDIR(entry->mode))
			c = 'd';
		else if (S_ISLNK(entry->mode))
			c = 'l';
		else if (S_ISREG(entry->mode))
			c = 'f';
		else {
			test_exit_error(argv0, "unexpected projection list "
					       "entry type: 0%06o, %s",
					(unsigned int) (entry->mode & S_IFMT),
					entry->name);
		}

		printf("%c\t%s", c, entry->name);

		if (S_ISDIR(entry->mode) || S_ISREG(entry->mode)) {
			printf("\t0%04o",
			       (unsigned int) (entry->mode & ~S_IFMT));
		}

		if (S_ISREG(entry->mode))
			printf("\t%jd", (intmax_t) entry->size);
		else if (S_ISLNK(entry->mode))
			printf("\t\t");

		if (S_ISLNK(entry->mode) || S_ISREG(entry->mode))
			printf("\t%s", entry->lnk_or_src);

		printf("\n");

		entry = entry->next;
	}
}

int main(int argc, char *const argv[])
{
	unsigned int opt_flags = TEST_OPT_NONE;
	unsigned int projlist_flags = TEST_VAL_UNSET | TEST_FILE_NONE;
	struct test_projlist_entry *projlist = NULL;
	const char *projfile = NULL;

	test_parse_opts(argc, argv, (TEST_OPT_PROJLIST | TEST_OPT_PROJFILE),
			0, 0, NULL, "");

	opt_flags = test_get_opts((TEST_OPT_PROJLIST | TEST_OPT_PROJFILE),
				  &projlist, &projlist_flags, &projfile);

	if (opt_flags == TEST_OPT_NONE)
		test_exit_error(argv[0], "no projection lists specified");

	if ((opt_flags & TEST_OPT_PROJFILE) != TEST_OPT_NONE &&
	    projfile == NULL) {
		test_exit_error(argv[0], "unexpected missing "
					 "projection list filename");
	}

	if ((projlist_flags & TEST_VAL_SET) != TEST_VAL_UNSET) {
		if (projlist == NULL)
			printf("empty projection list\n");
		else
			print_projlist(argv[0], projlist);
	}
	else if ((projlist_flags & TEST_FILE_EXIST) != TEST_FILE_NONE) {
		printf("empty projection list file\n");
	}
	else if ((opt_flags & TEST_OPT_PROJFILE) != TEST_OPT_NONE) {
		test_exit_error(argv[0], "missing projection list file: %s",
				projfile);
	}
	else {
		test_exit_error(argv[0], "unexpected projection list test "
					 "option flags: 0x%04x, 0x%04x",
				opt_flags, projlist_flags);
	}

	exit(EXIT_SUCCESS);
}

