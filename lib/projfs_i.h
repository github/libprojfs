/* Linux Projected Filesystem
   Copyright (C) 2018-2019 GitHub, Inc.

   See the COPYING file distributed with this library for additional
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
   License along with this library, in the file LICENSE.md; if not,
   see <http://www.gnu.org/licenses/>.
*/

#ifndef PROJFS_I_H
#define PROJFS_I_H

#include <dirent.h>

struct projfs_node {
	struct projfs_node *prev, *next;
	int fd;
	char *path;
	ino_t ino;
	dev_t dev;
	uint64_t nlookup;
};

struct projfs_dir {
	DIR *dir;
	long loc;
	struct dirent *ent;
};

/** Private projfs filesystem handle */
struct projfs {
	char *lowerdir;
	char *mountdir;
	struct projfs_handlers handlers;
	void *user_data;
	pthread_mutex_t mutex;
	struct fuse_session *session;
	pthread_t thread_id;
	int error;

	struct projfs_node root;
};

/** Private event handler type */
typedef int (*projfs_handler_t)(struct projfs_event *);

#endif /* PROJFS_I_H */

