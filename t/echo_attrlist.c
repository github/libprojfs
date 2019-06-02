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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"

static void print_attr_value(const void *value, size_t size)
{
	unsigned char c;
	size_t i;
	int hex = 0;

	for (i = 0; i < size; ++i) {
		c = *((unsigned char*)value + i);
		if ((c < 0x20 && c != '\t' && c != '\n') || c > 0x7E) {
			hex = 1;
			break;
		}
	}

	if (!hex) {
		test_print_value_quoted((const char*)value, size);
		return;
	}

	printf("0x");
	for (i = 0; i < size; ++i) {
		c = *((unsigned char*)value + i);
		printf("%02X", (unsigned int)c);
	}
}

static void print_attrlist(const char *argv0, struct test_list_entry *entry)
{
	while (entry != NULL) {
		struct test_attr *attr = &entry->entry.attr;

		test_print_value_quoted(attr->name, strlen(attr->name));

		printf("\t%jd\t", (intmax_t)attr->size);

		print_attr_value(attr->value, attr->size);

		putchar('\n');

		entry = entry->next;
	}
}

int main(int argc, char *const argv[])
{
	unsigned int opt_flags = TEST_OPT_NONE;
	unsigned int attrlist_flags = TEST_VAL_UNSET | TEST_FILE_NONE;
	struct test_list_entry *attrlist = NULL;
	const char *attrfile = NULL;

	test_parse_opts(argc, argv, NULL,
			TEST_OPT_ATTRLIST | TEST_OPT_ATTRFILE, 0, 0, NULL, "");

	opt_flags = test_get_opts(TEST_OPT_ATTRLIST | TEST_OPT_ATTRFILE,
				  &attrlist, &attrlist_flags, &attrfile);

	if (opt_flags == TEST_OPT_NONE)
		test_exit_error(argv[0], "no attribute lists specified");

	if ((opt_flags & TEST_OPT_ATTRFILE) != TEST_OPT_NONE &&
	    attrfile == NULL) {
		test_exit_error(argv[0], "unexpected missing "
					 "attribute list filename");
	}

	if ((attrlist_flags & TEST_VAL_SET) != TEST_VAL_UNSET) {
		if (attrlist == NULL)
			printf("empty attribute list\n");
		else
			print_attrlist(argv[0], attrlist);
	}
	else if ((attrlist_flags & TEST_FILE_EXIST) != TEST_FILE_NONE) {
		printf("empty attribute list file\n");
	}
	else if ((opt_flags & TEST_OPT_ATTRFILE) != TEST_OPT_NONE) {
		test_exit_error(argv[0], "missing or broken "
					 "attribute list file: %s",
				attrfile);
	}
	else {
		test_exit_error(argv[0], "unexpected attribute list test "
					 "option flags: 0x%04x, 0x%04x",
				opt_flags, attrlist_flags);
	}

	exit(EXIT_SUCCESS);
}

