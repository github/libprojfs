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
#include <limits.h>		// for NAME_MAX

#include "../include/projfs.h"

#define TEST_OPT_NUM_HELP	0
#define TEST_OPT_NUM_RETVAL	1
#define TEST_OPT_NUM_RETFILE	2
#define TEST_OPT_NUM_PROJLIST	3
#define TEST_OPT_NUM_PROJFILE	4
#define TEST_OPT_NUM_TIMEOUT	5
#define TEST_OPT_NUM_LOCKFILE	6

#define TEST_OPT_HELP		(0x0001 << TEST_OPT_NUM_HELP)
#define TEST_OPT_RETVAL		(0x0001 << TEST_OPT_NUM_RETVAL)
#define TEST_OPT_RETFILE	(0x0001 << TEST_OPT_NUM_RETFILE)
#define TEST_OPT_PROJLIST	(0x0001 << TEST_OPT_NUM_PROJLIST)
#define TEST_OPT_PROJFILE	(0x0001 << TEST_OPT_NUM_PROJFILE)
#define TEST_OPT_TIMEOUT	(0x0001 << TEST_OPT_NUM_TIMEOUT)
#define TEST_OPT_LOCKFILE	(0x0001 << TEST_OPT_NUM_LOCKFILE)

#define TEST_OPT_NONE		0x0000

#define TEST_VAL_UNSET		0x0000
#define TEST_VAL_SET		0x0001

#define TEST_FILE_NONE		0x0000
#define TEST_FILE_EXIST		0x0002
#define TEST_FILE_VALID		0x0004

#define TEST_PATH_MAX NAME_MAX			// sufficient for testing

struct test_projlist_entry {
	char *name;
	mode_t mode;
	off_t size;
	char *lnk_or_src;
	struct test_projlist_entry *next;
};

__attribute__((noreturn))
void test_exit_error(const char *argv0, const char *fmt, ...);

long int test_parse_long(const char *arg, int base);

int test_parse_retsym(const char *retsym, int *retval);

void test_parse_opts(int argc, char *const argv[], unsigned int opt_flags,
		     int min_args, int max_args, char *args[],
		     const char *args_usage);

void test_parse_mount_opts(int argc, char *const argv[],
			   unsigned int opt_flags,
			   const char **lower_path, const char **mount_path);

unsigned int test_get_opts(unsigned int opt_flags, ...);

void test_free_opts(unsigned int opt_flags, ...);

struct projfs *test_start_mount(const char *lowerdir, const char *mountdir,
				const struct projfs_handlers *handlers,
				size_t handlers_size, void *user_data);

void *test_stop_mount(struct projfs *fs);

void test_wait_signal(void);

