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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_common.h"

static int create_proj_dir_child(struct projfs *fs, const char *path,
				 struct test_projlist_entry *entry)
{
	mode_t mode = entry->mode & ~S_IFMT;
	int ret;

	if (S_ISDIR(entry->mode))
		ret = projfs_create_proj_dir(fs, path, mode);
	else if (S_ISLNK(entry->mode))
		ret = projfs_create_proj_symlink(fs, path, entry->lnk_or_src);
	else
		ret = projfs_create_proj_file(fs, path, entry->size, mode);

	if (ret != 0) {
		fprintf(stderr, "unable to create projected path: %s: %s\n",
			path, strerror(ret));
	}

	return ret;
}

static int create_proj_dir_children(struct projfs_event *event,
				    struct test_projlist_entry *projlist)
{
	struct test_projlist_entry *entry = projlist;
	size_t len = strlen(event->path);
	char *path, *name;
	int ret = 0;

	path = malloc(len + 1 + TEST_PATH_MAX + 1);
	if (path == NULL) {
		fprintf(stderr, "unable to allocate projected path buffer\n");
		return errno;
	}

	memcpy(path, event->path, len);
	path[len] = '/';

	name = path + len + 1;
	while (entry != NULL) {
		strcpy(name, entry->name);
		ret = create_proj_dir_child(event->fs, path, entry);
		if (ret != 0)
			break;
		entry = entry->next;
	}

	free(path);

	return ret;
}

static struct test_projlist_entry *
find_projlist_entry(struct test_projlist_entry *projlist, const char *name)
{
	struct test_projlist_entry *entry = projlist;

	while (entry != NULL) {
		if (strcmp(name, entry->name) == 0)
			break;
		entry = entry->next;
	}

	return entry;
}

static int fill_proj_file(int fd, const char *src_path)
{
	int src_fd;
	struct stat st;
	off_t len;
	int ret = 0;

	src_fd = open(src_path, O_RDONLY);
	if (src_fd == -1) {
		fprintf(stderr, "unable to open source file: %s: %s\n",
			src_path, strerror(errno));
		return errno;
	}

	if (fstat(src_fd, &st) == -1) {
		ret = errno;
		fprintf(stderr, "unable to stat source file: %s: %s\n",
			src_path, strerror(ret));
		goto out_close;
	}

	len = st.st_size;
	while (len > 0) {
		ssize_t off;

		off = sendfile(fd, src_fd, NULL, len);
		if (off == -1) {
			ret = errno;
			fprintf(stderr, "unable to copy source file: %s: %s\n",
				src_path, strerror(ret));
			break;
		}

		len -= off;
	}

out_close:
	close(src_fd);
	return ret;
}

static int test_proj_event(struct projfs_event *event)
{
	struct test_projlist_entry *projlist;
	unsigned int opt_flags, proj_flags;
	const char *projfile;
	int ret = 0;

	if ((event->mask & ~PROJFS_ONDIR) != PROJFS_CREATE_SELF) {
		fprintf(stderr, "unknown projection flags\n");
		return -EINVAL;
	}

	opt_flags = test_get_opts(TEST_OPT_PROJLIST | TEST_OPT_PROJFILE,
				  &projlist, &proj_flags, &projfile);

	if ((opt_flags & TEST_OPT_PROJFILE) != TEST_OPT_NONE &&
	    (proj_flags & TEST_FILE_EXIST) == TEST_FILE_NONE) {
		goto out_opts;
	}

	if (event->mask & PROJFS_ONDIR) {
		ret = create_proj_dir_children(event, projlist);
	} else {
		struct test_projlist_entry *entry;
		const char *name;

		name = strrchr(event->path, '/');
		if (name == NULL)
			name = event->path;
		else
			++name;

		/* TODO: when we support opaque user data in our xattrs,
		 *       store entry->lnk_or_src during file creation, then
		 *       retrieve here instead of looking for matching entry
		 */
		entry = find_projlist_entry(projlist, name);
		if (entry == NULL) {
			fprintf(stderr, "no matching projection list entry "
					"for projected file: %s\n",
				event->path);
			ret = ENOENT;
			goto out_opts;
		}

		ret = fill_proj_file(event->fd, entry->lnk_or_src);
		if (ret != 0) {
			fprintf(stderr, "unable to copy data into "
					"projected file: %s, %s\n",
				event->path, entry->lnk_or_src);
		}
	}

out_opts:
	test_free_opts(opt_flags, projlist);

	return -ret;
}

int main(int argc, char *const argv[])
{
	const char *lower_path, *mount_path;
	struct projfs *fs;
	struct projfs_handlers handlers = { 0 };

	test_parse_mount_opts(argc, argv,
			      TEST_OPT_PROJLIST | TEST_OPT_PROJFILE,
			      &lower_path, &mount_path);

	handlers.handle_proj_event = &test_proj_event;

	fs = test_start_mount(lower_path, mount_path,
			      &handlers, sizeof(handlers), NULL);
	test_wait_signal();
	test_stop_mount(fs);

	exit(EXIT_SUCCESS);
}
