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

#include <limits.h>
#include <stdlib.h>
#include <sys/time.h>

#include "../lib/fdtable.h"
#include "test_common.h"

static pid_t pids[MAX_TABLE_SIZE];

static pid_t get_random_pid(void)
{
	return (random() & (INT_MAX >> 1)) + 1;
}

static void test_insert(const char *argv0, struct fdtable *table, int fd)
{
	pid_t pid = get_random_pid();

	if (fdtable_insert(table, fd, pid) == -1) {
		test_exit_error(argv0, "unable to insert entry with key %d "
				       "and value %d; table may be full",
				fd, pid);
	}
	pids[fd] = pid;
}

static void test_replace(const char *argv0, struct fdtable *table, int fd)
{
	pid_t pid = get_random_pid();

	if (fdtable_replace(table, fd, pid) == -1) {
		test_exit_error(argv0, "unable to replace entry with key %d "
				       "and value %d; key not found",
				fd, pid);
	}
	pids[fd] = pid;
}

static int test_remove(const char *argv0, struct fdtable *table, int fd)
{
	pid_t pid;
	int ret;

	ret = fdtable_remove(table, fd, &pid);
	if (ret == -1 && pids[fd] > 0) {
		test_exit_error(argv0, "unable to remove entry with key %d "
				       "and value %d; key not found",
				fd, pids[fd]);
	} else if (ret == 0 && pid != pids[fd]) {
		test_exit_error(argv0, "incorrect entry with value %d "
				       "removed for key %d; correct value %d",
				pid, fd, pids[fd]);
	} else if (ret == 0) {
		pids[fd] = 0;
	}
	return ret;
}

int main(int argc, char *const argv[])
{
	struct fdtable *table;
	struct timeval tv;
	unsigned int max_load = 2 * MAX_TABLE_SIZE / 3;
	unsigned int min_load = MAX_TABLE_SIZE / 2;
	unsigned int load_range = max_load - min_load;
	unsigned int load = 0;
	unsigned int target = 0;
	int fd, i;

	gettimeofday(&tv, NULL);
	srandom(tv.tv_sec + tv.tv_usec);

	table = fdtable_create();
	if (table == NULL)
		test_exit_error(argv[0], "unable to create fdtable");

	for (i = 0; i < MAX_TABLE_SIZE * 10; ++i) {
		while (target == load)
			target = random() % load_range + min_load;

		fd = random() & (MAX_TABLE_SIZE - 1);
		if (target > load) {
			if (pids[fd] == 0) {
				test_insert(argv[0], table, fd);
				++load;
			} else {
				test_replace(argv[0], table, fd);
			}
		} else {
			if (test_remove(argv[0], table, fd) == 0)
				--load;
		}
	}

	while (load < max_load) {
		fd = random() & (MAX_TABLE_SIZE - 1);
		if (pids[fd] == 0) {
			test_insert(argv[0], table, fd);
			++load;
		}
	}

	fd = 0;
	while (pids[fd] > 0)
		++fd;
	if (fdtable_insert(table, fd, 1) != -1) {
		test_exit_error(argv[0], "insert above maximum table size "
					 "and load factor succeeded");
	}

	for (i = 0; i < MAX_TABLE_SIZE; ++i)
		test_remove(argv[0], table, i);

	fdtable_destroy(table);

	exit(EXIT_SUCCESS);
}

