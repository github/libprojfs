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

#include <getopt.h>

#include "../include/projfs.h"

#define TEST_OPT_NUM_HELP	0
#define TEST_OPT_NUM_RETVAL	1
#define TEST_OPT_NUM_RETFILE	2
#define TEST_OPT_NUM_ATTRLIST	3
#define TEST_OPT_NUM_ATTRFILE	4
#define TEST_OPT_NUM_TIMEOUT	5
#define TEST_OPT_NUM_LOCKFILE	6

#define TEST_OPT_HELP		(0x0001 << TEST_OPT_NUM_HELP)
#define TEST_OPT_RETVAL		(0x0001 << TEST_OPT_NUM_RETVAL)
#define TEST_OPT_RETFILE	(0x0001 << TEST_OPT_NUM_RETFILE)
#define TEST_OPT_ATTRLIST	(0x0001 << TEST_OPT_NUM_ATTRLIST)
#define TEST_OPT_ATTRFILE	(0x0001 << TEST_OPT_NUM_ATTRFILE)
#define TEST_OPT_TIMEOUT	(0x0001 << TEST_OPT_NUM_TIMEOUT)
#define TEST_OPT_LOCKFILE	(0x0001 << TEST_OPT_NUM_LOCKFILE)

#define TEST_OPT_NONE		0x0000

#define TEST_VAL_UNSET		0x0000
#define TEST_VAL_SET		0x0001

#define TEST_FILE_NONE		0x0000
#define TEST_FILE_EXIST		0x0002
#define TEST_FILE_VALID		0x0004

struct test_mount_args {
	int argc;
	const char **argv;
	const char *lowerdir;
	const char *mountdir;
};

struct test_attr {
	char *name;
	void *value;
	size_t size;
};

union test_entry {
	struct test_attr attr;
};

struct test_list_entry {
	struct test_list_entry *next;
	union test_entry entry;
};

__attribute__((noreturn))
void test_exit_error(const char *argv0, const char *fmt, ...);

void test_print_value_quoted(const char *value, size_t size);

long int test_parse_long(const char *arg, int base);

int test_parse_retsym(const char *retsym, int *retval);

void test_parse_opts(int argc, char *const argv[],
		     struct test_mount_args *mount_args,
		     unsigned int opt_flags, int min_args, int max_args,
		     char *args[], const char *args_usage);

void test_parse_mount_opts(int argc, char *const argv[],
			   struct test_mount_args *mount_args,
			   unsigned int opt_flags);

unsigned int test_get_opts(unsigned int opt_flags, ...);

void test_free_opts(unsigned int opt_flags, ...);

void test_free_mount_opts(struct test_mount_args *mount_args,
			  unsigned int opt_flags, ...);

struct projfs *test_start_mount(struct test_mount_args *mount_args,
				const struct projfs_handlers *handlers,
				size_t handlers_size, void *user_data);

void *test_stop_mount(struct projfs *fs, struct test_mount_args *mount_args);

void test_wait_signal(void);

