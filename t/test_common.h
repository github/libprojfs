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

#include "../include/projfs.h"
#ifdef PROJFS_VFSAPI
#include "../include/projfs_vfsapi.h"
#endif /* PROJFS_VFSAPI */

#define RETVAL_DEFAULT 1000		// magic unused value

#define TEST_OPT_NONE		0x0000
#define TEST_OPT_VFSAPI		0x8000		// not a command-line option

void test_exit_error(const char *argv0, const char *fmt, ...);

long int test_parse_long(const char *arg, int base);

int test_parse_retsym(int vfsapi, const char *retsym, int *retval);

void test_parse_opts(int argc, char *const argv[], int vfsapi,
		     const char **lower_path, const char **mount_path,
		     int *retval);

void test_parse_mount_opts(int argc, char *const argv[],
			   unsigned int opt_flags,
			   const char **lower_path, const char **mount_path,
			   int *retval);

struct projfs *test_start_mount(const char *lowerdir, const char *mountdir,
				const struct projfs_handlers *handlers,
				size_t handlers_size, void *user_data);

void *test_stop_mount(struct projfs *fs);

#ifdef PROJFS_VFSAPI
void test_start_vfsapi_mount(const char *storageRootFullPath,
			     const char *virtualizationRootFullPath,
			     PrjFS_Callbacks callbacks,
			     unsigned int poolThreadCount,
			     PrjFS_MountHandle** mountHandle);

void test_stop_vfsapi_mount(PrjFS_MountHandle* handle);
#endif /* PROJFS_VFSAPI */

void test_wait_signal(void);

