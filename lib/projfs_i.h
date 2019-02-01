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

#ifndef PROJFS_I_H
#define PROJFS_I_H

/** Private projfs filesystem handle */
struct projfs {
	char *lowerdir;
	char *mountdir;
	struct projfs_handlers handlers;
	void *user_data;
	pthread_mutex_t mutex;
	struct fuse_session *session;
	int lowerdir_fd;
	pthread_t thread_id;
	int error;
};

/** Private event handler type */
typedef int (*projfs_handler_t)(struct projfs_event *);

/**
 * Create a new directory with the given projection flag.
 *
 * @param[in] fs Projected filesystem handle.
 * @param[in] path Relative path of new directory under projfs mount point.
 * @param[in] mode File mode with which to create the new directory.
 * @param[in] proj_flag True if directory is projected; false otherwise.
 * @return Zero on success or an \p errno(3) code on failure.
 */
int _projfs_make_dir(struct projfs *fs, const char *path, mode_t mode,
                     uint8_t proj_flag);


#endif /* PROJFS_I_H */

